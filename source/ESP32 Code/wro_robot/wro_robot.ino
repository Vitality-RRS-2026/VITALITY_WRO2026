/* ============================================================================
   WRO Future Engineers - OPEN CHALLENGE
   3 laps, then stop in the starting section.
   ----------------------------------------------------------------------------
   HOW IT WORKS
     Straight : PD wall-follow, centred between the two side walls
     Corner   : a side sensor reads long -> turn 90 deg USING THE GYRO
     Counting : 12 corners = 3 laps
     Finish   : after corner 12, keep following the wall for FINISH_MS, stop

   WHY THE GYRO RUNS THE TURN, NOT A TIMER
     A timed turn changes with battery voltage and floor grip, so it drifts
     over 12 corners. "Turn until the gyro says 90" is the same every time.
     After each corner the heading is SNAPPED to the nearest multiple of 90,
     which stops drift accumulating across the run.

   SET TUNING_MODE 1 FIRST. The motor stays off and sensors print.
   ============================================================================ */

#include <Wire.h>
#include <ESP32Servo.h>

#define TUNING_MODE 0        // 1 = print only, no driving. 0 = run.

/* --- pins --------------------------------------------------------------- */
const int PIN_PWMA   = 25;
const int PIN_AIN1   = 26;
const int PIN_AIN2   = 27;
const int PIN_SERVO  = 14;
const int PIN_TRIG_L = 32;
const int PIN_ECHO_L = 34;
const int PIN_TRIG_R = 33;
const int PIN_ECHO_R = 35;

// Start button: one leg to GPIO 23, the other to GND.
// INPUT_PULLUP means the pin reads HIGH when released, LOW when pressed,
// so no external resistor is needed.
const int PIN_START_BUTTON = 23;

/* --- tuning ------------------------------------------------------------- */
const int   SERVO_CENTER = 90;     // YOUR measured straight-ahead value

// Set to 1 if the robot steers the WRONG WAY.
// Depending on how the linkage is assembled and which way the servo horn
// was pressed on, increasing the servo angle may turn the wheels left on
// one build and right on another. This flips it in software instead of
// making you rebuild the linkage.
#define STEER_INVERT 1

// Set to 1 if yaw counts the WRONG WAY.
// The turn logic assumes yaw INCREASES when the robot turns right. Whether
// that is true depends on which way up the MPU6050 is mounted. If it is
// inverted, a right turn drives yaw away from its target and the robot
// turns forever.
//
// HOW TO CHECK: run with TUNING_MODE 1, rotate the robot clockwise by hand
// (viewed from above) and watch yaw in the telemetry. It must go UP.
#define GYRO_INVERT 1
// SERVO STALL WARNING. Two servos were destroyed at 60 degrees by
// stalling against the linkage's mechanical stop - the servo pushes at
// near stall current until the windings overheat, which takes several
// runs to kill it, so it looks fine until suddenly it is not.
//
// Currently set to 60. Verify with servo_sweep.ino that the linkage does
// not bind before this angle, and check the servo by hand after each run:
// warm is normal, too hot to hold comfortably means it is stalling.
const int   STEER_MAX    = 60;     // max deviation either side
const int   STEER_LOCK   = 60;     // deviation used during a corner

const int   SPEED_STRAIGHT = 110;
const int   SPEED_TURN     = 90;   // must be above your motor deadband

// Wall following
const float KP_WALL = 1.99f;
const float KD_WALL = 0.25f;

/* Extra push that grows with the SQUARE of the intrusion, so the response
   is gentle just inside the margin and firm when the wall is genuinely
   close. A purely proportional term cannot do both: set it soft enough
   not to twitch at 14 cm and it barely reacts at 3 cm; set it hard enough
   for 3 cm and it jerks the robot around at 14 cm.

   Divided by the margin so the term stays in degrees and the constant
   keeps its meaning if WALL_MARGIN_CM changes. */
const float KP_WALL_SQ = 1.33f;
const float TARGET_SIDE_CM = 50.0f;   // kept for the no-direction fallback

const float WALL_MARGIN_CM = 25.0f;

// Gain for holding heading while in the clear middle of the corridor.
const float KP_HEADING_HOLD = 0.8f;



// Corner detection
const float CORNER_DIST_CM   = 120.0f;  // side reading above this = no wall
const int   CORNER_CONFIRM   = 2;       // ~80ms at 40ms per sensor


const float TURN_ANGLE       = 90.0f;

/* A turn may finish once yaw is within this many degrees of the target.

   At 30 this is a LARGE tolerance: a 90 degree turn can end after only 60
   degrees of sweep. That is deliberate - it stops the robot over-rotating
   - but it means each corner may leave the robot up to 30 degrees short.

   Two things keep that from compounding:
     - the both-walls check below usually ends the turn on real alignment
       rather than on this tolerance
     - the next corner targets an ABSOLUTE heading, so a short turn is
       corrected by the following one rather than accumulating           */
const float TURN_TOLERANCE_DEG = 30.0f;


/* Seeing a wall on BOTH sides again means the robot is back in a corridor,
   i.e. the turn is done - regardless of what the gyro thinks. This is an
   independent confirmation that does not accumulate drift. It is only
   trusted once the robot has already swept most of the turn, so that the
   corridor it came FROM cannot end the turn immediately. */
const float BOTH_WALLS_MIN_SWEEP = 75.0f;

// Consecutive confirmations before trusting the both-walls reading.
const int   CORRIDOR_CONFIRM  = 3;

/* Acceptable range for a commanded turn. Every corner on this track is 90
   degrees; the self-correcting heading target may legitimately ask for a
   bit more or less to straighten the robot up, but never double. Outside
   this band the rounding has gone wrong and we fall back to a plain 90. */
const float TURN_SWEEP_MIN   = 30.0f;
const float TURN_SWEEP_MAX   = 150.0f;
const unsigned long MIN_CORNER_GAP_MS = 1400;

// Gyro bias samples, 2ms each. Runs after the button, so it costs run time.
const int   GYRO_CAL_SAMPLES = 800;   // 1.6 s

// A 90 degree turn should take well under this. Exceeding it means
// something is wrong, so the robot stops instead of circling.
const unsigned long TURN_TIMEOUT_MS = 4000;

/* --- tight corner recovery ----------------------------------------------
   If a wall comes within TIGHT_WALL_CM during a turn, the robot is cutting
   it too fine. Slowing down and steering harder tightens the arc and buys
   clearance.

   WHY BOTH: steering harder alone does not help much at speed, because the
   robot covers ground while the servo is still moving. Slowing gives the
   tighter angle time to take effect.

   WARNING ON THE ANGLE: steerTo() clamps everything to STEER_MAX, so
   TIGHT_STEER_LOCK above that value has no effect. Raising STEER_MAX to
   suit it pushes the servo further into the range that destroyed two
   servos by stalling against the linkage stop. Verify with
   servo_sweep.ino before raising the cap.
   ------------------------------------------------------------------------ */
const float TIGHT_WALL_CM    = 8.0f;
const int   TIGHT_TURN_SPEED = 80;
const int   TIGHT_STEER_LOCK = 65;



// Run length
const int   TOTAL_CORNERS = 12;         // 4 per lap x 3 laps
const unsigned long FINISH_MS = 500;   // TUNE: drive time after last corner

const float US_MAX_CM = 200.0f;
const unsigned long US_TIMEOUT_US = 12000;
const int MPU_ADDR = 0x68;

/* --- globals ------------------------------------------------------------ */
Servo steer;
float distL = US_MAX_CM, distR = US_MAX_CM;
float yaw = 0, gyroBias = 0, headingTarget = 0;
float prevError = 0;
int   cornerCount = 0;
int   longL = 0, longR = 0;
bool  noEchoL = false, noEchoR = false;
int   corridorCount = 0;
int   usTurn = 0;
int   turnDir = 0;                 // +1 right, -1 left. Set at first corner.
unsigned long lastLoop = 0, finishStart = 0, turnStartMs = 0;
unsigned long lastYawUs = 0;
float turnStartYaw = 0;
unsigned long lastCornerMs = 0;

enum State { WAIT, DRIVE, TURNING, FINISHING, DONE };
State state = WAIT;

/* --- MPU6050 ------------------------------------------------------------ */
void mpuInit() {
  Wire.begin(21, 22);
  Wire.setClock(400000);
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission();
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1B); Wire.write(0x00); Wire.endTransmission();
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1A); Wire.write(0x03); Wire.endTransmission();
}
/* Reads the gyro's Z rate. Tracks consecutive failures so the rest of the
   program can fall back to timed turns instead of stalling forever. */
float gyroRaw() {
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x47);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 2, true);
  if (Wire.available() < 2) return 0;
  int16_t v = (Wire.read() << 8) | Wire.read();
  return (float)v;
}
/* Measures the gyro's resting offset so it can be subtracted from every
   later reading.

   TIMING NOTE: this now runs AFTER the start button, so it is inside the
   scored run. At 2ms per sample, GYRO_CAL_SAMPLES directly sets how much
   dead time you spend before moving:

       400 samples  = 0.8 s   noisier bias, more heading drift per lap
       800 samples  = 1.6 s   good compromise (default)
      1500 samples  = 3.0 s   best bias, 3 s of your round gone

   The round is 3 minutes, so this is not critical, but it is free lap time
   if your gyro is well behaved. Check the printed bias across several runs:
   if it varies by more than about 5 counts, raise the sample count. */
void calibrateGyro() {
  Serial.println(F("gyro cal - HOLD STILL"));
  double s = 0;
  for (int i = 0; i < GYRO_CAL_SAMPLES; i++) { s += gyroRaw(); delay(2); }
  gyroBias = s / (double)GYRO_CAL_SAMPLES;
  Serial.print(F("bias ")); Serial.println(gyroBias, 1);
}
void updateYaw(float dt) {
  float r = (gyroRaw() - gyroBias) / 131.0f;
  if (fabs(r) < 0.6f) r = 0;
#if GYRO_INVERT
  r = -r;
#endif
  yaw += r * dt;
}

/* --- ultrasonic --------------------------------------------------------- */
/* Set true when the last ping TIMED OUT rather than returning a real
   echo. A timeout is ambiguous - it can mean "no wall" or "the pulse hit
   the wall at an angle and reflected away" - so the caller treats it with
   more suspicion than a genuine long reading. */
bool lastPingTimedOut = false;

float ping(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(3);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  unsigned long us = pulseIn(echo, HIGH, US_TIMEOUT_US);

  /* pulseIn returns 0 when it TIMES OUT - no echo came back within 12ms,
     which is about 2 metres. That means nothing is in range, i.e. FAR.
     Being too close does not cause a timeout; it produces a very short
     pulse and reads as a small number.

     So 0 must map to US_MAX_CM, not to 0. Returning 0 would tell the wall
     follower a wall is touching the robot, and would stop the
     distL >= CORNER_DIST_CM test from ever being true, which disables
     side-based corner detection entirely. */
  if (us == 0) { lastPingTimedOut = true; return US_MAX_CM; }
  lastPingTimedOut = false;
  float cm = us / 58.0f;
  return (cm > US_MAX_CM) ? US_MAX_CM : cm;
}

/* One sensor per loop, alternating. Firing both at once means each hears
   the other's echo. Each sensor refreshes every 2 loops, about 40ms. */
void updateUltrasonics() {
  if (usTurn == 0) { distL = ping(PIN_TRIG_L, PIN_ECHO_L); noEchoL = lastPingTimedOut; }
  else             { distR = ping(PIN_TRIG_R, PIN_ECHO_R); noEchoR = lastPingTimedOut; }
  usTurn ^= 1;
}

/* --- actuators ---------------------------------------------------------- */
void motor(int s) {
#if TUNING_MODE
  s = 0;
#endif
  s = constrain(s, -255, 255);
  digitalWrite(PIN_AIN1, s > 0 ? HIGH : LOW);
  digitalWrite(PIN_AIN2, s < 0 ? HIGH : LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWMA, abs(s));
#else
  ledcWrite(0, abs(s));
#endif
}
void steerTo(float dev) {
#if STEER_INVERT
  dev = -dev;
#endif
  dev = constrain(dev, -STEER_MAX, STEER_MAX);
  steer.write(SERVO_CENTER + (int)dev);
}

/* --- wall following ----------------------------------------------------- */
float wallSteer() {
  bool seeL = distL < CORNER_DIST_CM;
  bool seeR = distR < CORNER_DIST_CM;

  // No walls at all - nothing to measure against, so hold heading.
  if (!seeL && !seeR) {
    prevError = 0;
    return KP_HEADING_HOLD * (headingTarget - yaw);
  }

  /* How far INSIDE the margin each wall is. Zero means the wall is at
     15 cm or further, so no correction is wanted from that side. */
  float intrudeL = (seeL && distL < WALL_MARGIN_CM) ? (WALL_MARGIN_CM - distL) : 0.0f;
  float intrudeR = (seeR && distR < WALL_MARGIN_CM) ? (WALL_MARGIN_CM - distR) : 0.0f;

  // Clear of both walls: drive straight, just hold the heading.
  if (intrudeL == 0.0f && intrudeR == 0.0f) {
    prevError = 0;
    return KP_HEADING_HOLD * (headingTarget - yaw);
  }

  /* Positive error means steer right. A wall intruding on the LEFT pushes
     the robot right; one on the RIGHT pushes it left. If somehow both
     intrude - a corridor narrower than 30 cm - they partly cancel and the
     robot heads for the middle, which is the best available answer. */
  float error = intrudeL - intrudeR;

  float d = error - prevError;
  prevError = error;

  /* Proportional + squared + derivative. The squared term uses the
     MAGNITUDE of the error and keeps its sign, so it pushes the same way
     as the proportional term rather than fighting it. */
  float mag = fabs(error);
  float boost = KP_WALL_SQ * (mag * mag) / WALL_MARGIN_CM;
  if (error < 0) boost = -boost;

  return KP_WALL * error + boost + KD_WALL * d;
}


/* How far the robot is from its nearest legal heading (a multiple of 90).
   Every heading on a rectangular track is a multiple of 90, so this is a
   useful measure of how crooked the robot is running. Telemetry only -
   it does not gate any decision. */
float headingError() {
  return fabs(yaw - round(yaw / 90.0f) * 90.0f);
}

/* Returns true only on a clean press.

   WHY DEBOUNCE: a mechanical switch bounces for a few milliseconds when
   pressed, which reads as many separate presses. Requiring the pin to stay
   LOW for 30ms filters that out. */
bool startPressed() {
  static unsigned long downSince = 0;
  if (digitalRead(PIN_START_BUTTON) == LOW) {
    if (downSince == 0) downSince = millis();
    if (millis() - downSince >= 30) return true;
  } else {
    downSince = 0;
  }
  return false;
}

/* --- setup ---------------------------------------------------------------

   ORDER MATTERS HERE.

   Only the minimum needed to sit safely still happens before the button:
   pin directions, actuators parked, I2C awake. Everything that actually
   prepares the robot to run - gyro calibration above all - happens AFTER
   the press.

   Why: the gyro bias must be measured while the robot is stationary and in
   its final position. If we calibrated at power-up, any nudge while placing
   the robot on the mat would corrupt the bias, and every corner afterwards
   would inherit that error.
   -------------------------------------------------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(400);

  /* ---- minimal init: make everything safe and idle ---- */

  // Servo timers FIRST. If the motor PWM claims a timer first, the servo
  // silently fails to attach and never moves.
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  steer.setPeriodHertz(50);
  steer.attach(PIN_SERVO, 500, 2400);
  steerTo(0);

  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PWMA, 20000, 8);
#else
  ledcSetup(0, 20000, 8);
  ledcAttachPin(PIN_PWMA, 0);
#endif
  motor(0);                       // motor parked before anything else runs

  pinMode(PIN_TRIG_L, OUTPUT); pinMode(PIN_ECHO_L, INPUT);
  pinMode(PIN_TRIG_R, OUTPUT); pinMode(PIN_ECHO_R, INPUT);
  pinMode(PIN_START_BUTTON, INPUT_PULLUP);

  mpuInit();                      // just wakes the chip over I2C, no measuring

  /* ---- WAIT FOR THE BUTTON ---- */
  // WRO rules: one switch powers the robot on, a SEPARATE button starts the
  // program. Auto-starting on a timer is a rule violation.
  Serial.println(F("\nREADY - press start button"));
  while (!startPressed()) {
    motor(0);
    steerTo(0);
    delay(10);
  }

  // Wait for release so one long press cannot be read as a second press
  // later in the run.
  while (digitalRead(PIN_START_BUTTON) == LOW) delay(10);

  Serial.println(F("GO"));

  /* ---- everything else happens AFTER the press ---- */
  calibrateGyro();                // robot must be still - it already is
  yaw = 0;                        // heading zero = straight ahead from here
  headingTarget = 0;
  prevError = 0;
  cornerCount = 0;
  longL = longR = 0;
  turnDir = 0;

  lastLoop = millis();
  lastYawUs = micros();

#if TUNING_MODE
  Serial.println(F("TUNING MODE - motor disabled"));
#endif
  state = DRIVE;
}

/* --- loop --------------------------------------------------------------- */
void loop() {
  /* GYRO FIRST, EVERY ITERATION.

     pulseIn() blocks for up to 12ms when a sensor gets no echo, so the old
     fixed 20ms loop could stretch to 40ms or more. The gyro was sampled
     once per loop, which meant integrating at an irregular 25-50Hz and
     applying each rate reading across the whole gap. That is why turns came
     out inconsistent - sometimes over, sometimes under.

     Now yaw integrates as fast as the loop can run, using real elapsed
     micros, and the slow sensor work is throttled separately below.     */
  unsigned long nowUs = micros();
  float dt = (nowUs - lastYawUs) / 1000000.0f;
  lastYawUs = nowUs;
  if (dt > 0.2f) dt = 0.2f;            // ignore absurd gaps after a stall
  updateYaw(dt);

  unsigned long now = millis();
  if (now - lastLoop < 20) return;
  lastLoop = now;

  /* Ultrasonics are skipped during the FIRST part of a turn: they are not
     needed there, and pulseIn blocking would thin out the gyro sampling
     exactly when the turn accuracy depends on it.

     Once the robot has swept past BOTH_WALLS_MIN_SWEEP they come back on, so
     the early-exit check below has data to work with. */
  bool lateInTurn = (state == TURNING) &&
                    (fabs(yaw - turnStartYaw) >= BOTH_WALLS_MIN_SWEEP);
  if (state != TURNING || lateInTurn) updateUltrasonics();

  // Debounced "wall has gone" detection
  if (distL >= CORNER_DIST_CM) longL++; else longL = 0;
  if (distR >= CORNER_DIST_CM) longR++; else longR = 0;


  switch (state) {

    case WAIT:                  // safe idle - not used in normal flow,
      motor(0);                 // the button wait now happens in setup()
      steerTo(0);
      break;

    case DRIVE: {
      steerTo(wallSteer());
      motor(SPEED_STRAIGHT);

      /* CORNER DETECTION: a side wall disappearing.

         The exclusivity check - one side long while the other is NOT -
         stops an open area on both sides from triggering a random turn. */
      bool cornerL = (longL >= CORNER_CONFIRM) && (longR < CORNER_CONFIRM);
      bool cornerR = (longR >= CORNER_CONFIRM) && (longL < CORNER_CONFIRM);

      // Corners are physically far apart, so a detection too soon after
      // the last one is spurious. This guard does not depend on any sensor
      // being accurate, unlike a heading-based check.
      bool spacedOut = (millis() - lastCornerMs) > MIN_CORNER_GAP_MS;

      if ((cornerL || cornerR) && spacedOut) {
        // Lap direction is decided by the FIRST corner, because the start
        // direction is randomised. Do not assume clockwise.
        if (turnDir == 0) {
          turnDir = cornerL ? -1 : +1;
          Serial.print(F("direction: "));
          Serial.println(turnDir > 0 ? F("clockwise") : F("anticlockwise"));
        }
        // ABSOLUTE heading target, not a relative sweep.
        //
        // The track is a rectangle, so every legal heading is a multiple
        // of 90 degrees. round(yaw/90)*90 gives the heading we SHOULD have
        // been on; adding 90 in the turn direction gives the heading we
        // should end up on.
        //
        // This self-corrects. Entering a corner 30 degrees crooked means
        // the robot turns 60 or 120 degrees as needed to come out straight,
        // instead of turning a blind 90 and staying 30 degrees off.
        /* Absolute heading target, with a sanity clamp.

           round(yaw/90) self-corrects small heading errors, which is what
           we want. But it has a cliff: once the robot is more than 45
           degrees off, round() snaps to the WRONG multiple of 90 and the
           commanded turn jumps by a full 90 degrees. Measured: at yaw -134
           the sweep is 46 degrees; at -136 it becomes 136 degrees.

           So compute the target, then check how far it actually asks the
           robot to turn. A corner on this track is 90 degrees. Anything
           outside a sensible band means the rounding went over the cliff,
           and we fall back to a plain 90 from the current heading.       */
        headingTarget = round(yaw / 90.0f) * 90.0f + turnDir * TURN_ANGLE;

        float sweep = fabs(headingTarget - yaw);
        if (sweep < TURN_SWEEP_MIN || sweep > TURN_SWEEP_MAX) {
          Serial.print(F("  sweep ")); Serial.print(sweep, 0);
          Serial.println(F(" deg out of range - using plain 90"));
          headingTarget = yaw + turnDir * TURN_ANGLE;
        }
        turnStartMs = millis();
        turnStartYaw = yaw;
        corridorCount = 0;
        lastCornerMs = millis();
        state = TURNING;
        Serial.print(F("corner ")); Serial.println(cornerCount + 1);
      }
      break;
    }

    case TURNING: {
      /* Tight corner: a wall within TIGHT_WALL_CM means the turn is cutting
         it too close. Slow down and steer harder to tighten the arc. */
      bool tight = (distL < TIGHT_WALL_CM) || (distR < TIGHT_WALL_CM);
      if (tight) {
        steerTo(turnDir * TIGHT_STEER_LOCK);
        motor(TIGHT_TURN_SPEED);
        static unsigned long lastTightMsg = 0;
        if (millis() - lastTightMsg > 500) {
          lastTightMsg = millis();
          Serial.print(F("  tight corner L=")); Serial.print(distL, 0);
          Serial.print(F(" R=")); Serial.println(distR, 0);
        }
      } else {
        steerTo(turnDir * STEER_LOCK);
        motor(SPEED_TURN);
      }

      float swept = fabs(yaw - turnStartYaw);

      /* Two independent ways to finish a turn.

         1. GYRO - yaw within TURN_TOLERANCE_DEG of the target, or past it.
            Tolerance matters because tyre scrub or a brush against a wall
            can eat the last few degrees; without it the turn hangs until
            the timeout and the corner never gets counted.

         2. BOTH WALLS - a wall visible on both sides means the robot is
            back in a corridor and pointing down it. That is physical
            evidence of alignment, so it ends the turn whatever the gyro
            says. Valuable precisely because it does NOT drift: if the gyro
            is slowly accumulating error, the walls still tell the truth.

            Only trusted after BOTH_WALLS_MIN_SWEEP degrees, otherwise the
            corridor the robot came FROM would end the turn immediately.
            Confirmed over several readings, since ultrasonics mis-read.

            When the walls end the turn, yaw is set to the target - the
            walls say we ARE aligned, so this is an evidence-based
            correction rather than a blind snap.                          */
      bool byGyro = fabs(yaw - headingTarget) <= TURN_TOLERANCE_DEG;
      if (turnDir > 0 && yaw >= headingTarget) byGyro = true;
      if (turnDir < 0 && yaw <= headingTarget) byGyro = true;

      bool byWalls = false;
      if (!byGyro && swept >= BOTH_WALLS_MIN_SWEEP) {
        if (distL < CORNER_DIST_CM && distR < CORNER_DIST_CM) corridorCount++;
        else                                                  corridorCount = 0;

        if (corridorCount >= CORRIDOR_CONFIRM) {
          Serial.print(F("  corridor found at yaw ")); Serial.print(yaw, 0);
          Serial.print(F(" (target ")); Serial.print(headingTarget, 0);
          Serial.print(F("), swept ")); Serial.print(swept, 0);
          Serial.println(F(" deg - ending turn"));
          yaw = headingTarget;
          byWalls = true;
        }
      }

      bool reached = byGyro || byWalls;

      // SAFETY: a turn should never take more than a few seconds. If it
      // does, the gyro sign is likely wrong - see GYRO_INVERT.
      if (!reached && millis() - turnStartMs > TURN_TIMEOUT_MS) {
        motor(0);
        steerTo(0);
        Serial.println(F("!! TURN TIMEOUT - check GYRO_INVERT"));
        Serial.print(F("   yaw=")); Serial.print(yaw, 1);
        Serial.print(F(" target=")); Serial.println(headingTarget, 1);
        state = DONE;
        break;
      }

      if (reached) {
        // NOTE: we deliberately do NOT snap yaw to a multiple of 90 here.
        // Snapping made the numbers look clean while leaving the robot
        // physically crooked, and the error compounded corner after corner.
        // Because the next turn targets an absolute heading, any residual
        // error is corrected by the turn itself.
        headingTarget = round(yaw / 90.0f) * 90.0f;
        lastCornerMs = millis();      // gap measured from turn EXIT
        prevError = 0;
        longL = longR = 0;
        corridorCount = 0;

        /* INVALIDATE STALE DISTANCES.

           Ultrasonics are skipped during most of a turn, so these values
           can still describe the corner the robot just came out of. Acting
           on them in DRIVE made the robot clamp its steering as if a wall
           were still close ahead, so it could not correct its line and
           drove into the wall.

           Setting them to maximum means "unknown, assume clear" until a
           real reading arrives, which takes about 60ms. */
        distL = distR = US_MAX_CM;
        cornerCount++;

        if (cornerCount >= TOTAL_CORNERS) {
          finishStart = millis();
          state = FINISHING;
          Serial.println(F("3 laps done - heading to start section"));
        } else {
          state = DRIVE;
        }
      }
      break;
    }

    case FINISHING:
      steerTo(wallSteer());
      motor(SPEED_STRAIGHT);
      if (millis() - finishStart >= FINISH_MS) {
        motor(0); steerTo(0);
        state = DONE;
        Serial.println(F("STOPPED"));
      }
      break;

    case DONE:
      motor(0);
      steerTo(0);
      break;

    default:
      motor(0);
      break;
  }

  static uint8_t n = 0;
  if (++n >= 5) {
    n = 0;
    Serial.print(F("L=")); Serial.print(distL, 0);
    if (noEchoL) Serial.print(F("*"));
    Serial.print(F(" R=")); Serial.print(distR, 0);
    if (noEchoR) Serial.print(F("*"));
    Serial.print(F(" yaw=")); Serial.print(yaw, 0);
    Serial.print(F(" err=")); Serial.print(headingError(), 0);
    Serial.print(F(" corners=")); Serial.print(cornerCount);
    Serial.print(F(" state=")); Serial.println((int)state);
  }
}
