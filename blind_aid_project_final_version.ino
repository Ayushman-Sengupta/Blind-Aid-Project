// Rotating ultrasonic blind-aid, v5: STOP alert + 360 scan + single buzzer beep codes
// Board: Arduino Uno R4 Minima + CNC Shield V3 + A4988 (X driver) + NEMA 17 + HC-SR04
//        + 1 passive buzzer + 2 LEDs
//
// Wiring:
//   Motor driver (X): EN = D8, STEP = D2, DIR = D5 (built into the shield)
//   HC-SR04:  Trig = D9 (X-), Echo = D10 (Y-), VCC = 5V, GND = GND
//   BUZZER:   signal D11 (Z-) -> + leg (direct, or via NPN transistor), - leg to GND
//             (if D11 does not work, move the wire to D12 and set PIN_BUZZ = 12 below)
//   LEFT LED:  long leg -> 470 ohm -> A0 (Abort), short leg -> GND
//   RIGHT LED: long leg -> 470 ohm -> A1 (Hold),  short leg -> GND
//
// Signals the person hears / sees:
//   STOP      = rapid high-pitched beeps, both LEDs flash   (object detected, scan starting)
//   1 beep    = LEFT free,   left LED on
//   2 beeps   = RIGHT free,  right LED on
//   3 beeps   = BEHIND free, both LEDs on
//   BLOCKED   = 4 long LOW-pitched beeps, LEDs alternate     (nothing free)
//
// How it works:
//   1. An object closer than TRIGGER_CM plays the STOP signal and starts a scan.
//   2. The sensor sweeps the full circle, measuring every 18 degrees.
//   3. If the front is clear again during the scan (someone walked past): do nothing.
//   4. Zones: LEFT and RIGHT (about 60 to 135 deg each side), BEHIND (beyond 135 deg),
//      FRONT (+-60 deg). A zone is "free" if nothing closer than FREE_CM was seen in it.
//        left and/or right free -> pick one (only one free: that one; both free: more
//                                  clearance wins, near tie -> wider gap wins, else TIE_GOES_LEFT)
//        neither side free, behind free -> BEHIND
//        nothing free -> BLOCKED signal
//   5. The beep code plays and the LEDs stay on for HOLD_MS. The sensor stays at center
//      unless ALERT_FACING_FREE is true.
//   6. It then waits for the path in front to clear before listening again.
//
// At power-up every signal plays once as a self-test (set RUN_SELFTEST to false to skip it).

#define FW_NAME "blind-aid v5 (stop alert, 360 scan, beep codes)"

// ---------- Pins ----------
const int PIN_EN     = 8;
const int PIN_STEP   = 2;
const int PIN_DIR    = 5;
const int PIN_TRIG   = 9;
const int PIN_ECHO   = 10;
const int PIN_BUZZ   = 11;   // change to 12 if the D11 buzzer does not work
const int PIN_LED_L  = A0;
const int PIN_LED_R  = A1;

// ---------- Settings you can tweak ----------
// Motor / scan
const bool          LEFT_DIR          = HIGH;   // calibrated: HIGH on this build. Flip back to LOW if left/right swap again
const int           SWEEP_STEPS       = 100;    // 100 steps x 1.8 deg = 180 deg each side (full 360 view)
const int           SAMPLE_STEPS      = 10;     // measure every 10 steps = every 18 deg
const int           ZONE_STEPS        = 33;     // front zone = +-60 deg
const int           REAR_STEPS        = 75;     // beyond +-135 deg counts as "behind"
const int           FRONT_STEPS       = 10;     // "front" = +-18 deg, used to detect a person walking past
const int           STEP_US           = 2000;   // raise to 2500 if the motor skips
const bool          ALERT_FACING_FREE = false;  // false = alert from center (safer for loose head wires)
                                                // true  = turn the sensor toward the free spot first

// Distances (cm)
const float         TRIGGER_CM        = 80.0;   // closer than this = STOP + scan. Use 20-40 for table-top tests,
                                                // 100-150 for real walking
const float         FRONT_CLEAR_CM    = TRIGGER_CM + 10.0;  // front at least this clear during the scan = ignore
const float         REARM_CM          = TRIGGER_CM + 10.0;  // path must be clearer than this to re-arm
const float         FREE_CM           = 40.0;   // a zone is free if nothing is closer than this (60 for real walking)
const float         TIE_CM            = 15.0;   // clearances within this count as equal
const bool          TIE_GOES_LEFT     = true;   // final tie-break between left and right

// Direction beeps
const unsigned long HOLD_MS           = 2000;   // LEDs stay on this long (beeps play at the start)
const unsigned long BEEP_MS           = 250;    // length of each direction beep
const unsigned long BEEP_GAP_MS       = 250;    // silence between direction beeps
const unsigned int  BUZZ_HZ           = 2000;   // direction beep pitch; try 2500-4000 if it sounds quiet

// STOP signal: rapid, high pitched
const int           STOP_BEEPS        = 6;
const unsigned long STOP_ON_MS        = 80;
const unsigned long STOP_OFF_MS       = 60;
const unsigned int  STOP_HZ           = 3000;

// BLOCKED signal: slow, long, low pitched
const int           BLOCKED_BEEPS     = 4;
const unsigned long BLOCKED_BEEP_MS   = 600;
const unsigned long BLOCKED_GAP_MS    = 300;
const unsigned int  BLOCKED_HZ        = 1500;

const bool          RUN_SELFTEST      = true;   // play every signal once at power-up
// --------------------------------------------

int  motorPos   = 0;      // current sensor position in steps (0 = center, negative = left)
bool armed      = true;
int  clearCount = 0;
int  waitPrint  = 0;

// ---------- Distance ----------
float readCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long us = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (us == 0) return 400.0;
  return us * 0.0343 / 2.0;
}

float medianCm() {
  float a = readCm(); delay(30);
  float b = readCm(); delay(30);
  float c = readCm();
  if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
  if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
  return c;
}

// ---------- Motor ----------
// Move the sensor to an absolute step position (negative = left of center)
void goTo(int target) {
  int diff = target - motorPos;
  if (diff == 0) return;
  bool dirLevel = (diff < 0) ? LEFT_DIR : !LEFT_DIR;
  digitalWrite(PIN_DIR, dirLevel ? HIGH : LOW);
  int n = abs(diff);
  for (int i = 0; i < n; i++) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(STEP_US);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(STEP_US);
  }
  motorPos = target;
}

// ---------- Signals ----------
void allOff() {
  digitalWrite(PIN_BUZZ,  LOW);
  digitalWrite(PIN_LED_L, LOW);
  digitalWrite(PIN_LED_R, LOW);
}

// Square wave on the passive buzzer: hz pitch for ms milliseconds
void tonePulse(unsigned int hz, unsigned long ms) {
  unsigned long halfUs = 500000UL / hz;
  unsigned long start = millis();
  while (millis() - start < ms) {
    digitalWrite(PIN_BUZZ, HIGH);
    delayMicroseconds(halfUs);
    digitalWrite(PIN_BUZZ, LOW);
    delayMicroseconds(halfUs);
  }
  digitalWrite(PIN_BUZZ, LOW);
}

// STOP: rapid high-pitched beeps, both LEDs flash with each beep
void stopSignal() {
  for (int i = 0; i < STOP_BEEPS; i++) {
    digitalWrite(PIN_LED_L, HIGH);
    digitalWrite(PIN_LED_R, HIGH);
    tonePulse(STOP_HZ, STOP_ON_MS);
    digitalWrite(PIN_LED_L, LOW);
    digitalWrite(PIN_LED_R, LOW);
    if (i < STOP_BEEPS - 1) delay(STOP_OFF_MS);
  }
  allOff();
}

// Direction code: "beeps" beeps; the chosen LEDs stay on until holdMs has passed since the start
void alertDirection(int beeps, bool ledLeft, bool ledRight, unsigned long holdMs) {
  digitalWrite(PIN_LED_L, ledLeft  ? HIGH : LOW);
  digitalWrite(PIN_LED_R, ledRight ? HIGH : LOW);
  unsigned long start = millis();
  for (int i = 0; i < beeps; i++) {
    tonePulse(BUZZ_HZ, BEEP_MS);
    if (i < beeps - 1) delay(BEEP_GAP_MS);
  }
  while (millis() - start < holdMs) delay(5);
  allOff();
}

// BLOCKED: slow, long, low-pitched beeps, LEDs alternate left/right
void blockedAlert() {
  for (int i = 0; i < BLOCKED_BEEPS; i++) {
    bool leftOn = (i % 2 == 0);
    digitalWrite(PIN_LED_L, leftOn ? HIGH : LOW);
    digitalWrite(PIN_LED_R, leftOn ? LOW : HIGH);
    tonePulse(BLOCKED_HZ, BLOCKED_BEEP_MS);
    digitalWrite(PIN_LED_L, LOW);
    digitalWrite(PIN_LED_R, LOW);
    if (i < BLOCKED_BEEPS - 1) delay(BLOCKED_GAP_MS);
  }
  allOff();
}

void selfTest() {
  Serial.println("Self-test: STOP signal");
  stopSignal();
  delay(800);
  Serial.println("Self-test: 1 beep (left)");
  alertDirection(1, true, false, 0);
  delay(600);
  Serial.println("Self-test: 2 beeps (right)");
  alertDirection(2, false, true, 0);
  delay(600);
  Serial.println("Self-test: 3 beeps (behind)");
  alertDirection(3, true, true, 0);
  delay(800);
  Serial.println("Self-test: BLOCKED signal");
  blockedAlert();
  delay(300);
}

// ---------- Scan ----------
// zones: 0 = left, 1 = front, 2 = right, 3 = behind
int zoneOf(int pos) {
  if (abs(pos) > REAR_STEPS) return 3;
  if (pos < -ZONE_STEPS)     return 0;
  if (pos >  ZONE_STEPS)     return 2;
  return 1;
}

// How far a step position is from the middle of its zone (used to break ties)
int zoneDist(int z, int pos) {
  int sideMid = (REAR_STEPS + ZONE_STEPS) / 2;
  if (z == 0) return abs(pos + sideMid);
  if (z == 2) return abs(pos - sideMid);
  if (z == 3) return abs(abs(pos) - SWEEP_STEPS);
  return abs(pos);
}

// Returns true if an alert was given, false if the scan was ignored.
bool scanAndAlert() {
  Serial.println("Object detected: STOP signal, scanning");
  stopSignal();                                // tell the person to stop before the scan starts

  float minZ[4]      = {999, 999, 999, 999};   // nearest reading in each zone
  float bestD[4]     = {-1, -1, -1, -1};       // clearest reading in each zone
  int   bestPos[4]   = {0, 0, 0, 0};           // step position of that clearest reading
  int   freeCount[4] = {0, 0, 0, 0};           // readings in the zone that were >= FREE_CM
  float frontMin     = 999;                    // nearest reading straight ahead

  for (int pos = -SWEEP_STEPS; pos <= SWEEP_STEPS; pos += SAMPLE_STEPS) {
    goTo(pos);
    delay(80);                                 // let the sensor settle
    float d = medianCm();

    int z = zoneOf(pos);
    if (d < minZ[z]) minZ[z] = d;
    if (d >= FREE_CM) freeCount[z]++;
    if (abs(pos) <= FRONT_STEPS && d < frontMin) frontMin = d;
    if (d > bestD[z] || (d == bestD[z] && zoneDist(z, pos) < zoneDist(z, bestPos[z]))) {
      bestD[z]   = d;
      bestPos[z] = pos;
    }
  }

  Serial.print("Nearest  L: ");  Serial.print(minZ[0]);
  Serial.print("  Front: ");     Serial.print(frontMin);
  Serial.print("  R: ");         Serial.print(minZ[2]);
  Serial.print("  Behind: ");    Serial.println(minZ[3]);

  // Front clear again: someone walked past, do nothing
  if (frontMin >= FRONT_CLEAR_CM) {
    Serial.println("Front clear again (person passing?) - ignoring");
    goTo(0);
    return false;
  }

  bool leftFree   = (minZ[0] >= FREE_CM);
  bool rightFree  = (minZ[2] >= FREE_CM);
  bool behindFree = (minZ[3] >= FREE_CM);

  // Pick the direction: 0 = left, 2 = right, 3 = behind, -1 = nothing free
  int w = -1;
  if (leftFree && !rightFree)      w = 0;
  else if (rightFree && !leftFree) w = 2;
  else if (leftFree && rightFree) {
    if (fabs(minZ[0] - minZ[2]) > TIE_CM)    w = (minZ[0] > minZ[2]) ? 0 : 2;           // more clearance
    else if (freeCount[0] != freeCount[2])   w = (freeCount[0] > freeCount[2]) ? 0 : 2; // wider gap
    else                                     w = TIE_GOES_LEFT ? 0 : 2;
  }
  else if (behindFree)             w = 3;

  if (w == -1) {
    Serial.println("Nothing free: BLOCKED signal");
    goTo(0);
    blockedAlert();
    return true;
  }

  goTo(ALERT_FACING_FREE ? bestPos[w] : 0);

  if (w == 0) {
    Serial.println("Free: LEFT (1 beep, left LED)");
    alertDirection(1, true, false, HOLD_MS);
  } else if (w == 2) {
    Serial.println("Free: RIGHT (2 beeps, right LED)");
    alertDirection(2, false, true, HOLD_MS);
  } else {
    Serial.println("Free: BEHIND (3 beeps, both LEDs)");
    alertDirection(3, true, true, HOLD_MS);
  }

  goTo(0);                                     // back to center
  return true;
}

// ---------- Arduino ----------
void setup() {
  Serial.begin(9600);
  pinMode(PIN_EN,    OUTPUT);
  pinMode(PIN_STEP,  OUTPUT);
  pinMode(PIN_DIR,   OUTPUT);
  pinMode(PIN_TRIG,  OUTPUT);
  pinMode(PIN_ECHO,  INPUT);
  pinMode(PIN_BUZZ,  OUTPUT);
  pinMode(PIN_LED_L, OUTPUT);
  pinMode(PIN_LED_R, OUTPUT);
  allOff();
  digitalWrite(PIN_EN, LOW);                   // driver on, motor holds position

  delay(500);
  Serial.println("BOOT");
  Serial.println(FW_NAME);
  if (RUN_SELFTEST) selfTest();
  Serial.println("Ready");
}

void loop() {
  float d = readCm();

  if (!armed) {
    // Alert already given: wait for a clear path before listening again
    if (++waitPrint >= 10) {                   // print about once a second
      waitPrint = 0;
      Serial.print("Waiting, distance: "); Serial.println(d);
    }
    if (d > REARM_CM) {
      clearCount++;
      if (clearCount >= 3) {
        armed = true;
        clearCount = 0;
        Serial.println("Re-armed, waiting for next object");
      }
    } else {
      clearCount = 0;
    }
    delay(100);
    return;
  }

  if (d < TRIGGER_CM) {
    delay(50);
    float d2 = readCm();                       // confirm with a second reading
    if (d2 < TRIGGER_CM) {
      if (scanAndAlert()) {
        armed = false;
        clearCount = 0;
        waitPrint = 0;
      } else {
        delay(500);                            // ignored scan: stay armed, short pause
      }
    }
  }
  delay(60);
}
