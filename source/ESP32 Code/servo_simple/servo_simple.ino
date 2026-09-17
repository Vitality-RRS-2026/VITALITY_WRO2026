/* Simple servo test - ESP32 + MG90S on Ackermann steering
   Holds the servo at one fixed angle. Change ANGLE and re-upload.

   Needs the ESP32Servo library:
     Library Manager -> search "ESP32Servo" -> install

   ANGLE 90 should be wheels dead ahead. Use this to find your real
   centre value, then put that number into the main sketch.

   START AT 90. Going straight to an extreme can jam the linkage
   against the chassis and strip the servo gears.                 */

#include <ESP32Servo.h>

const int PIN_SERVO = 14;
const int ANGLE = 90;        // 0-180. Try 75, 90, 105 to see the range.

Servo steer;

void setup() {
  ESP32PWM::allocateTimer(0);
  steer.setPeriodHertz(50);
  steer.attach(PIN_SERVO, 500, 2400);
  steer.write(ANGLE);
}

void loop() {
}
