#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Thermal management
//
// Owns the DS18B20 and drives the fan so the measured temperature settles at
// TEMP_SETPOINT. The controller is a PI: the integral term is what makes it
// quiet, since it removes the steady-state error a plain curve or P controller
// would need in order to hold any given speed, so the fan ends up at exactly
// the lowest duty that maintains the setpoint under the current load. There is
// no D term - the sensor runs at 9 bits (0.5C steps) against a plant with a
// time constant of minutes, so a derivative would mostly turn quantization
// noise into audible duty jitter. An EMA on the reading damps the same noise in
// the P term.
//
// The fan cannot run below TM_MIN_RUN_DUTY and needs SAFE_START_DUTY to start
// turning at all, so the 0% .. TM_MIN_RUN_DUTY gap is bridged by a small state
// machine (OFF / STARTING / RUNNING) rather than by the PI. Dwell times keep it
// from cycling across that gap, which is the one place bang-bang behaviour
// could creep back in. With "fan always on" the gap is simply never crossed:
// the fan starts once and stays at TM_MIN_RUN_DUTY however cold it gets.
//
// The setpoint and the always-on flag are runtime state; the macros below only
// give their power-on values. Everything else is compile time.
//
// The smoothness of the output itself comes from fan.cpp, which slews the duty
// at SETFANSPEED_RAMP_VPERSEC; this only commands targets.
// ---------------------------------------------------------------------------

// --- tuning ---------------------------------------------------------------

#define TEMP_SETPOINT       45.0f   // degC the controller maintains at power-on
#define TM_SETPOINT_MIN     20.0f   // range setThermalSetpoint() accepts
#define TM_SETPOINT_MAX     90.0f
#define SAFE_START_DUTY     30      // % duty the fan reliably starts spinning at
#define TM_MIN_RUN_DUTY     25      // % lowest duty it keeps spinning at once started
#define TM_MAX_DUTY         100     // % upper end of the control range

#define TM_KP               6.0f    // %/degC
#define TM_KI               0.30f   // %/(degC*s), integral-dominant for a slow plant

#define TM_SAMPLE_MS        1000    // control period, also the DS18B20 sample rate
#define TM_FILTER_ALPHA     0.3f    // EMA weight of a new reading, tames the 0.5C steps

#define TM_KICK_MS          1500    // hold SAFE_START_DUTY this long after a start

// Power-on value of the always-on flag, see setThermalAlwaysRun(). 1: the fan
// is never stopped. TM_MIN_RUN_DUTY becomes a hard floor the controller sits on
// whenever the load needs less air than that, the fan is started on the first
// control step instead of waiting for demand, and the whole stop/restart half
// of the state machine below is never entered.
#define TM_ALWAYS_RUN       0

// The stop band is deliberately wide and slow: a load the fan can just about
// handle at TM_MIN_RUN_DUTY makes the demand dip below the floor now and then,
// and riding those dips out keeps the fan turning steadily instead of cycling.
// Only a load that is genuinely too small to need any airflow gets through it.
#define TM_STOP_HYST        8       // % below TM_MIN_RUN_DUTY the demand must fall to
#define TM_STOP_DWELL_MS    30000  // ...and stay below, before the fan is stopped
#define TM_MIN_ON_MS        15000   // minimum spin time once started
#define TM_MIN_OFF_MS       10000   // minimum rest time once stopped
// Demand the integrator is parked at when the fan stops. The lower it is, the
// longer the temperature has to climb before the fan can come back, so this is
// what sets the off time when the load sits right at the edge of what the fan
// can resolve; 0 keeps those cycles as long and as few as possible.
#define TM_RESTART_SEED     0

// --- sensor ---------------------------------------------------------------

#define TM_FAULT_DUTY       100     // fail-safe duty when the sensor stops answering
#define TM_FAULT_READS      3       // consecutive bad reads before that kicks in
#define TM_TEMP_MIN        -40.0f   // readings outside the DS18B20 range are bad reads
#define TM_TEMP_MAX         125.0f

// --- API ------------------------------------------------------------------

// Probe the DS18B20 and reset the controller. Leaves management disabled.
void initThermal(void);

// Run one control step if TM_SAMPLE_MS has elapsed, to be called from loop().
// Blocks for the length of a conversion (~94ms at 9 bits) on the steps where it
// samples, so this is the only place the sensor is read.
void updateThermal(void);

// Turn the controller on or off. Both directions are bumpless: enabling picks up
// the fan where it currently is, disabling leaves it running at its last speed.
void setThermalEnabled(bool on);
bool getThermalEnabled(void);

// Keep the fan spinning at TM_MIN_RUN_DUTY instead of stopping it when the load
// needs less air than that. Turning it on starts the fan on the next control
// step; turning it off does not stop the fan, it only re-arms the stop band, so
// the usual dwell times still have to pass first.
void setThermalAlwaysRun(bool on);
bool getThermalAlwaysRun(void);

// Temperature the controller holds, clamped to TM_SETPOINT_MIN..TM_SETPOINT_MAX.
// A change is a step in the reference and the P term answers it as such, which
// is the point; the ramp in fan.cpp keeps that from being audible as a jump.
void setThermalSetpoint(float degC);
float getThermalSetpoint(void);

// Whether a DS18B20 was found at startup.
bool hasTempSensor(void);

// Last filtered reading in degC, NAN before the first good sample.
float getTemperature(void);

// One line of controller state, for tuning over BLE.
String getThermalStatus(void);
