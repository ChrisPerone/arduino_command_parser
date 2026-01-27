#include <Arduino_LED_Matrix.h>
#include <malloc.h>
#include "emoticons.h"

ArduinoLEDMatrix matrix;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

// ------------------------------------------------------------
//  STATE STRUCTURES
// ------------------------------------------------------------

struct ValveState {
  bool active;
  bool isPwm;
  unsigned long offTime;
  int emoticon;
};

struct AnimationState {
  bool active = false;
  int frameCount = 0;
  int frameIndex = 0;
  int repeatCount = 0;
  int frameDelay = 100;
  unsigned long nextFrameTime = 0;
  const byte (*frames[10])[12]; // Fixed size: Allocate space for 10 frame pointers
};

AnimationState anim;
ValveState valves[10];

// ------------------------------------------------------------
//  PIN SETUP
// ------------------------------------------------------------

const byte valvePins[10] = {2,3,4,5,6,7,8,9,10,11};
const byte pwmPins[] = {3,5,6,9,10,11};
const byte pwmPinCount = ARRAY_SIZE(pwmPins);
const int pumpPin = 12;
const int minZone = 1;
const int maxZone = 10;
const int maxSafePSI = 30;
const int ACTION_NONE = 0;
const int ACTION_ON = 1;
const int ACTION_OFF = 2;
const int ACTION_DELAY = 3;

// ------------------------------------------------------------
//  SETUP
// ------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  matrix.begin();

  for (int i = 0; i < 10; i++)
    pinMode(valvePins[i], OUTPUT);

  pinMode(pumpPin, OUTPUT);

  Serial.println("Ready");
}

// ------------------------------------------------------------
//  UTILITIES
// ------------------------------------------------------------

int getFreeRam() {
  struct mallinfo mi = mallinfo();
  // On the R4 (Renesas), this calculates free heap space
  return mi.fordblks; 
}

// ------------------------------------------------------------
//  MAIN LOOP
// ------------------------------------------------------------

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    processCmd(line);
  }

  updateValves();
  updateAnimation();
  // Serial.print("Free RAM: ");
  // Serial.println(getFreeRam());  
}

// ------------------------------------------------------------
//  ANIMATION ENGINE (Option A)
// ------------------------------------------------------------
void startAnimation(const byte (*frameList[])[12], int count, int delayMs, int repeats = 1) {
  anim.active = true;
  anim.frameCount = count;
  anim.frameIndex = 0;
  anim.repeatCount = repeats;
  anim.frameDelay = delayMs;
  anim.nextFrameTime = millis();

  for (int i = 0; i < count; i++)
    anim.frames[i] = frameList[i];
}

void updateAnimation() {
  if (!anim.active) return;

  unsigned long now = millis();

  if (now >= anim.nextFrameTime) {
    // byte val = pgm_read_byte(&(smile1[row][col]));
    byte frameBuffer[8][12];
    // 2. Copy the frame from PROGMEM (Flash) into your RAM buffer
    memcpy_P(frameBuffer, anim.frames[anim.frameIndex], sizeof(frameBuffer));
  
    // 3. Render the RAM buffer
    matrix.renderBitmap(frameBuffer, 8, 12);    
    // matrix.renderBitmap(anim.frames[anim.frameIndex], 8, 12);

    anim.frameIndex++;

    if (anim.frameIndex >= anim.frameCount) {
      anim.frameIndex = 0;
      anim.repeatCount--;

      if (anim.repeatCount <= 0) {
        anim.active = false;
        matrix.clear();
        return;
      }
    }

    anim.nextFrameTime = now + anim.frameDelay;
  }
}

// ------------------------------------------------------------
//  EMOTICONS
// ------------------------------------------------------------

void showEmoticon(int emoticon, bool on) {
  matrix.clear();

  if (!on) {
    anim.active = false;
    return;
  }

  switch (emoticon) {
    case 10:
      startAnimation(emote2Frames, emote2FrameCount, 100, 3);
      break;

    case 8:
      startAnimation(emote3Frames, emote4FrameCount, 240, 1);
      break;

    case 2:
      startAnimation(emote4Frames, emote4FrameCount, 120, 2);
      break;

    case 1:
      startAnimation(emote1Frames, emote1FrameCount, 240, 1);
      break;

    default:
      startAnimation(emote2Frames, emote2FrameCount, 100, 1);
      break;
  }
}

// ------------------------------------------------------------
//  VALVE CONTROL
// ------------------------------------------------------------

void inflateZone(int zone, int durationMs, byte intensity, int emoticon) {
  int pin = valvePins[zone - 1];

  //TODO: Read analog pressure sensor mapped to each zone
  // if (currentPressure > maxSafePSI) {
  //     digitalWrite(pin, LOW); // Override command for safety
  // } else {
  bool isPwm = false;
  for (byte i = 0; i < pwmPinCount; ++i) {
    if (pwmPins[i] == pin) {
      isPwm = true;
      break;
    }
  }
  if (isPwm) {
    Serial.println("setting pwm pin");
    analogWrite(pin, intensity); 
  } else {
    digitalWrite(pin, HIGH);
  }

  valves[zone - 1].active = true;
  valves[zone - 1].isPwm = isPwm;
  valves[zone - 1].offTime = millis() + durationMs;
  valves[zone - 1].emoticon = emoticon;

  showEmoticon(emoticon, true);
}

void updateValves() {
  unsigned long now = millis();
  bool anyOpen = false;

  for (int i = 0; i < 10; i++) {
    if (valves[i].active) {
      anyOpen = true;

      if (now >= valves[i].offTime) {
        if (valves[i].isPwm) {
          analogWrite(valvePins[i], 0);
        } else {
          digitalWrite(valvePins[i], LOW);
        }
        valves[i].active = false;
        matrix.clear();
      }
    }
  }

  digitalWrite(pumpPin, anyOpen);
}

// ------------------------------------------------------------
//  COMMAND PARSER
//  Command format: "ON 1 5000 10" or "OFF 1 0 0"
// ------------------------------------------------------------

void processCmd(String &cmd) {
  cmd.trim();

  char act[6] = "";
  int zone = 0, t = 0, intensity = 0, emote = 0;
  byte pwmValue = 0;
  int pin = 0;

  int n = sscanf(cmd.c_str(), "%5s %d %d %d %d", act, &zone, &t, &intensity, &emote);
  // Convert the string action into a number ID
  int actionID = ACTION_NONE; 
  if (strcmp(act, "ON") == 0)      actionID = ACTION_ON;
  else if (strcmp(act, "OFF") == 0)     actionID = ACTION_OFF;
  else if (strcmp(act, "DELAY") == 0)   actionID = ACTION_DELAY;

  Serial.println("action:" + String(act) + " zone:" + String(zone) + " time:" + String(t) + " intensity:" + String(intensity) + " emote:" + String(emote) + " numArgs:" + String(n));
  Serial.println("actionID:" + String(actionID));

  switch (actionID) {
    case ACTION_ON:
      //Validation
      if (n != 5 || zone < minZone || zone > maxZone) {
        Serial.println("Invalid command");
        return;
      }
      digitalWrite(pumpPin, HIGH);
      pwmValue = constrain(intensity, 0, 255);
      inflateZone(zone, t, pwmValue, emote);
      break;
    case ACTION_OFF:
      //Validation
      if (n != 5 || zone < minZone || zone > maxZone) {
        Serial.println("Invalid command");
        return;
      }
      pin = valvePins[zone - 1];
      digitalWrite(pin, LOW);
      valves[zone - 1].active = false;
      showEmoticon(emote, false);
      break;
    case ACTION_DELAY:
      //Validation
      if (n < 2 || n > 3) {
        Serial.println("Invalid command");
        return;
      }
      Serial.println("DELAY " + String(zone) + "ms emote " + String(t));
      delay(zone);
      showEmoticon(t, false);
      break;
    default:
      Serial.println("Invalid action");
      return;
  }
  // if (actionID == 3 && (n < 2 || n > 3)) {
  //   Serial.println("Invalid command");
  //   return;
  // } else if ((actionID == 1 || actionID == 2) && (n != 5 || zone < minZone || zone > maxZone)) {
  //   Serial.println("Invalid command");
  //   return;
  // } else if (actionID == 0) {
  //   Serial.println("Invalid action");
  //   return;
  // }

  // int pin = valvePins[zone - 1];

  // if (actionID == 3) {
  //   Serial.println("DELAY " + String(zone) + "ms emote " + String(t));
  //   delay(zone);
  //   showEmoticon(t, false);
  //   return;
  // }

  // if (actionID == 1) {
  //   digitalWrite(pumpPin, HIGH);
  //   byte pwmValue = constrain(intensity, 0, 255);
  //   inflateZone(zone, t, pwmValue, emote);

  //   Serial.print("ON zone ");
  //   Serial.print(zone);
  //   Serial.print(" for ");
  //   Serial.print(t);
  //   Serial.print("ms intensity ");
  //   Serial.print(pwmValue);
  //   Serial.print(" emote ");
  //   Serial.println(emote);
  // } else {
  //   digitalWrite(pin, LOW);
  //   valves[zone - 1].active = false;
  //   showEmoticon(emote, false);

  //   Serial.print("OFF zone ");
  //   Serial.print(zone);
  //   Serial.print(" emote ");
  //   Serial.println(emote);
  // }

  bool anyOpen = false;
  for (int i = 0; i < 10; i++) {
    if (valves[i].active) {
      anyOpen = true;
      break;
    }
  }

  digitalWrite(pumpPin, anyOpen);
}