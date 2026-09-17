/* Ultrasonic + steering direction test
   ------------------------------------
   Prints both sensor distances 5x per second, and every 3 seconds moves
   the servo to a known position so you can confirm which way is which.

   WHAT TO CHECK

   1. SENSORS
      Put your hand ~20cm from the LEFT sensor  -> L should drop to ~20
      Put your hand ~20cm from the RIGHT sensor -> R should drop to ~20
      If one stays at 200 no matter what, that sensor is not working.
      If covering the LEFT changes R, your wiring is swapped.

   2. STEERING
      Watch the wheels during each servo step. "STEER RIGHT" must turn the
      wheels to the robot's right when viewed from behind, looking forward.
      If they go the wrong way, set STEER_INVERT to 1 in open_challenge.ino
                                                                          */

#include <ESP32Servo.h>

const int PIN_SERVO  = 14;
const int PIN_TRIG_L = 32;
const int PIN_ECHO_L = 34;
const int PIN_TRIG_R = 33;
const int PIN_ECHO_R = 35;

const int SERVO_CENTER = 90;
const int STEER_TEST   = 50;

Servo steer;
unsigned long lastStep = 0;
int step_i = 0;

float ping(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(3);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, 12000);
  if (us == 0) return 200.0;          // nothing in range, or sensor silent
  float cm = us / 58.0f;
  return (cm > 200.0f) ? 200.0f : cm;
}

void setup() {
  Serial.begin(115200);
  delay(400);

  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  steer.setPeriodHertz(50);
  steer.attach(PIN_SERVO, 500, 2400);
  steer.write(SERVO_CENTER);

  pinMode(PIN_TRIG_L, OUTPUT); pinMode(PIN_ECHO_L, INPUT);
  pinMode(PIN_TRIG_R, OUTPUT); pinMode(PIN_ECHO_R, INPUT);

  Serial.println(F("\nsensor + steering test"));
  Serial.println(F("200 means nothing detected OR sensor not responding\n"));
}

void loop() {
  // Alternate sensors - firing both at once causes crosstalk
  float l = ping(PIN_TRIG_L, PIN_ECHO_L);
  delay(30);
  float r = ping(PIN_TRIG_R, PIN_ECHO_R);

  Serial.print(F("L=")); Serial.print(l, 0);
  if (l >= 199) Serial.print(F(" (nothing/dead)"));
  Serial.print(F("   R=")); Serial.print(r, 0);
  if (r >= 199) Serial.print(F(" (nothing/dead)"));
  Serial.println();

  // Step the servo every 3 seconds
  if (millis() - lastStep > 3000) {
    lastStep = millis();
    step_i = (step_i + 1) % 3;
    if (step_i == 0) {
      steer.write(SERVO_CENTER);
      Serial.println(F(">>> STEER CENTRE"));
    } else if (step_i == 1) {
      steer.write(SERVO_CENTER + STEER_TEST);
      Serial.println(F(">>> STEER RIGHT  (wheels should point robot's RIGHT)"));
    } else {
      steer.write(SERVO_CENTER - STEER_TEST);
      Serial.println(F(">>> STEER LEFT   (wheels should point robot's LEFT)"));
    }
  }

  delay(170);
}
