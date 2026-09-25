#include "tm.h"
#include "fan.h"

#define ONEWIRE_SEARCH 0
#include <OneWire.h>
#include <DS18B20.h>

// Temp sensor
static uint8_t ds18B20Pin = 4;
static uint8_t ds18B20Address[8] = {0}; // queried later
static DS18B20 tempSensor(ds18B20Pin);
static bool tempSensorPresent = false;

typedef enum {
  TM_IDLE = 0,     // fan stopped
  TM_STARTING,    // SAFE_START_DUTY kick, the fan is getting moving
  TM_RUNNING,     // PI in charge, duty floored at TM_MIN_RUN_DUTY
} TmState;

static const char *tmStateName[] = {"IDLE", "STARTING", "RUNNING"};

static bool tmEnabled = false;
static bool tmAlwaysRun = TM_ALWAYS_RUN;
static float tmSetpoint = TEMP_SETPOINT;
static TmState tmState = TM_IDLE;
static uint32_t tmStateSinceMs = 0;
static uint32_t tmLastSampleMs = 0;

// Set once the demand drops into the stop band, cleared as soon as it leaves
// again; the fan only stops if it stays there for TM_STOP_DWELL_MS.
static bool tmBelowStop = false;
static uint32_t tmBelowStopSinceMs = 0;

static uint8_t tmBadReads = 0;
static bool tmFaulted = false;
static bool tmSeedFromFan = false;  // bumpless takeover, see setThermalEnabled()

static float tmTempFilt = NAN;  // EMA of the reading, NAN until the first sample
static float tmProp = 0.0f;     // P term of the last control step
static float tmInteg = 0.0f;    // I term, in percent of duty
static float tmDemand = 0.0f;   // P + I, before the minimum-duty floor

// Find the sensor and put it into 9 bit mode. Also called from updateThermal()
// while no sensor is known, so one plugged in after boot is picked up.
static bool tmProbeSensor(void) {
  if (!tempSensor.selectNext()) return false;

  tempSensor.getAddress(ds18B20Address);
  uint8_t resBit = tempSensor.getResolution();
  Serial.print("DS18B20 sensor selected, res:");
  Serial.print(resBit);
  Serial.print(", addr: ");
  for (uint8_t i = 0; i < 8; i++) {
    if (i) Serial.print(' ');
    // print(..., HEX) drops the leading zero, so pad bytes below 0x10
    if (ds18B20Address[i] < 0x10) Serial.print('0');
    Serial.print(ds18B20Address[i], HEX);
  }
  Serial.println();
  if (resBit != 9) {
    Serial.println("Temp sensor res!=t 9, set 9bit...");
    tempSensor.setResolution(9);
  }
  return true;
}

static void tmEnterState(TmState state) {
  if (tmState == state) return;
  tmState = state;
  tmStateSinceMs = millis();
  tmBelowStop = false;
  Serial.print("tm state: ");
  Serial.println(tmStateName[state]);
}

// Move the integrator so that P + I lands on the given duty. Used wherever the
// controller takes over an output it did not compute itself, so the handover
// does not show up as a step on the fan.
static void tmSeedInteg(float duty) {
  tmInteg = duty - tmProp;
}

static void tmWriteDuty(uint8_t duty) {
  if (duty != getFanSpeed()) setFanSpeed(duty);  // setFanSpeed() logs, so only on a change
}

// Drive the fan from the state machine. The PI only has a say in TM_RUNNING;
// the other two states are the ends of the range the fan cannot resolve.
static void tmApplyOutput(void) {
  switch (tmState) {
    case TM_STARTING:
      tmWriteDuty(SAFE_START_DUTY);
      break;
    case TM_RUNNING: {
      float duty = tmDemand;
      if (duty < (float)TM_MIN_RUN_DUTY) duty = TM_MIN_RUN_DUTY;
      if (duty > (float)TM_MAX_DUTY) duty = TM_MAX_DUTY;
      tmWriteDuty((uint8_t)(duty + 0.5f));
      break;
    }
    default:
      tmWriteDuty(0);
      break;
  }
}

// No usable temperature: run the fan flat out until one comes back. The start
// kick is skipped on purpose, the ramp in fan.cpp walks the duty up through the
// starting range anyway.
static void tmFailSafe(void) {
  tmProp = 0.0f;
  tmInteg = TM_FAULT_DUTY;
  tmDemand = TM_FAULT_DUTY;
  tmEnterState(TM_RUNNING);
  tmWriteDuty(TM_FAULT_DUTY);
}

static void tmControlStep(void) {
  const float dt = TM_SAMPLE_MS / 1000.0f;
  uint32_t now = millis();
  uint32_t inState = now - tmStateSinceMs;

  float err = tmTempFilt - tmSetpoint;
  tmProp = TM_KP * err;

  if (tmSeedFromFan) {  // first step after being enabled
    tmSeedInteg((float)getFanSpeed());
    tmSeedFromFan = false;
  } else {
    tmInteg += TM_KI * err * dt;
  }

  // Anti-windup by back-calculation, against the output the state machine can
  // actually reach right now rather than the full range: while the fan is off
  // the demand only has to make it to the restart threshold, and while it is
  // being kicked the duty is fixed, so integrating past either would come back
  // as an overshoot in the first seconds of TM_RUNNING.
  float ceiling = TM_MAX_DUTY;
  if (tmState == TM_IDLE) ceiling = TM_MIN_RUN_DUTY;
  else if (tmState == TM_STARTING) ceiling = SAFE_START_DUTY;

  // The same argument holds at the bottom: while the fan is always on it never
  // goes below TM_MIN_RUN_DUTY, so letting the integrator sink under that floor
  // while the machine is cold would only buy a long delay before the duty
  // reacts once the temperature finally comes back up.
  float floorDuty = 0.0f;
  if (tmAlwaysRun && tmState == TM_RUNNING) floorDuty = (float)TM_MIN_RUN_DUTY;

  float demand = tmProp + tmInteg;
  if (demand > ceiling) {
    demand = ceiling;
    tmInteg = demand - tmProp;
  } else if (demand < floorDuty) {
    demand = floorDuty;
    tmInteg = demand - tmProp;
  }
  tmDemand = demand;

  switch (tmState) {
    case TM_IDLE:
      // Always on: the state is only reachable at boot, on a bumpless takeover
      // from a stopped fan, and while the flag was still off, so start now.
      // Otherwise the integrator keeps running while the fan is off and the
      // restart is triggered by how long the temperature has been above the
      // setpoint rather than by a fixed temperature band.
      if (tmAlwaysRun || (demand >= (float)TM_MIN_RUN_DUTY && inState >= TM_MIN_OFF_MS)) {
        tmSeedInteg((float)SAFE_START_DUTY);  // so the kick does not drop away when it ends
        tmEnterState(TM_STARTING);
      }
      break;

    case TM_STARTING:
      // Ends on the first sample past TM_KICK_MS, i.e. the kick can be up to one
      // sample longer than configured. Harmless, it only delays the settling.
      if (inState >= TM_KICK_MS) tmEnterState(TM_RUNNING);
      break;

    case TM_RUNNING:
      if (tmAlwaysRun) {
        tmBelowStop = false;
        break;
      }
      if (demand < (float)(TM_MIN_RUN_DUTY - TM_STOP_HYST)) {
        if (!tmBelowStop) {
          tmBelowStop = true;
          tmBelowStopSinceMs = now;
        }
      } else {
        tmBelowStop = false;
      }
      if (tmBelowStop && (now - tmBelowStopSinceMs) >= TM_STOP_DWELL_MS &&
          inState >= TM_MIN_ON_MS) {
        // Park the integrator well under the restart threshold, so the fan has
        // to earn its way back up rather than restarting on the first wobble.
        tmSeedInteg((float)TM_RESTART_SEED);
        tmEnterState(TM_IDLE);
      }
      break;
  }

  tmApplyOutput();
}

void updateThermal(void) {
  uint32_t now = millis();
  if (now - tmLastSampleMs < TM_SAMPLE_MS) return;
  tmLastSampleMs = now;

  if (!tempSensorPresent) tempSensorPresent = tmProbeSensor();

  // select() checks that the sensor answers and that its scratchpad CRC is
  // good; without it a disconnected bus reads back as all ones, which getTempC()
  // would hand us as a plausible looking -0.5C.
  float t = NAN;
  bool good = false;
  if (tempSensorPresent && tempSensor.select(ds18B20Address)) {
    t = tempSensor.getTempC();
    good = (t >= TM_TEMP_MIN && t <= TM_TEMP_MAX);
  }

  if (good) {
    if (tmFaulted) {
      Serial.println("tm: temp sensor back");
      tmFaulted = false;
    }
    tmBadReads = 0;
    if (isnan(tmTempFilt)) tmTempFilt = t;  // no history yet, start from here
    else tmTempFilt += TM_FILTER_ALPHA * (t - tmTempFilt);
  } else if (tmBadReads < 255) {
    tmBadReads++;
    if (tmBadReads >= TM_FAULT_READS && !tmFaulted) {
      Serial.println("tm: temp sensor lost, fan to full speed");
      tmFaulted = true;
    }
  }

  // Sampling runs even with management off, the advertisement wants the reading.
  if (!tmEnabled) return;

  if (tmBadReads >= TM_FAULT_READS || isnan(tmTempFilt)) {
    tmFailSafe();
    return;
  }
  tmControlStep();
}

void setThermalEnabled(bool on) {
  if (on == tmEnabled) return;
  tmEnabled = on;

  if (!on) {
    Serial.println("tm off, fan left at its current speed");
    return;
  }

  uint8_t fan = getFanSpeed();
  tmDemand = (float)fan;
  tmSeedFromFan = true;  // the first control step seeds the integrator from the fan
  tmEnterState(fan >= TM_MIN_RUN_DUTY ? TM_RUNNING : TM_IDLE);
  // Age out the minimum off time, otherwise turning management on while the fan
  // is stopped would sit on a hot sensor for TM_MIN_OFF_MS before reacting.
  if (tmState == TM_IDLE) tmStateSinceMs = millis() - TM_MIN_OFF_MS;
  tmLastSampleMs = millis() - TM_SAMPLE_MS;  // control on the next updateThermal()
  Serial.print("tm on, taking over at ");
  Serial.print(fan);
  Serial.println('%');
}

bool getThermalEnabled(void) {
  return tmEnabled;
}

void setThermalAlwaysRun(bool on) {
  if (on == tmAlwaysRun) return;
  tmAlwaysRun = on;
  // Turning it off mid-run must not stop the fan on the spot: the stop band
  // starts counting from here, so TM_STOP_DWELL_MS still has to pass. Turning
  // it on is picked up by the next control step, which leaves TM_IDLE.
  tmBelowStop = false;
  Serial.print("tm fan always on: ");
  Serial.println(on ? 1 : 0);
}

bool getThermalAlwaysRun(void) {
  return tmAlwaysRun;
}

void setThermalSetpoint(float degC) {
  if (degC < TM_SETPOINT_MIN) degC = TM_SETPOINT_MIN;
  if (degC > TM_SETPOINT_MAX) degC = TM_SETPOINT_MAX;
  tmSetpoint = degC;
  Serial.print("tm setpoint: ");
  Serial.print(tmSetpoint, 1);
  Serial.println('C');
}

float getThermalSetpoint(void) {
  return tmSetpoint;
}

bool hasTempSensor(void) {
  return tempSensorPresent;
}

float getTemperature(void) {
  return tmTempFilt;
}

String getThermalStatus(void) {
  String s = tmEnabled ? "ON " : "OFF ";
  s += tmStateName[tmState];
  s += " t=" + String(tmTempFilt, 1);
  s += " sp=" + String(tmSetpoint, 1);
  s += " P=" + String(tmProp, 1);
  s += " I=" + String(tmInteg, 1);
  s += " dem=" + String(tmDemand, 1);
  s += " fan=" + String(getFanSpeed());
  if (tmFaulted) s += " FAULT";
  return s;
}

void initThermal(void) {
  tempSensorPresent = tmProbeSensor();
  if (!tempSensorPresent) Serial.println("Cannot find DS18B20 sensor");

  tmState = TM_IDLE;
  tmStateSinceMs = millis() - TM_MIN_OFF_MS;  // no dwell to wait out at boot
  tmLastSampleMs = millis() - TM_SAMPLE_MS;   // sample on the first loop
  tmInteg = 0.0f;
  tmProp = 0.0f;
  tmDemand = 0.0f;
  tmTempFilt = NAN;
  tmBadReads = 0;
  tmFaulted = false;
  tmEnabled = false;
  tmBelowStop = false;
  tmAlwaysRun = TM_ALWAYS_RUN;
  tmSetpoint = TEMP_SETPOINT;
}
