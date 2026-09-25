#include "ble.h"
#include "fan.h"
#include "tm.h"

static uint32_t bleLastAdvUpdate = 0;

// ---------------------------------------------------------------------------
// Command interface
//
// A command line is "<cmd> <arg>", where <cmd> is at most CMD_NAME_MAX
// characters and everything after the first space is the argument. The argument
// may be empty ("gf" and "gf " are equivalent). A handler is free to split its
// argument further, which is how "tm" gets its sub-commands. handleCommand() is
// registered with initBle(), which feeds it the writes to the characteristic
// and hands the reply back to the client.
// ---------------------------------------------------------------------------

#define CMD_NAME_MAX 10

typedef void (*CommandHandler)(const String &arg);

struct Command {
  const char *name;
  CommandHandler handler;
};

static String cmdReply;

// Callbacks answer with this; the reply ends up in the characteristic value.
static void reply(const String &text) {
  cmdReply = text;
}

// Parse a decimal unsigned number that fills the whole string. Returns false on
// an empty string, stray characters or a value outside 0..max. String::toInt()
// would be shorter but returns 0 for unparseable input, so "sf abc" would look
// like a request for 0%.
static bool parseUint(const String &arg, uint32_t max, uint32_t *out) {
  char *end;
  long value = strtol(arg.c_str(), &end, 10);
  // end == c_str() means no digits at all, *end != 0 means trailing garbage
  if (end == arg.c_str() || *end != '\0') return false;
  if (value < 0 || value > (long)max) return false;
  *out = (uint32_t)value;
  return true;
}

// Same for a decimal number with an optional fraction, within min..max.
static bool parseFloat(const String &arg, float lo, float hi, float *out) {
  char *end;
  float value = strtof(arg.c_str(), &end);
  if (end == arg.c_str() || *end != '\0') return false;
  if (isnan(value) || value < lo || value > hi) return false;
  *out = value;
  return true;
}

// Split "<word> <rest>" off the front of an argument. Returns the word and
// leaves the remainder, trimmed, in *rest.
static String splitWord(const String &arg, String *rest) {
  int space = arg.indexOf(' ');
  if (space < 0) {
    *rest = "";
    return arg;
  }
  String tail = arg.substring(space + 1);
  tail.trim();
  *rest = tail;
  return arg.substring(0, space);
}

// "gf" - get fan speed in percent
static void cmdGetFan(const String &arg) {
  reply(String(getFanSpeed()));
}

// "sf <0..100>" - set fan speed in percent
static void cmdSetFan(const String &arg) {
  uint32_t percent;
  if (!parseUint(arg, 100, &percent)) {
    reply("ERR sf wants 0..100");
    return;
  }
  // A manual speed would just be overwritten on the next control step, so
  // asking for one means asking for the controller to get out of the way.
  bool wasThermal = getThermalEnabled();
  setThermalEnabled(false);
  setFanSpeed((uint8_t)percent);
  reply("OK " + String(getFanSpeed()) + (wasThermal ? " (tm off)" : ""));
}

// "tm <0|1>" / "tm en <0|1>" - turn thermal management off/on
// "tm fon <0|1>"              - fan always on, see setThermalAlwaysRun()
// "tm sp <degC>"              - temperature setpoint
static void cmdThermalMgmt(const String &arg) {
  String value;
  String sub = splitWord(arg, &value);

  if (sub == "fon") {
    uint32_t on;
    if (!parseUint(value, 1, &on)) {
      reply("ERR tm fon wants 0 or 1");
      return;
    }
    setThermalAlwaysRun(on != 0);
    reply("OK fon " + String(getThermalAlwaysRun() ? 1 : 0));
    return;
  }

  if (sub == "sp") {
    float sp;
    if (!parseFloat(value, TM_SETPOINT_MIN, TM_SETPOINT_MAX, &sp)) {
      reply("ERR tm sp wants " + String(TM_SETPOINT_MIN, 0) + ".." +
            String(TM_SETPOINT_MAX, 0));
      return;
    }
    setThermalSetpoint(sp);
    reply("OK sp " + String(getThermalSetpoint(), 1));
    return;
  }

  // Bare "tm <0|1>", kept as the short form of "tm en <0|1>".
  String enArg = (sub == "en") ? value : arg;
  uint32_t on;
  if (!parseUint(enArg, 1, &on)) {
    reply("ERR tm wants en|fon <0|1>, sp <degC>");
    return;
  }
  setThermalEnabled(on != 0);
  reply("OK " + String(getThermalEnabled() ? 1 : 0));
}

// "ts" - thermal status, one line of controller state for tuning
static void cmdThermalStatus(const String &arg) {
  reply(getThermalStatus());
}

static const Command commands[] = {
  {"gf", cmdGetFan},
  {"sf", cmdSetFan},
  {"tm", cmdThermalMgmt},
  {"ts", cmdThermalStatus},
};

// Split the line into command and argument, then run the matching handler.
// Returns the reply to hand back to the client.
static String handleCommand(const String &line) {
  String trimmed = line;
  trimmed.trim(); // clients like to append CR/LF

  int space = trimmed.indexOf(' ');
  String name = (space < 0) ? trimmed : trimmed.substring(0, space);
  String arg = (space < 0) ? String("") : trimmed.substring(space + 1);

  if (name.length() == 0) return "ERR empty command";
  if (name.length() > CMD_NAME_MAX) return "ERR unknown command";

  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (name == commands[i].name) {
      cmdReply = "OK";
      commands[i].handler(arg);
      return cmdReply;
    }
  }
  return "ERR unknown command: " + name;
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // Serial is the native USB CDC port here, which only exists once the host
  // opens it. Wait briefly so the boot messages below are not lost, then stop
  // blocking on writes for when no serial monitor is attached.
  while (!Serial && millis() < 3000) {
    delay(10);
  }
  Serial.setTxTimeoutMs(0);
#endif

  initThermal();
  initFan();
  initBle(handleCommand);
  setThermalEnabled(true);
}

void loop() {
  if (millis() - bleLastAdvUpdate >= ADV_UPDATE_MS) {
    bleLastAdvUpdate = millis();
    String payload = String(getTemperature(), 1) + "C";
    if(hasTempSensor()) {
      payload += "|" + String(getFanSpeed()) + "%";
    }
    publishAdvString(payload);
  }
  updateThermal();
  updateFanRamp();
  delay(10);
}
