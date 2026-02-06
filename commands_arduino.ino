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
const byte pinCount = ARRAY_SIZE(valvePins);
const byte pwmPinCount = ARRAY_SIZE(pwmPins);
const int pumpPin = 12;
const int minZone = 1;
const int maxZone = 10;
const int maxSafePSI = 30;
enum class ActionType { NONE, ON, OFF, DELAY, SHUTDOWN };

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
  static unsigned long nextActionTime = 0;
  unsigned long currentTime = millis();

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    processCmd(line);
  } else if (currentTime >= nextActionTime) {
    // Generate the mock command
    // sendMockCommand();

    // Schedule the next random interval
    nextActionTime = currentTime + random(100, 1000); 
  }

  // Wire.requestFrom(8, 4); // Ask Mega for status
  //   while (Wire.available()) {
  //     char c = Wire.read();
  //     if (c == 'B') { // 'B' for Beat
  //       // I now know your heart just beat in the real world!
  //       updateAIChat("User Heartbeat Detected"); 
  //     }
  // }

// --- NEW: SENSORY FEEDBACK STREAM ---
  static unsigned long lastDataTime = 0;
  if (millis() - lastDataTime > 100) { // Send data every 100ms
    // int p1 = analogRead(A1); // Chest
    // int p2 = analogRead(A2); // Thigh
    // int p3 = analogRead(A3); // Hand/Touch
    // int p4 = analogRead(A4); // Vacuum/PSI

    // Standardized format for our mitmproxy bridge
    // Serial.print("DATA: P1:"); Serial.println(p1);
    // Serial.print(" P2:"); Serial.print(p2);
    // Serial.print(" P3:"); Serial.print(p3);
    // Serial.print(" P4:"); Serial.println(p4);

    lastDataTime = millis();
  }
  updateValves();
  updateAnimation();
  // Serial.print("Free RAM: ");
  // Serial.println(getFreeRam());  
}

void sendMockCommand() {
  char cmdBuffer[40]; // Buffer to hold the command string

  // 2. Generate the random values
  int pin      = random(1, 7);    // 1 through 6
  int duration = random(500, 5001); // 500 through 5000
  int intensity = 255; //random(120, 256); // 120 through 255
  int count    = random(1, 11);   // 1 through 10

  // 3. Format the string: "ON <pin> <duration> <intensity> <count>"
  sprintf(cmdBuffer, "ON %d %d %d %d", pin, duration, intensity, count);

  // 4. Pass it to your existing function
  Serial.print("Mocking: ");
  Serial.println(cmdBuffer);
  processCmd(cmdBuffer);
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
    case 15:
      // frameDelay = 60000/(BPM*frameCount)    
      startAnimation(emoteHeartbeatFrames, emoteHeartbeatFrameCount, 100, 3);
      break;

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

  for (int i = 0; i < pinCount; i++) {
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
//  Command format: action pin delayMS pwm(intensity) emoji
//  pwm param only valid for pwm pins, otherwise skips
//  e.g. "ON 2 5000 120 10" set pin 2 to high for 5 seconds at pwm 120 and show emoji 10
//  e.g. "OFF 1 3000 10" set pin 1 to low for 3 seconds and show emoji 10
//  e.g. "DELAY 1000 8" delay everything for 1 second and show emoji 8
// ------------------------------------------------------------

void processCmd(const String &cmd) {
  char act[12] = "";
  int zone = 0, t = 0, intensity = 0, emote = 0;
  byte pwmValue = 0;
  int pin = 0;

  int n = sscanf(cmd.c_str(), "%11s %d %d %d %d", act, &zone, &t, &intensity, &emote);
  // Convert the string action into a number ID
  ActionType action = ActionType::NONE;
  if (strcasecmp(act, "ON") == 0)             action = ActionType::ON;
  else if (strcasecmp(act, "OFF") == 0 ||
           strcasecmp(act, "DUMP") == 0)      action = ActionType::OFF;
  else if (strcasecmp(act, "DELAY") == 0)     action = ActionType::DELAY;
  else if (strcasecmp(act, "SHUTDOWN") == 0)  action = ActionType::SHUTDOWN;

  Serial.println("action:" + String(int(action)) + " zone:" + String(zone) + " time:" + String(t) + " intensity:" + String(intensity) + " emote:" + String(emote) + " numArgs:" + String(n));

  switch (action) {
    case ActionType::ON:
      //Validation
      if (n != 5 || zone < minZone || zone > maxZone) {
        Serial.println("Invalid command");
        return;
      }
      digitalWrite(pumpPin, HIGH);
      pwmValue = constrain(intensity, 0, 255);
      inflateZone(zone, t, pwmValue, emote);
      break;
    case ActionType::OFF:
      //Validation
      if (n != 4 || zone < minZone || zone > maxZone) {
        Serial.println("Invalid command");
        return;
      }
      pin = valvePins[zone - 1];
      digitalWrite(pin, LOW);
      valves[zone - 1].active = false;
      showEmoticon(emote, false);
      break;
    case ActionType::DELAY:
      //Validation
      if (n != 3) {
        Serial.println("Invalid command");
        return;
      }
      Serial.println("DELAY " + String(zone) + "ms emote " + String(t));
      delay(zone);
      showEmoticon(t, false);
      break;
    case ActionType::SHUTDOWN:
      Serial.println("SHUTDOWN");
      for (int i = 0; i < pinCount; ++i) {
        if (valves[i].active) {
          valves[i].active = false;
          if (valves[i].isPwm) {
            analogWrite(valvePins[i], 0);
          } else {
            digitalWrite(valvePins[i], LOW);
          }
        }
      }
      delay(30000);
      break;
    default:
      Serial.println("Invalid action");
      return;
  }
}