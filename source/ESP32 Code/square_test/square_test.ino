/* ============================================================================
   Square driving test - ESP32 + TB6612FNG + MG90S servo
   ----------------------------------------------------------------------------
   Drives a square: straight for 1s, turn, repeat 4 times.
   Motor and servo only. No gyro, no encoder.

   Because there is no sensor telling the robot how far it has turned,
   the corner is done on a TIMER. You have to tune TURN_MS by hand until
   the corner comes out at roughly 90 degrees. Start at 900 and adjust.

   GIVE IT ABOUT 1.5 x 1.5 METRES OF CLEAR FLOOR.
   ============================================================================ */

#include <ESP32Servo.h>

/* --- pins --------------------------------------------------------------- */
const int PIN_PWMA  = 25;
const int PIN_AIN1  = 26;
const int PIN_AIN2  = 27;
const int PIN_SERVO = 14;

/* --- tuning ------------------------------------------------------------- */
const int SPEED_STRAIGHT = 150;   // half of 255
const int SPEED_TURN     = 100;    // quarter of 255

const int SERVO_CENTER = 90;      // YOUR measured straight-ahead value
const int STEER_LOCK   = 45;      // degrees off centre during a turn
const int TURN_DIRECTION = +1;    // +1 right, -1 left

const unsigned long STRAIGHT_MS = 1000;
const unsigned long TURN_MS     = 900;   // TUNE THIS until the corner is 90deg

const int NUM_CORNERS = 4;

/* --- globals ------------------------------------------------------------ */
Servo steer;

void motor(int s) {
  s = constrain(s, -255, 255);
  digitalWrite(PIN_AIN1, s > 0 ? HIGH : LOW);
  digitalWrite(PIN_AIN2, s < 0 ? HIGH : LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWMA, abs(s));
#else
  ledcWrite(0, abs(s));
#endif
}

void steerTo(int deviation) {
  steer.write(SERVO_CENTER + deviation);
}

/* Ramps the motor up instead of slamming it to full PWM.

   A stopped motor looks almost like a short circuit for the first few
   milliseconds - it can pull several times its running current. That spike
   drags the supply rail down, and if it dips below about 2.8V the ESP32's
   brownout detector resets the board. The symptom is the sketch restarting
   the moment the motor engages.

   Ramping over ~250ms keeps the peak current far lower. It costs a
   fraction of a second and usually removes the problem entirely.        */
void motorSoftStart(int target) {
  const int STEP = 15;
  const int STEP_MS = 15;
  int current = 0;
  while (abs(current) < abs(target)) {
    current += (target > 0 ? STEP : -STEP);
    if (abs(current) > abs(target)) current = target;
    motor(current);
    delay(STEP_MS);
  }
  motor(target);
}

/* --- setup -------------------------------------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(500);
  delay(3000);

  // SERVO FIRST. ESP32Servo and the motor PWM share the ESP32's LEDC
  // timers. If the motor claims a timer first, the servo silently fails
  // to attach and never moves. Giving the servo timers 1-3 and leaving
  // timer 0 for the motor keeps them out of each other's way.
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  steer.setPeriodHertz(50);
  steer.attach(PIN_SERVO, 500, 2400);
  steerTo(0);
  delay(300);

  // Visible proof the servo is alive before anything else happens.
  Serial.println(F("servo check: left, right, centre"));
  steerTo(-STEER_LOCK); delay(600);
  steerTo(+STEER_LOCK); delay(600);
  steerTo(0);           delay(600);

  // Motor PWM second, on LEDC channel 0.
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PWMA, 20000, 8);
#else
  ledcSetup(0, 20000, 8);
  ledcAttachPin(PIN_PWMA, 0);
#endif
  motor(0);

  // The servo sweep above draws a burst of current. Let the rail settle
  // before asking the motor for anything.
  Serial.println(F("Starting in 3 seconds - clear the floor"));
  delay(3000);

  for (int i = 0; i < NUM_CORNERS; i++) {
    Serial.print(F("side ")); Serial.println(i + 1);
    steerTo(0);
    motorSoftStart(SPEED_STRAIGHT);
    delay(STRAIGHT_MS);

    Serial.println(F("  turning"));
    steerTo(TURN_DIRECTION * STEER_LOCK);
    delay(150);                    // let the servo reach lock first
    motorSoftStart(SPEED_TURN);
    delay(TURN_MS);
  }

  motor(0);
  steerTo(0);
  Serial.println(F("done"));
}

void loop() {
}
