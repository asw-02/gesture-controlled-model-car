#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include <string.h>

#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"

constexpr uint8_t sdaPin = 21;
constexpr uint8_t sclPin = 22;
constexpr uint8_t driveButtonPin = 4;
constexpr uint8_t ledPin = 19;
constexpr uint8_t radioChannel = 1;
constexpr uint32_t sendIntervalMs = 50;
constexpr uint32_t pressDebounceMs = 25;
constexpr uint8_t carMac[6] = {0, 0, 0, 0, 0, 0};  // Enter the car's STA MAC address

// ESP-NOW payload: version bits 0x10, motion command in the low four bits.
enum class Motion : uint8_t {
  Stop = 0, Forward, Backward, Left, Right, TurnLeft, TurnRight
};
constexpr uint8_t protocolVersion = 0x10;

struct Orientation {
  float yaw;
  float pitch;
  float roll;
};

MPU6050 mpu;
Orientation neutral{};
bool ready = false;
bool haveNeutral = false;
bool buttonWasDown = false;
uint32_t pressedAt = 0;
uint32_t lastSentAt = 0;

bool readOrientation(Orientation &orientation) {
  uint8_t packet[64];
  if (!mpu.dmpGetCurrentFIFOPacket(packet)) {
    return false;
  }

  Quaternion quaternion;
  VectorFloat gravity;
  float ypr[3];
  mpu.dmpGetQuaternion(&quaternion, packet);
  mpu.dmpGetGravity(&gravity, &quaternion);
  mpu.dmpGetYawPitchRoll(ypr, &quaternion, &gravity);
  constexpr float degreesPerRadian = static_cast<float>(RAD_TO_DEG);
  orientation = {ypr[0] * degreesPerRadian, ypr[1] * degreesPerRadian,
                 ypr[2] * degreesPerRadian};
  return isfinite(orientation.yaw) && isfinite(orientation.pitch) && isfinite(orientation.roll);
}

Motion motionFromOrientation(const Orientation &current) {
  const float pitch = current.pitch - neutral.pitch;
  const float yaw = remainderf(current.yaw - neutral.yaw, 360.0f);
  const float roll = remainderf(current.roll - neutral.roll, 360.0f);

  // Gesture order and thresholds approximately match the original firmware.
  if (pitch < -26.0f) return Motion::Forward;
  if (pitch > 17.0f) return Motion::Backward;
  if (yaw > 38.0f) return Motion::TurnRight;
  if (yaw < -40.0f) return Motion::TurnLeft;
  if (roll > 24.0f) return Motion::Right;
  if (roll < -33.0f) return Motion::Left;
  return Motion::Stop;
}

void sendMotion(Motion motion, uint32_t now) {
  const uint8_t message = protocolVersion | static_cast<uint8_t>(motion);
  esp_now_send(carMac, &message, sizeof(message));
  lastSentAt = now;
}

void setup() {
  pinMode(driveButtonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  Serial.begin(115200);

  const uint8_t unsetMac[6] = {};
  if (memcmp(carMac, unsetMac, sizeof(carMac)) == 0) {
    Serial.println("Set carMac to the car's STA MAC address.");
    return;
  }
  if (!WiFi.mode(WIFI_STA) ||
      esp_wifi_set_channel(radioChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
      esp_now_init() != ESP_OK) {
    Serial.println("Could not initialize ESP-NOW.");
    return;
  }

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, carMac, sizeof(carMac));
  peer.channel = radioChannel;
  peer.ifidx = WIFI_IF_STA;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Could not register the car as an ESP-NOW peer.");
    return;
  }

  if (!Wire.begin(sdaPin, sclPin, 400000)) {
    Serial.println("Could not initialize I2C.");
    return;
  }
  mpu.initialize();
  if (!mpu.testConnection() || mpu.dmpInitialize() != 0) {
    Serial.println("MPU6050 or DMP unavailable.");
    return;
  }
  Serial.println("Keep the glove still and level during calibration.");
  mpu.CalibrateAccel(6);
  mpu.CalibrateGyro(6);
  mpu.setDMPEnabled(true);
  ready = true;
  Serial.println("Glove ready.");
}

void loop() {
  if (!ready) {
    delay(20);
    return;
  }

  const uint32_t now = millis();
  const bool buttonDown = digitalRead(driveButtonPin) == LOW;
  if (buttonDown != buttonWasDown) {
    buttonWasDown = buttonDown;
    if (buttonDown) {
      pressedAt = now;
      mpu.resetFIFO();
    } else {
      haveNeutral = false;
      digitalWrite(ledPin, LOW);
      sendMotion(Motion::Stop, now);
    }
  }

  if (now - lastSentAt >= sendIntervalMs) {
    Motion motion = Motion::Stop;
    if (buttonDown && now - pressedAt >= pressDebounceMs) {
      Orientation current;
      if (readOrientation(current)) {
        if (!haveNeutral) {
          neutral = current;
          haveNeutral = true;
          digitalWrite(ledPin, HIGH);
        } else {
          motion = motionFromOrientation(current);
        }
      }
    }
    sendMotion(motion, now);
  }
  delay(1);
}
