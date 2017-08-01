#include <LowPower.h>
#include <Wire.h> 
#include "RTClib.h"

/// self test
const bool SELF_TEST = true;
/// continuous test
/// WARNING: must remain false !!!
const bool CONTINUOUS_TEST = false;
/// debug mode
const bool DEBUG = true;
/// compile-time clock setting, goes like this:
/// 1. set it to true, compile the sketch and write it to the MCU
/// 2. reset the MCU and run it once, 
/// 3. set it to false, compile the sketch and write it to the MCU
const bool CLOCK_SETUP = false;

/// irrigation period: once every IRRIGATION_PERIOD_DAYS days
const int IRRIGATION_PERIOD_DAYS = 2;
/// irrigation day: between 0 and IRRIGATION_PERIOD_DAYS-1
const int IRRIGATION_DAY = 0;
/// irrigation begin hour: between 0 and 23
const int IRRIGATION_BEGIN_HOUR = 20;
/// irrigation end hour: between 0 and 23
const int IRRIGATION_END_HOUR = 20;
/// irrigation begin minute: between 1 and 60
/// SEEME: why 1-60 and not 0-59 ?!
const int IRRIGATION_BEGIN_MINUTE = 10;
/// irrigation end minute: between 1 and 60
/// SEEME: why 1-60 and not 0-59 ?!
const int IRRIGATION_END_MINUTE = 50;
/// alternating the cascades
const bool CASCADE_ALTERNATION = false;
/// fill just the small cascade
const bool SMALL_CASCADE_ONLY = false;
/// water tank level probing interval in case of low water level [seconds]
const uint32_t WARNING_TANK_LEVEL_PROBING_INTERVAL = 10;

/// pump motor port
const int PUMP_MOTOR_PORT = 11;
/// LED signaling port
const int SIGNALING_LED_PORT = 13;
/// analog input port: empty tank
const int EMPTY_TANK_PORT = A0;
/// analog input port: big cascade full
const int BIG_CASCADE_FULL_PORT = A1;
/// analog input port: small cascade full
const int SMALL_CASCADE_FULL_PORT = A2;

/// clock
RTC_DS1307 RTC;
/// temporal probe
DateTime time;
/// machine state
int state;
/// the unix timestamp of the last water level check in case of low water level
uint32_t tsProbeWarningTankLevel;
/// LED warning state
int stateWarningLED;
/// the unix timestamp of the warning LED state change
uint32_t tsChangeWarningLED;
/// indicates that the state machine needs to sleep longer between iterations
bool bSleepLonger;


void setup() {
  // 
  Wire.begin();
  RTC.begin();
  if (CLOCK_SETUP) {
    // sets the clock to the compile date and time
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }
  Serial.begin(9600);
  pinMode(PUMP_MOTOR_PORT, OUTPUT);
  pinMode(SIGNALING_LED_PORT, OUTPUT);
  pinMode(EMPTY_TANK_PORT, INPUT);
  pinMode(BIG_CASCADE_FULL_PORT, INPUT);
  pinMode(SMALL_CASCADE_FULL_PORT, INPUT);
  tsProbeWarningTankLevel = 0;
  stateWarningLED = 0;
  tsChangeWarningLED = 0;
  bSleepLonger = false;
  state = 0;
}

void trace(const String& message) {
  //
  Serial.print(time.year(), DEC);
  Serial.print('/');
  Serial.print(time.month(), DEC);
  Serial.print('/');
  Serial.print(time.day(), DEC);
  Serial.print(' ');
  Serial.print(time.hour(), DEC);
  Serial.print(':');
  Serial.print(time.minute(), DEC);
  Serial.print(':');
  Serial.print(time.second(), DEC);
  Serial.print(" - ");
  Serial.print(message);
  Serial.println();
}

void displayConfig() {
  // 
  Serial.print("Irigates once every ");
  Serial.print(IRRIGATION_PERIOD_DAYS, DEC);
  Serial.print(" days, at day ");
  Serial.println(IRRIGATION_DAY, DEC);
  Serial.print("today: ");
  if ((today() % IRRIGATION_PERIOD_DAYS) == IRRIGATION_DAY) {
    // 
    Serial.println("yes, ");
  } else {
    // 
    Serial.println("no, ");
  }
  Serial.print("today + 1: ");
  if (((today() + 1) % IRRIGATION_PERIOD_DAYS) == IRRIGATION_DAY) {
    // 
    Serial.println("yes, ");
  } else {
    // 
    Serial.println("no, ");
  }
  Serial.print("today + 2: ");
  if (((today() + 2) % IRRIGATION_PERIOD_DAYS) == IRRIGATION_DAY) {
    // 
    Serial.println("yes, ");
  } else {
    // 
    Serial.println("no, ");
  }
  Serial.print("today + 3: ");
  if (((today() + 3) % IRRIGATION_PERIOD_DAYS) == IRRIGATION_DAY) {
    // 
    Serial.println("yes,");
  } else {
    // 
    Serial.println("no,");
  }

  if (CASCADE_ALTERNATION) {
    // 
    Serial.print("alternating the cascades - today: ");
    if (((int)(today() / IRRIGATION_PERIOD_DAYS) % 2) == 0) {
      // 
      Serial.println("the small cascade, ");
    } else {
      // 
      Serial.println("the big cascade, ");
    }
    Serial.print("today + 1: ");
    if (((int)((today() + 1) / IRRIGATION_PERIOD_DAYS) % 2) == 0) {
      // 
      Serial.println("the small cascade, ");
    } else {
      // 
      Serial.println("the big cascade, ");
    }
    Serial.print("today + 2: ");
    if (((int)((today() + 2) / IRRIGATION_PERIOD_DAYS) % 2) == 0) {
      // 
      Serial.println("the small cascade, ");
    } else {
      // 
      Serial.println("the big cascade, ");
    }
    Serial.print("today + 3: ");
    if (((int)((today() + 3) / IRRIGATION_PERIOD_DAYS) % 2) == 0) {
      // 
      Serial.println("the small cascade.");
    } else {
      // 
      Serial.println("the big cascade.");
    }
  } else {
    // 
    if (SMALL_CASCADE_ONLY) {
      // 
      Serial.println(" just the small cascade,");
    } else {
      //
      Serial.println(" just the big cascade,");
    }
  }
  Serial.print(" between ");
  Serial.print(IRRIGATION_BEGIN_HOUR, DEC);
  Serial.print(":");
  Serial.print(IRRIGATION_BEGIN_MINUTE, DEC);
  Serial.print(" and ");
  Serial.print(IRRIGATION_END_HOUR, DEC);
  Serial.print(":");
  Serial.print(IRRIGATION_END_MINUTE, DEC);
  Serial.println(". ");
  Serial.print("Checking the tank every ");
  Serial.print(WARNING_TANK_LEVEL_PROBING_INTERVAL, DEC);
  Serial.print(" seconds. Today is day #: ");
  Serial.print(today(), DEC);  
  Serial.println(".");
}

bool active(int analogInput, int threshold = 500) {
  //
  // Setup:
  //       
  //             /  
  // [+5v] o----/  ----+---[10k]----o [GND]
  //                   |
  //                   o
  //             [analogInput]
  // On: 1023
  // Off: 0
  // Prag: 500
  int value = analogRead(analogInput);
  if (value > threshold) {
    // 
    return true;
  }
  // 
  return false;
}

bool tankEmpty() {
  //
  return active(EMPTY_TANK_PORT, 1020);
}

bool bigCascadeFull() {
  //
  return active(BIG_CASCADE_FULL_PORT, 800);
}

bool smallCascadeFull() {
  //
  return active(SMALL_CASCADE_FULL_PORT, 800);
}

int today() {
  // a consistent day representation
  return (int) (time.secondstime() / 86400) + 1;
}

bool cascadeFull() {
  // 
  if (CASCADE_ALTERNATION) {
    // alternating the big and small cascade filling
    if (((int)(today() / IRRIGATION_PERIOD_DAYS) % 2) == 0) {
      // the small cascade;
      // for safety, tanking into account the big cascade filling
      return smallCascadeFull() 
        || bigCascadeFull();
    }
    // the big cascade
    return bigCascadeFull();
  }
  // filling just a cascade
  if (SMALL_CASCADE_ONLY) {
    // just the small one;
    // for safety, tanking into account the big cascade filling
    return smallCascadeFull() 
      || bigCascadeFull();
  }
  // just the big one
  return bigCascadeFull();
}

bool irrigationInterval() {
  // 
  if ((today() % IRRIGATION_PERIOD_DAYS) != IRRIGATION_DAY) {
    // not the irrigation day
    return false;
  }
  // the irrigation day
  if ((time.hour() < IRRIGATION_BEGIN_HOUR) 
    || (time.hour() > IRRIGATION_END_HOUR)) {
    // outside the irrigation hours
    return false;
  }
  // within the irrigation hours
  if ((time.minute() < IRRIGATION_BEGIN_MINUTE) 
    || (time.minute() > IRRIGATION_END_MINUTE)) {
    // outside the irrigation minutes
    return false;
  }
  // within the irrigation interval
  return true;
}

void tankEmptyWarning() {
  // 
  uint32_t timestamp = time.unixtime();
  if (timestamp - tsChangeWarningLED > 1) {
    // time to change the LED state
    tsChangeWarningLED = timestamp;
    stateWarningLED = !stateWarningLED;
    digitalWrite(SIGNALING_LED_PORT, stateWarningLED ? HIGH : LOW);
  }
}

bool execute() {
  //
  time = RTC.now();
  bSleepLonger = false;
  
  if (state == 0) {
    // init
    trace("System initialization (in 10 secunds).");
    delay(10000);
    displayConfig();
    trace("System start.");
    if (SELF_TEST) {
      // with self-testing
      state = 10;
    } else {
      // without self-testing
      state = 1;
    }
  }
  if (state == 10) {
    // self-test: empty tank LED on
    trace("Test: Start");
    digitalWrite(SIGNALING_LED_PORT, HIGH);
    delay(1000);
    state = 11;
  }
  if (state == 11) {
    // self-test: empty tank LED off
    digitalWrite(SIGNALING_LED_PORT, LOW);
    delay(1000);
    state = 12;
  }
  if (state == 12) {
    // self-test: empty tank LED on
    digitalWrite(SIGNALING_LED_PORT, HIGH);
    delay(1000);
    state = 13;
  }
  if (state == 13) {
    // self-test: empty tank LED off
    digitalWrite(SIGNALING_LED_PORT, LOW);
    delay(1000);
    state = 14;
  }
  if (state == 14) {
    // self-test: pump motor on
    digitalWrite(PUMP_MOTOR_PORT, HIGH);
    delay(8000);
    state = 15;
  }
  if (state == 15) {
    // self-test: pump motor off
    digitalWrite(PUMP_MOTOR_PORT, LOW);
    state = 16;
  }
  if (state == 16) {
    // self-test: testing cascade full switches
    for (int i = 0; i < 30; i ++) {
      // 
      int cnt = 0;
      if (smallCascadeFull()) {
        // 
        trace("Test: The small cascade is full.");
        cnt ++;
      }
      if (bigCascadeFull()) {
        //
        trace("Test: The big cascade is full.");
        cnt ++;
      }
      if (tankEmpty()) {
        //
        trace("Test: The water tank is empty.");
        cnt ++;
      }
      if (cnt == 0) {
        // 
        trace("Test: All switches are open.");
      }
      delay(1000);
    }
    if (CONTINUOUS_TEST) {
      // 
      state = 16;
    } else {
      trace("Test: Stop");
      digitalWrite(SIGNALING_LED_PORT, HIGH);
      delay(3000);
      digitalWrite(SIGNALING_LED_PORT, LOW);
      state = 1;
    }
  }
  
  
  if (state == 1) {
    // tests
    if (tankEmpty()) {
      // 
      trace("The water tank is empty!");
      state = 100;
    } 
    else {
      // the water tank is not empty
      if (irrigationInterval()) {
        // 
        trace("It is the irrigation time.");
        state = 2;
      }
      else {
        // other
        bSleepLonger = true;
      }
    }
  }
  if (state == 2) {
    // 
    trace("Starting the pump motor.");
    digitalWrite(PUMP_MOTOR_PORT, HIGH);
    state = 3;
  }
  if (state == 3) {
    // water level testing
    if (tankEmpty()) {
      // 
      trace("Water tank got empty! Emergency stop.");
      digitalWrite(PUMP_MOTOR_PORT, LOW);
      state = 100;
    }
    else if (cascadeFull()) {
      // 
      trace("The water cascade filled-up.");
      state = 4;
    }
    else if (!irrigationInterval()) {
      // 
      trace("The irrigation time is up.");
      state = 4;
    }
    else {
      // the bottle cascade is being filled
    }
  }
  if (state == 4) {
    // 
    trace("Stopping the pump motor.");
    digitalWrite(PUMP_MOTOR_PORT, LOW);
    state = 5;
  }
  if (state == 5) {
    // wating for the irrigation time to pass
    if (!irrigationInterval()) {
      // 
      trace("The irrigation time has passed.");
      state = 1;
    }
  }
  
  // WARNING
  if (state == 100) {
    // water tank is empty
    tsProbeWarningTankLevel = time.unixtime();
    state = 101;
  }
  if (state == 101) {
    // testing water tank level
    uint32_t timestamp = time.unixtime();
    tankEmptyWarning();
    if (timestamp - tsProbeWarningTankLevel > WARNING_TANK_LEVEL_PROBING_INTERVAL) {
      // time to test the water tank level
      if (tankEmpty()) {
        // the tank is still empty
        state = 100;
      } else {
        // 
        digitalWrite(SIGNALING_LED_PORT, LOW);
        trace("The tank is no longer empty.");
        state = 1;
      }
    }
  }
  return bSleepLonger;
}

void loop() {
  // 
  if (execute()) {
    // should sleep longer
    if (DEBUG) {
      // 
      trace("Hibernating for 8 seconds.");
      delay(8000);
      trace("Out of the hibernation.");
    } else {
      //
      LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    }
  } else {
    // should sleep 1 second
    if (DEBUG) {
      //
      delay(1000);
    } else {
      // 
      LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
    }
  }
}
