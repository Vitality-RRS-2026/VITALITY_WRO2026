/* Slow servo sweep test - ESP32 + MG90S
   -------------------------------------
   Sweeps slowly left and right, printing the angle as it goes.
   Watch the serial monitor AND listen to the servo.

   WHAT YOU ARE LOOKING FOR:
     The angle where the servo starts to buzz or strain, or where the
     wheels stop moving but the servo keeps pushing. That is a
     mechanical limit. A servo held against a limit draws close to an
     amp continuously, which is what collapses your power rail.

     Note the angle it happens at, then set your STEER_LOCK a few
     degrees inside it.

   RUN THIS WITH THE MOTOR UNPLUGGED, as you have been doing.
   Start with a NARROW range and widen it once you know it is safe. */

#include <ESP32Servo.h>

const int PIN_SERVO = 14;

const int CENTER    = 90;    // your straight-ahead value
const int SWEEP     = 55;    // degrees either side. START SMALL. Raise to 20, 25...
const int STEP_MS   = 1;    // ms per degree. Higher = slower sweep.

Servo steer;

void setup() {
  Serial.begin(115200);
  delay(300);

  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  steer.setPeriodHertz(50);
  steer.attach(PIN_SERVO, 500, 2400);

  steer.write(CENTER);
  Serial.println(F("centering..."));
  delay(1000);
  Serial.print(F("sweeping "));
  Serial.print(CENTER - SWEEP);
  Serial.print(F(" to "));
  Serial.println(CENTER + SWEEP);
}

void loop() {
  steer.write(CENTER + SWEEP);
  delay(1500);

  steer.write(CENTER - SWEEP);
  delay(1500);
}
