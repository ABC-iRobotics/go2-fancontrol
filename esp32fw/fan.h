#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Fan output
//
// The FET chops the 12V rail into the LC filter so the fan sees
// a DC voltage of duty * FAN_SUPPLY_VOLTS. A duty step
// rings that filter, so setFanSpeed() only moves a target and the duty is
// slewed towards it from updateFanRamp(), by at most SETFANSPEED_RAMP_VPERSEC
// volts of fan voltage per second. 0 turns the ramp off and applies the speed
// at once.
// ---------------------------------------------------------------------------

// Set up the PWM output. Leaves the fan stopped.
void initFan(void);

// Set the speed the fan should end up at. The output gets there over time, see
// updateFanRamp(); this only publishes the target, so it is safe to call from
// the BLE callback task.
void setFanSpeed(uint8_t percent);

// Last commanded speed in percent, i.e. the ramp target rather than what is on
// the pin right now.
uint8_t getFanSpeed(void);

// Move the fan output towards the target, to be called from loop().
void updateFanRamp(void);
