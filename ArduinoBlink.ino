#include <Adafruit_NeoPixel.h>
const int motorPin = 1;  // define motor pin globally
int NUM_LEDS = 160;
int LED_PIN = 6;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

/* Light + motor timing tuned for ~12s total show (160 LEDs):
- currentDelay starts at 400ms, shrinks by a progressive acceleration factor
- accelFactorStart = gentle early, accelFactorEnd = aggressive late
- minDelay floor must be low enough or 160 steps cannot finish in 12s
- eye blinks use the last ~2s of the window
*/

float currentDelay = 400.0;
const float accelFactorStart = 0.95;  // early: shrink delay gently (slow build)
const float accelFactorEnd = 0.40;    // late: shrink hard so cascade ends ~10.2s
const float minDelay = 10.0;
const int eyeBlinkMs = 500;           // 5 blinks ≈ 1.8s → total show ≈ 12s
const int maxBlinks = 5;


// Motor aligned to the same ~12s window as the lights
const int motorMinSpeed = 100; 
const int motorMaxSpeed = 255;
const unsigned long motorRampUpMs = 13000;  // ease-in across most of the show
const unsigned long motorRunMs = 14000;     // soft-stop when lights finish
const int motorRampDownStep = 8;
const unsigned long motorRampDownIntervalMs = 40;

/*
Creates a LightState class to more easily keep track of where in the process the code is at
  - println lightstate for debugging, and its used for conditional statements
*/

enum LightState { LIGHT_IDLE, LIGHT_CASCADE, LIGHT_BLINK, LIGHT_DONE };
LightState lightState = LIGHT_IDLE;
int ledIndex = 0;
int blinkCount = 0;
bool eyeIsOn = false;
unsigned long lastLightStepMs = 0;

int motorSpeed = 0;
unsigned long motorRampStartMs = 0;
unsigned long lastMotorRampDownMs = 0;

//Assigns different pins to different inputs and outputs. e.g. motor, sensor, LEDs

void setup() {
  Serial.begin(9600);
  pinMode(A1, INPUT);
  pinMode(motorPin, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  strip.begin();
  strip.show();
  analogWrite(motorPin, 0);
}

//ensures the function doesnt reset until all the code is executed, hasExecuted is returned as true at the end of the loop
bool hasExecuted = false;

/*
startBuffaloCascade controls the initial conditions of the lights half of the project, it resets all the values to their baseline 
  - also chages the lightState object to its next stage
*/
void startBuffaloCascade() {
  ledIndex = 0;
  currentDelay = 400.0;
  blinkCount = 0;
  eyeIsOn = false;
  lightState = LIGHT_CASCADE;
  lastLightStepMs = millis();

  strip.setPixelColor(ledIndex, strip.Color(221, 60, 0));
  strip.show();
}

/*After the initial conditions are set this function works in the loop to continuously update the lights in the following ways:
  - Keeps track of how much time has passed (now) through millis(), and only updates if the defined time delay between LEDs is less than the time passed
  - keeps track of progress defined as the number of LEDs lit up / the number of LEDs - 2, this value determines when the accelerationFactor 
  increases and is squared to delay when the accelerationFactor activates
  - the accelerationFactor is then multiplied into the currentDelay to shorten it in each iteration of the loop
  - the ledIndex then increases and the lastLightStepMs is updated to now
  - The last part of the code controls the eye light which only turn on after all the lights but 1 have turned on. It keeps track of the number of 
  blinks and flashes red every eyeBlinkMs
*/

void updateBuffaloCascade() {
  if (lightState != LIGHT_CASCADE && lightState != LIGHT_BLINK) {
    return;
  }

  unsigned long now = millis();

  if (lightState == LIGHT_CASCADE) {
    if (now - lastLightStepMs < (unsigned long)currentDelay) {
      return;
    }

    // 0 at first body LED → 1 near the eye; squared so speedup ramps up late
    float progress = (float)ledIndex / (float)max(1, NUM_LEDS - 2);
    if (progress > 1.0f) {
      progress = 1.0f;
    }
    float eased = progress * progress;
    float accelerationFactor =
        accelFactorStart - eased * (accelFactorStart - accelFactorEnd);

    currentDelay = currentDelay * accelerationFactor;
    if (currentDelay < minDelay) {
      currentDelay = minDelay;
    }

    ledIndex++;
    lastLightStepMs = now;

    if (ledIndex >= NUM_LEDS - 1) {
      int eyeLight = NUM_LEDS - 1;
      strip.setPixelColor(eyeLight, strip.Color(255, 0, 0)); //red
      strip.show();
      eyeIsOn = true;
      blinkCount = 0;
      lightState = LIGHT_BLINK;
      return;
    }

    strip.setPixelColor(ledIndex, strip.Color(221, 60, 0));  //gold color 
    strip.show();
  }

  if (lightState == LIGHT_BLINK) {
    if (now - lastLightStepMs < (unsigned long)eyeBlinkMs) {
      return;
    }

    int eyeLight = NUM_LEDS - 1;
    lastLightStepMs = now;

    if (eyeIsOn) {
      strip.setPixelColor(eyeLight, strip.Color(0, 0, 0));
      strip.show();
      eyeIsOn = false;
    } else {
      blinkCount++;
      if (blinkCount >= maxBlinks) {
        strip.clear();
        strip.show();
        lightState = LIGHT_DONE;
        return;
      }
      strip.setPixelColor(eyeLight, strip.Color(255, 0, 0));
      strip.show();
      eyeIsOn = true;
    }
  }
}

/*
- Controls the initial conditions of the motor every time the loop starts again for the first time. It defines:
  - the starting speed of the motor and keeps track of how much time has passed through motorRampStartMs and lastMotorRampDownMs for later 
  use in updateMotor
*/
void startMotor() {
  motorRampStartMs = millis();
  lastMotorRampDownMs = millis();
  motorSpeed = motorMinSpeed;
  analogWrite(motorPin, motorSpeed);
}

void stopMotor() {
  motorSpeed = 0;
  analogWrite(motorPin, 0);
}

// Motor follows motorRunMs / motorRampUpMs — not the LED cascade length
void updateMotor() {
  // Idle and already stopped
  if (lightState == LIGHT_IDLE && motorSpeed <= 0) {
    return;
  }

  unsigned long elapsed = millis() - motorRampStartMs;
  int targetSpeed = 0;

  // Run only while show is active AND within the motor timeframe
  bool showActive = (lightState == LIGHT_CASCADE || lightState == LIGHT_BLINK);
  bool withinTimeframe = (elapsed < motorRunMs);

  if (showActive && withinTimeframe) {
    if (elapsed >= motorRampUpMs) {
      targetSpeed = motorMaxSpeed;
    } else {
      // Ease-in: slow at first, then faster toward max (still ends by motorRampUpMs)
      float t = (float)elapsed / (float)motorRampUpMs;
      float eased = t * t * (3.0f - 2.0f * t);  // slow start and slow finish
      targetSpeed = motorMinSpeed +
                    (int)((motorMaxSpeed - motorMinSpeed) * eased);
    }
    targetSpeed = constrain(targetSpeed, motorMinSpeed, motorMaxSpeed);
  } else {
    // Timeframe over, or show finished → soft-stop
    targetSpeed = 0;
  }

  if (targetSpeed > motorSpeed) {
    motorSpeed = targetSpeed;
    analogWrite(motorPin, motorSpeed);
  } else if (targetSpeed < motorSpeed) {
    unsigned long now = millis();
    if (now - lastMotorRampDownMs >= motorRampDownIntervalMs) {
      lastMotorRampDownMs = now;
      motorSpeed -= motorRampDownStep;
      if (motorSpeed < targetSpeed) {
        motorSpeed = targetSpeed;
      }
      if (motorSpeed <= 0) {
        stopMotor();
      } else {
        analogWrite(motorPin, motorSpeed);
      }
    }
  }
}

void loop() {
  int lightValue = analogRead(A1);
  Serial.println(lightValue);
  bool lightDropped = (lightValue < 700);

  // One trigger starts the full show; sensor can uncover mid-run
  if (lightDropped && !hasExecuted && lightState == LIGHT_IDLE) {
    Serial.println(lightValue);
    startMotor();
    startBuffaloCascade();
    hasExecuted = true;
  }

  updateBuffaloCascade();
  updateMotor();

  // Re-arm only after the show finishes and light is above the threshold again
  if (lightState == LIGHT_DONE && !lightDropped) {
    hasExecuted = false;
    lightState = LIGHT_IDLE;
  }
}
