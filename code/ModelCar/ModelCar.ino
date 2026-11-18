#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

constexpr uint8_t radioChannel = 1;
constexpr uint32_t signalTimeoutMs = 250;
constexpr uint32_t pwmFrequency = 1000;
constexpr uint8_t pwmResolution = 8;
constexpr uint8_t motorDuty = 150;
constexpr uint8_t gloveMac[6] = {0, 0, 0, 0, 0, 0};  // Enter the glove's STA MAC address

// Order: front left, rear left, front right, rear right.
// Each pair lists the forward input followed by the reverse input.
constexpr uint8_t motorPins[][2] = {{19, 18}, {21, 23}, {26, 27}, {33, 25}};
constexpr size_t motorCount = sizeof(motorPins) / sizeof(motorPins[0]);

// ESP-NOW payload: version bits 0x10, motion command in the low four bits.
enum class Motion : uint8_t {
  Stop = 0, Forward, Backward, Left, Right, TurnLeft, TurnRight
};
constexpr uint8_t protocolVersion = 0x10;

constexpr int8_t wheelDirections[][motorCount] = {
  { 0,  0,  0,  0},  // Stop
  { 1,  1,  1,  1},  // Forward
  {-1, -1, -1, -1},  // Reverse
  {-1,  1,  1, -1},  // Strafe left
  { 1, -1, -1,  1},  // Strafe right
  {-1, -1,  1,  1},  // Rotate left
  { 1,  1, -1, -1},  // Rotate right
};

struct ReceivedMotion {
  Motion motion;
  uint32_t receivedAt;
};

QueueHandle_t motionQueue = nullptr;
bool ready = false;
Motion appliedMotion = Motion::Stop;
ReceivedMotion lastCommand{Motion::Stop, 0};

void applyMotion(Motion motion) {
  // Set both H-bridge inputs LOW before applying a new direction.
  for (const auto &pins : motorPins) {
    ledcWrite(pins[0], 0);
    ledcWrite(pins[1], 0);
  }
  const auto &directions = wheelDirections[static_cast<uint8_t>(motion)];
  for (size_t wheel = 0; wheel < motorCount; ++wheel) {
    const int8_t direction = directions[wheel];
    if (direction != 0) {
      ledcWrite(motorPins[wheel][direction < 0], motorDuty);
    }
  }
}

void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (!info || !data || length != 1 || memcmp(info->src_addr, gloveMac, sizeof(gloveMac)) != 0) {
    return;
  }
  const uint8_t command = data[0] & 0x0F;
  if ((data[0] & 0xF0) != protocolVersion || command > static_cast<uint8_t>(Motion::TurnRight)) {
    return;
  }
  const ReceivedMotion received{static_cast<Motion>(command), millis()};
  xQueueOverwrite(motionQueue, &received);
}

void setup() {
  for (const auto &pins : motorPins) {
    for (uint8_t pin : pins) {
      pinMode(pin, OUTPUT);
      digitalWrite(pin, LOW);
    }
  }
  Serial.begin(115200);
  for (const auto &pins : motorPins) {
    for (uint8_t pin : pins) {
      if (!ledcAttach(pin, pwmFrequency, pwmResolution)) {
        Serial.printf("Could not configure PWM on GPIO %u.\n", pin);
        return;
      }
      ledcWrite(pin, 0);
    }
  }

  const uint8_t unsetMac[6] = {};
  if (memcmp(gloveMac, unsetMac, sizeof(gloveMac)) == 0) {
    Serial.println("Set gloveMac to the glove's STA MAC address.");
    return;
  }
  motionQueue = xQueueCreate(1, sizeof(ReceivedMotion));
  if (!motionQueue || !WiFi.mode(WIFI_STA) ||
      esp_wifi_set_channel(radioChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
      esp_now_init() != ESP_OK || esp_now_register_recv_cb(onDataReceived) != ESP_OK) {
    Serial.println("Could not start the ESP-NOW receiver.");
    return;
  }
  ready = true;
  Serial.println("Model car is waiting for the glove.");
}

void loop() {
  if (!ready) {
    delay(20);
    return;
  }
  xQueueReceive(motionQueue, &lastCommand, 0);
  const Motion requested = millis() - lastCommand.receivedAt < signalTimeoutMs
                               ? lastCommand.motion
                               : Motion::Stop;
  if (requested != appliedMotion) {
    applyMotion(requested);
    appliedMotion = requested;
  }
  delay(1);
}
