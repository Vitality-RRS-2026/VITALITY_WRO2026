/* ============================================================================
   Motor test bench - ESP32 + TB6612FNG + GA25-370 with encoder
   ----------------------------------------------------------------------------
   Upload this INSTEAD of the main sketch to characterise your drivetrain.
   Everything is driven from the Serial Monitor at 115200 baud.

   What you can find out:
     - ticks per output-shaft revolution   (command: c)
     - the PWM deadband, below which the motor will not turn   (command: d)
     - the full PWM-to-speed curve as CSV                      (command: r)
     - whether the motor spins the right way                   (command: f / b)

   SAFETY
     Put the robot ON BLOCKS with the wheels free for commands c, d and f/b.
     Only the 'r' ramp is worth running on the ground, and only with space
     ahead of it. The motor stops automatically after 10s with no command.
   ============================================================================ */

#include <Arduino.h>

/* --- pin map, matches the main sketch ------------------------------------ */
const int PIN_PWMA  = 25;
const int PIN_AIN1  = 26;
const int PIN_AIN2  = 27;
const int PIN_ENC_A = 18;
const int PIN_ENC_B = 19;

/* --- configuration ------------------------------------------------------- */
const int   PWM_FREQ    = 20000;
const int   PWM_RES     = 8;
const int   PWM_CHANNEL = 0;

// Best guess until you measure it with 'c'. 11 PPR * 4 edges * 30:1 gearbox.
// We count ONE edge only, so this is 11 * 30 = 330 for single-edge counting.
float ticksPerRev = 330.0f;

// REAR wheel diameter. Your front wheels are 44mm; check the rears.
float wheelDiaMM = 44.0f;

const unsigned long IDLE_TIMEOUT_MS = 10000;

/* --- state --------------------------------------------------------------- */
volatile long ticks = 0;
int   currentPwm = 0;
unsigned long lastCmdMs = 0;

void IRAM_ATTR encISR() {
  if (digitalRead(PIN_ENC_B)) ticks++;
  else                        ticks--;
}

long readTicks() {
  long t;
  noInterrupts();
  t = ticks;
  interrupts();
  return t;
}

void resetTicks() {
  noInterrupts();
  ticks = 0;
  interrupts();
}

/* --- motor --------------------------------------------------------------- */
void motorWrite(int speed) {
  speed = constrain(speed, -255, 255);
  currentPwm = speed;
  if (speed > 0)      { digitalWrite(PIN_AIN1, HIGH); digitalWrite(PIN_AIN2, LOW);  }
  else if (speed < 0) { digitalWrite(PIN_AIN1, LOW);  digitalWrite(PIN_AIN2, HIGH); }
  else                { digitalWrite(PIN_AIN1, LOW);  digitalWrite(PIN_AIN2, LOW);  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWMA, abs(speed));
#else
  ledcWrite(PWM_CHANNEL, abs(speed));
#endif
}

/* --- measurement ---------------------------------------------------------
   Returns output-shaft RPM measured over windowMs.
   ------------------------------------------------------------------------- */
float measureRPM(unsigned long windowMs) {
  resetTicks();
  delay(windowMs);
  long t = readTicks();
  float revs = fabs((float)t) / ticksPerRev;
  return revs * (60000.0f / (float)windowMs);
}

float rpmToMMPerSec(float rpm) {
  return (rpm / 60.0f) * PI * wheelDiaMM;
}

/* --- commands ------------------------------------------------------------ */
void printHelp() {
  Serial.println(F("\n================ MOTOR TEST BENCH ================"));
  Serial.println(F(" 0-255  set PWM forward, e.g. type 120 then Enter"));
  Serial.println(F(" -120   negative value runs the motor in reverse"));
  Serial.println(F(" s      stop"));
  Serial.println(F(" f      forward 3s at PWM 120, check direction"));
  Serial.println(F(" b      backward 3s at PWM 120"));
  Serial.println(F(" m      measure RPM at the CURRENT pwm setting"));
  Serial.println(F(" d      find the PWM deadband (wheels off the ground)"));
  Serial.println(F(" r      full ramp test, prints CSV"));
  Serial.println(F(" c      calibrate ticks per revolution"));
  Serial.println(F(" t      show current tick count"));
  Serial.println(F(" h      this help"));
  Serial.print  (F(" ticksPerRev=")); Serial.print(ticksPerRev, 1);
  Serial.print  (F("  wheelDia=")); Serial.print(wheelDiaMM, 1);
  Serial.println(F("mm"));
  Serial.println(F("=================================================\n"));
}

void deadbandTest() {
  Serial.println(F("\n--- DEADBAND TEST (wheels must be free to spin) ---"));
  Serial.println(F("Raising PWM until the shaft actually moves..."));
  for (int p = 5; p <= 150; p += 5) {
    motorWrite(p);
    delay(400);
    resetTicks();
    delay(300);
    long t = labs(readTicks());
    Serial.print(F("  pwm ")); Serial.print(p);
    Serial.print(F(" -> ticks ")); Serial.println(t);
    if (t > 5) {
      motorWrite(0);
      Serial.print(F("\nDEADBAND: motor starts moving at about PWM "));
      Serial.println(p);
      Serial.println(F("Use this as your minimum drive value. Commanding"));
      Serial.println(F("anything below it just stalls and heats the driver.\n"));
      return;
    }
  }
  motorWrite(0);
  Serial.println(F("\nNo movement up to PWM 150. Check VM voltage, STBY,"));
  Serial.println(F("motor wiring to AO1/AO2, and that VCC is on 3.3V.\n"));
}

void rampTest() {
  Serial.println(F("\n--- RAMP TEST ---"));
  Serial.println(F("Paste the CSV below into a spreadsheet to see the curve.\n"));
  Serial.println(F("pwm,rpm,mm_per_sec"));
  for (int p = 0; p <= 255; p += 15) {
    motorWrite(p);
    delay(600);                      // let it settle before measuring
    float rpm = measureRPM(500);
    Serial.print(p);       Serial.print(',');
    Serial.print(rpm, 1);  Serial.print(',');
    Serial.println(rpmToMMPerSec(rpm), 1);
  }
  motorWrite(0);
  Serial.println(F("\nRamp complete, motor stopped.\n"));
}

void calibrateTicks() {
  Serial.println(F("\n--- TICKS PER REVOLUTION ---"));
  Serial.println(F("Motor is off. Turn the WHEEL by hand exactly 10 full"));
  Serial.println(F("turns, as accurately as you can, then send any key."));
  motorWrite(0);
  resetTicks();
  while (!Serial.available()) delay(50);
  while (Serial.available()) Serial.read();
  long t = labs(readTicks());
  if (t < 50) {
    Serial.println(F("Almost no ticks counted. Encoder A/B may not be"));
    Serial.println(F("wired, or encoder VCC is not powered (3.3V)."));
    return;
  }
  float tpr = (float)t / 10.0f;
  Serial.print(F("Counted ")); Serial.print(t);
  Serial.print(F(" ticks over 10 revs -> ")); Serial.print(tpr, 1);
  Serial.println(F(" ticks/rev"));
  ticksPerRev = tpr;
  Serial.println(F("Value applied for this session. Copy it into BOTH this"));
  Serial.println(F("file and ENCODER_TICKS_PER_REV in the main sketch.\n"));
}

/* --- setup / loop -------------------------------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PWMA, PWM_FREQ, PWM_RES);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_PWMA, PWM_CHANNEL);
#endif
  motorWrite(0);

  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, RISING);

  lastCmdMs = millis();
  printHelp();
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    lastCmdMs = millis();

    if (line.length() == 0) return;

    char c0 = line.charAt(0);

    if (c0 == 'h')       printHelp();
    else if (c0 == 's')  { motorWrite(0); Serial.println(F("stopped")); }
    else if (c0 == 'd')  deadbandTest();
    else if (c0 == 'r')  rampTest();
    else if (c0 == 'c')  calibrateTicks();
    else if (c0 == 't')  { Serial.print(F("ticks=")); Serial.println(readTicks()); }
    else if (c0 == 'f') {
      Serial.println(F("forward 3s at pwm 120..."));
      resetTicks(); motorWrite(120); delay(3000);
      long t = readTicks(); motorWrite(0);
      Serial.print(F("ticks=")); Serial.println(t);
      Serial.println(t > 0 ? F("Tick count POSITIVE - wiring consistent.")
                           : F("Tick count NEGATIVE - swap encoder A and B,"
                               " or swap AO1/AO2, so forward reads positive."));
    }
    else if (c0 == 'b') {
      Serial.println(F("backward 3s at pwm 120..."));
      resetTicks(); motorWrite(-120); delay(3000);
      long t = readTicks(); motorWrite(0);
      Serial.print(F("ticks=")); Serial.println(t);
    }
    else if (c0 == 'm') {
      Serial.print(F("measuring at pwm ")); Serial.println(currentPwm);
      float rpm = measureRPM(1000);
      Serial.print(F("  rpm=")); Serial.print(rpm, 1);
      Serial.print(F("  speed=")); Serial.print(rpmToMMPerSec(rpm), 1);
      Serial.println(F(" mm/s"));
    }
    else {
      int v = line.toInt();
      if (v != 0 || c0 == '0') {
        motorWrite(v);
        Serial.print(F("pwm set to ")); Serial.println(v);
      } else {
        Serial.println(F("unrecognised - send h for help"));
      }
    }
  }

  // Safety: cut the motor if nothing has been sent for a while
  if (currentPwm != 0 && millis() - lastCmdMs > IDLE_TIMEOUT_MS) {
    motorWrite(0);
    Serial.println(F("idle timeout - motor stopped"));
  }
}

/* ============================================================================
   SUGGESTED ORDER

   1. 'c'  on blocks. Turn the wheel 10 revs by hand. Gives ticksPerRev.
           Everything else depends on this being right.
   2. 'f'  confirms the motor drives forward and ticks count positive.
           If it runs backwards, swap AO1 and AO2.
   3. 'd'  finds the lowest PWM that actually moves the shaft. Your main
           sketch should never command below this.
   4. 'r'  on the ground with clear space. Gives the PWM-to-speed curve.
           Pick DRIVE_SPEED from this, not by guessing.

   The curve from 'r' is usually far from a straight line, especially at the
   low end. That matters when you add closed-loop speed control later -- the
   same PWM step produces very different speed changes at 60 than at 200.
   ============================================================================ */
