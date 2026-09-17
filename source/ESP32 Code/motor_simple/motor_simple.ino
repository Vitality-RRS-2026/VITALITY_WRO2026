#include <ESP32Servo.h>
/* Max speed test - ESP32 + TB6612FNG + GA25-370
   Runs the motor flat out. Change SPEED to try other values,
   then re-upload.

   PUT THE ROBOT ON BLOCKS or give it clear floor space first.  */

const int PIN_PWMA = 25;
const int PIN_AIN1 = 26;
const int PIN_AIN2 = 27;

const int SPEED = 240; 

const int PIN_SERVO = 14;
const int ANGLE = 20;

Servo steer;

void setup() {
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);

  steer.attach(PIN_SERVO, 500, 2400);
  steer.write(ANGLE);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PWMA, 20000, 8);
  digitalWrite(PIN_AIN1, SPEED >= 0 ? HIGH : LOW);
  digitalWrite(PIN_AIN2, SPEED >= 0 ? LOW  : HIGH);
  ledcWrite(PIN_PWMA, abs(SPEED));
#else
  ledcSetup(0, 20000, 8);
  ledcAttachPin(PIN_PWMA, 0);
  digitalWrite(PIN_AIN1, SPEED >= 0 ? HIGH : LOW);
  digitalWrite(PIN_AIN2, SPEED >= 0 ? LOW  : HIGH);
  ledcWrite(0, abs(SPEED));
#endif
}

void loop() {
}
