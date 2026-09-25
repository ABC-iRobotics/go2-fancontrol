#include "fan.h"

#define SETFANSPEED_RAMP_VPERSEC 5.0f
#define FAN_SUPPLY_VOLTS 12.0f
#define FAN_PWM_FREQ_HZ 100000

// LEDC builds the PWM period by dividing its source clock by 2^bits, so the
// resolution is capped at log2(clock / FAN_PWM_FREQ_HZ). The core would pick
// the 40MHz XTAL on the C3, which leaves only 8 bits at 100kHz; the 80MHz APB
// clock buys one more, halving the ramp step. Lowering FAN_PWM_FREQ_HZ buys
// more still - the filter sits far enough above its corner for 25kHz, which
// would allow 11 bits - so the resolution is probed at startup rather than
// hardcoded, and a frequency change needs no edit here.
#define FAN_PWM_MAX_BITS 14  // SOC_LEDC_TIMER_BIT_WIDTH on the C3
#define FAN_PWM_MIN_BITS 8

static uint8_t fanPwmPin = 5;
static uint8_t fanPercent = 0;  // last commanded speed, i.e. the ramp target

// Ramp state, in raw duty units of 0..fanDutyMax. Only setFanSpeed() writes
// fanDutyTarget (a single aligned word, so the BLE task can set it without
// locking); everything else belongs to the loop task.
static uint32_t fanDutyMax = (1UL << FAN_PWM_MIN_BITS) - 1;
static uint32_t fanDutyTarget = 0;    // where the ramp is heading
static uint32_t fanDutyCurrent = 0;   // what is on the pin right now
static uint32_t fanRampToDuty = 0;    // target the running ramp was started for
static uint32_t fanRampFromDuty = 0;  // duty the running ramp started at
static uint32_t fanRampStartMs = 0;   // millis() when it started
// Slew rate as duty steps per second, scaled by 1000 so that rates below one
// step per second survive the integer math. 0 means "no ramp".
static uint32_t fanRampMilliStepsPerSec = 0;

static void fanWriteDuty(const uint32_t duty) {
  fanDutyCurrent = duty;
  ledcWrite(fanPwmPin, duty);
}

void setFanSpeed(const uint8_t percent) {
  fanPercent = percent;
  uint32_t duty = (uint32_t)percent * fanDutyMax / 100;
  Serial.print("fan set: ");
  Serial.print(percent);
  Serial.print("% -> duty ");
  Serial.print(duty);
  Serial.print("/");
  Serial.println(fanDutyMax);
  fanDutyTarget = duty;
}

uint8_t getFanSpeed(void) {
  return fanPercent;
}

// Each ramp is computed from the duty and the instant it started at instead of
// from a per-tick increment, so rates slower than one step per tick accumulate
// rather than being rounded away.
void updateFanRamp(void) {
  uint32_t target = fanDutyTarget;  // may change under us, so read it once

  if (target != fanRampToDuty) {  // new target: start a ramp towards it
    fanRampToDuty = target;
    fanRampFromDuty = fanDutyCurrent;
    fanRampStartMs = millis();
  }
  if (fanDutyCurrent == target) return;

  if (fanRampMilliStepsPerSec == 0) {  // ramp disabled, go there in one step
    fanWriteDuty(target);
    return;
  }

  // Unsigned subtraction wraps the right way around a millis() overflow.
  uint32_t elapsed = millis() - fanRampStartMs;
  uint32_t travelled = (uint32_t)(((uint64_t)elapsed * fanRampMilliStepsPerSec) / 1000000ULL);

  uint32_t duty;
  if (target >= fanRampFromDuty) {
    uint32_t span = target - fanRampFromDuty;
    duty = fanRampFromDuty + ((travelled >= span) ? span : travelled);
  } else {
    uint32_t span = fanRampFromDuty - target;
    duty = fanRampFromDuty - ((travelled >= span) ? span : travelled);
  }

  if (duty == fanDutyCurrent) return;  // not a full step yet
  fanWriteDuty(duty);
  if (duty == target) {
    Serial.print("fan ramp done, duty: ");
    Serial.println(duty);
  }
}

void initFan(void) {
  pinMode(fanPwmPin, OUTPUT);
  // Must happen before the first channel is attached, and gets us 9 bits at
  // 100kHz instead of the 8 the default XTAL clock would allow.
  ledcSetClockSource(LEDC_USE_APB_CLK);

  // The LEDC driver rejects a resolution that does not fit the frequency, so
  // walk down until one is accepted instead of guessing the clock in advance.
  uint8_t bits;
  for (bits = FAN_PWM_MAX_BITS; bits >= FAN_PWM_MIN_BITS; bits--) {
    if (ledcAttach(fanPwmPin, FAN_PWM_FREQ_HZ, bits)) break;
  }
  if (bits < FAN_PWM_MIN_BITS) {
    Serial.println("Fan PWM setup failed, fan stays off");
    return;
  }
  fanDutyMax = (1UL << bits) - 1;

  // volts/s -> duty steps/s, times 1000 for the fixed-point math in the ramp
  fanRampMilliStepsPerSec =
    (uint32_t)(SETFANSPEED_RAMP_VPERSEC / FAN_SUPPLY_VOLTS * fanDutyMax * 1000.0f);

  Serial.print("fan PWM: ");
  Serial.print(FAN_PWM_FREQ_HZ);
  Serial.print("Hz, ");
  Serial.print(bits);
  Serial.print(" bits, ramp ");
  Serial.print(SETFANSPEED_RAMP_VPERSEC);
  Serial.print("V/s = ");
  Serial.print(fanRampMilliStepsPerSec / 1000.0f);
  Serial.println(" steps/s");

  fanWriteDuty(0);
  setFanSpeed(0);
}
