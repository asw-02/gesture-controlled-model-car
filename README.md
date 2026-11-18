# Gesture-Controlled Model Car

An ESP32 on the glove reads hand orientation from an MPU6050 and sends driving commands over ESP-NOW to a second ESP32 in the car. Four independently driven Mecanum wheels let the car move forward, backward and sideways, or rotate in place. The two boards communicate directly without a Wi-Fi router.

## Parts

| Assembly | Components |
| --- | --- |
| Glove | 1 ESP32-WROOM-32 development board, 1 MPU6050 breakout, 1 normally open pushbutton, 1 red LED with a series resistor (e.g. 330 Ω), a glove, a USB cable and a USB power bank |
| Model car | 1 ESP32-WROOM-32 development board, 2 L298N dual H-bridge modules, 4 compatible DC gear motors, 4 Mecanum wheels (two of each roller orientation), a chassis and mounting hardware |
| Power and wiring | A suitable motor battery (7.4 V nominal in the original build), a main power switch, a regulated 5 V supply for the car's ESP32, hookup wire and a USB data cable for flashing |

## Wiring diagrams

- [Glove wiring diagram (original PDF)](wiring-diagrams/glove-wiring.pdf)
- [Model car wiring diagram (original PDF)](wiring-diagrams/model-car-wiring.pdf)

The PDFs show the original build and retain their German labels. Use the pin assignments below with this firmware. Leave out the glove's mode button on GPIO 18 and the three ultrasonic sensors from the original build. If converting an existing car, disconnect the ultrasonic sensor from GPIO 23 before moving the rear-left motor control wire from GPIO 3 (RX) to GPIO 23.

### Glove

| ESP32 | Connection |
| --- | --- |
| GPIO 21 | MPU6050 SDA |
| GPIO 22 | MPU6050 SCL |
| GPIO 4 | Drive-enable pushbutton to GND. The firmware enables the internal pull-up. |
| GPIO 19 | LED anode. Connect the cathode to GND through the series resistor. |
| 3V3 and GND | Power a 3.3 V-compatible MPU6050 breakout. Pull SDA and SCL up to 3.3 V. |
| USB | Power bank |

An existing button module can be reused if it pulls the input LOW when pressed and its output stays within 3.3 V logic levels. The MPU6050 uses its default I²C address, `0x68`, with AD0 LOW.

### Model car

Use one L298N module for the two left motors and one for the two right motors. Wheel positions are given from the car's perspective when facing forward.

| Motor | Forward GPIO | Reverse GPIO |
| --- | --- | --- |
| Front left | 19 | 18 |
| Rear left | 21 | **23** |
| Front right | 26 | 27 |
| Rear right | 33 | 25 |

1. Connect each GPIO pair to the two inputs of one L298N channel and the motor to that channel's output pair. Leave the encoder connections open. Enable both channels with ENA and ENB. The firmware applies PWM to the direction inputs.
2. Connect the motor battery to both drivers' motor supply through the main switch. Join battery negative, both driver grounds and ESP32 ground. Follow the terminal and jumper markings on your L298N modules for the 5 V logic supply.
3. Power the car's ESP32 from regulated 5 V. Disconnect that supply before powering the board over USB.
4. Mount the Mecanum wheels so the roller axes form an X when viewed from above. Raise the chassis for the first test so all four wheels can turn freely. If a motor spins backwards during a forward command, swap its two wires at the driver output.

## Install the firmware

1. Install Arduino IDE 2. Open **File → Preferences → Additional Boards Manager URLs** and add the stable Espressif package index.

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

2. In Boards Manager, install **esp32 by Espressif Systems** version **3.x**. Select **ESP32 Dev Module** for a generic ESP32-WROOM-32 board. [Espressif installation guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
3. For the glove, copy `Arduino/I2Cdev` and `Arduino/MPU6050` from [jrowberg/i2cdevlib](https://github.com/jrowberg/i2cdevlib) into the Arduino sketchbook's `libraries` directory. The sketch uses `MPU6050_6Axis_MotionApps20.h`.
4. Upload [MacAddress.ino](code/MacAddress/MacAddress.ino) to each ESP32. Open Serial Monitor at **115200 baud** and record each board's STA MAC address as the printed byte array. Keep track of which board goes in the glove and which goes in the car.
5. In [Glove.ino](code/Glove/Glove.ino), set `carMac` to the car's MAC address. In [ModelCar.ino](code/ModelCar/ModelCar.ino), set `gloveMac` to the glove's address. Upload each sketch to its board. Both boards use ESP-NOW on channel 1.
6. Keep the glove still and level during startup until Serial Monitor shows **Glove ready.** Test all six motions and the stop behavior with the wheels off the ground.

## Drive the car

Hold the glove in a comfortable neutral position, then press and hold the drive-enable button. The LED lights up when the glove captures the neutral position. The gestures below are relative to that position and assume the same sensor orientation as the original build.

| Gesture | Movement |
| --- | --- |
| Tilt your hand forward or back (pitch) | Drive forward or backward |
| Rotate your hand left or right while keeping it level (yaw) | Rotate in place |
| Tilt your hand to the left or right (roll) | Move sideways |
| Return to neutral or release the button | Stop driving |

When gestures overlap, pitch takes priority over yaw, then roll. To recenter, release the button and press it again in the neutral position. Adjust gesture thresholds in `motionFromOrientation()` in the glove sketch and the PWM duty in `motorDuty` in the car sketch.

Releasing the button sends a stop command immediately. If the car receives no valid command for 250 ms, it sets all motor outputs to zero and the wheels coast to a stop.
