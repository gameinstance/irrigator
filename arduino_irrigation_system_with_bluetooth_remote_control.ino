#include <EEPROM.h>
#include <LowPower.h>
#include <Wire.h>
#include <SerialCommand.h>
#include "RTClib.h"

/// self test
const bool SELF_TEST = true;
/// debug mode
const bool DEBUG = false;
/// continuous test
/// WARNING: must remain false !!!
const bool CONTINUOUS_TEST = false;
/// compile-time clock setting, goes like this:
/// 1. set it to true, compile the sketch and write it to the MCU
/// 2. reset the MCU and run it once, 
/// 3. set it to false, compile the sketch and write it to the MCU
const bool CLOCK_SETUP = false;

/// water tank level probing interval in case of low water level [seconds]
const uint32_t WARNING_TANK_LEVEL_PROBING_INTERVAL = 10;

/// logic output port: pump motor
const int PUMP_MOTOR_PORT = 11;
/// logic output port: LED signaling
const int SIGNALING_LED_PORT = 13;
/// logic input port: bluetooth power
const int BLUETOOTH_POWER_PORT = 12;
/// logic input port: bluetooth connection
const int BLUETOOTH_CONNECTION_PORT = 10;
/// analog input port: empty tank
const int EMPTY_TANK_PORT = A0;
/// analog input port: big cascade full
const int BIG_CASCADE_FULL_PORT = A1;
/// analog input port: small cascade full
const int SMALL_CASCADE_FULL_PORT = A2;


/// real-time clock object
RTC_DS1307 RTC;
/// temporal probe
DateTime time;
/// machine state
int state;
/// the unix timestamp of the last water level check in case of low water level
uint32_t tsProbeWarningTankLevel;
/// LED warning state
bool stateWarningLED;
/// the unix timestamp of the warning LED state change
uint32_t tsChangeWarningLED;


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

bool active(int analogInput, int threshold = 512) {
  //
  // Setup:
  //       
  //             /  
  // [GND] o----/  ----+---[10k]----o [Vcc]
  //                   |
  //                   o
  //             [analogInput]
  //
  int value = analogRead(analogInput);
  if (value < threshold) {
    // 
    return true;
  }
  // 
  return false;
}

bool tankEmpty() {
  //
  return active(EMPTY_TANK_PORT, 100);
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

void tankEmptyWarning() {
  // 
  uint32_t timestamp = time.unixtime();
  if (timestamp > tsChangeWarningLED + 1) {
    // time to change the LED state
    tsChangeWarningLED = timestamp;
    stateWarningLED = !stateWarningLED;
    digitalWrite(SIGNALING_LED_PORT, stateWarningLED ? HIGH : LOW);
  }
}

bool lowPowerMode() {
  // 
  if (!digitalRead(BLUETOOTH_POWER_PORT)) {
    // the bluetooth device is powered
    return false;
  }
  // no bluetooth power
/*
  if (digitalRead(BLUETOOTH_CONNECTION_PORT)) {
    // the bluetooth device is connected
    return false;
  }
  // no bluetooth connection
*/
  if (DEBUG) {
    // system under debugging mode
    return false;
  }
  // no reason not to go low power
  return true;
}

bool wait(int ms) {
  // 
  static unsigned long ts = 0;
  static bool delayStarted = false;
  if (!delayStarted) {
    // 
    delayStarted = true;
    ts = millis() + ms;
    return true;
  }
  // 
  if (millis() >= ts) {
    // 
    delayStarted = false;
    return false;
  }
  // 
  return true;
}


/*
 * The app specific configuration class.
 */
class MyConfig {

  public:
  
    /// default constructor
    MyConfig() {
      // 
    };
    /// destructor
    virtual ~MyConfig() {
      // 
    };

    /// loads interval data
    bool LoadInterval() {
      // 
      byte value0 = EEPROM.read(0);
      byte value1 = EEPROM.read(1);
      if (!ValidPeriodAndDay(value0, value1)) {
        // 
//        Serial.println("11");
        return false;
      }
      // loading the irrigation period and day
      irrigationPeriodDays = value0;
      irrigationDay = value1;
      
      // loading the cascade alternation flag
      cascadeAlternation = (bool) EEPROM.read(2);
      
      // loading the small cascade only flag
      smallCascadeOnly = (bool) EEPROM.read(3);

      return true;
    }
    /// loads the hour data
    bool LoadHour() {
      // 
      byte value;
      // loading the begin hour
      value = EEPROM.read(4);
      if (!ValidHour(value)) {
        // invalid hour
//        Serial.println("21");
        return false;
      }
      beginHour = value;

      // loading the begin minute
      value = EEPROM.read(5);
      if (!ValidMinute(value)) {
        // invalid minute
//        Serial.println("22");
        return false;
      }
      beginMinute = value;

      // loading the end hour
      value = EEPROM.read(6);
      if (!ValidHour(value)) {
        // invalid hour
//        Serial.println("23");
        return false;
      }
      endHour = value;
      
      // loading the end minute
      value = EEPROM.read(7);
      if (!ValidMinute(value)) {
        // invalid minute
//        Serial.println("24");
        return false;
      }
      endMinute = value;

      return true;
    }
    /// loads the config from EEPROM
    bool Load() {
      // 
      return LoadInterval() && LoadHour();
    };
    /// sets the defaults
    void Default() {
      // 
      Serial.println("Applying defaults.");
      irrigationPeriodDays = 2;
      irrigationDay = 0;
      cascadeAlternation = false;
      smallCascadeOnly = false;
      beginHour = 19;
      endHour = 19;
      beginMinute = 15;
      endMinute = 55;
    };
    /// sets the period data
    bool SetPeriod(byte var[5]) {
      // the first index contains the instruction
      if (!ValidPeriodAndDay(var[1], var[2])) {
        // 
        return false;
      }
      //
      irrigationPeriodDays = var[1];
      irrigationDay = var[2];
      cascadeAlternation = (bool) var[3];
      smallCascadeOnly = (bool) var[4];
      return true;
    };
    /// sets the hours
    bool SetHour(byte var[5]) {
      // the first index contains the instruction
      if (!ValidHour(var[1])
        || !ValidHour(var[3])
        || !ValidMinute(var[2])
        || !ValidMinute(var[4])) {
        // invalid times
        Serial.println(1);
        return false;
      }
      // 
      if ((var[1] == 0)
        && (var[2] == 0)
        && (var[3] == 0)
        && (var[4] == 0)) {
        // all data zero: rather ill-formed command than intentionate
        Serial.println(2);
        return false;
      }
      //
      if ((int) (var[1] * 60 + var[2]) > (int)(var[3]) * 60 + var[4]) {
        // incoherent times
        Serial.println(3);
        return false;
      }
      //
      beginHour = var[1];
      beginMinute = var[2];
      endHour = var[3];
      endMinute = var[4];
      return true;
    };
    /// saves the period
    bool SavePeriod() {
      // 
      EEPROM.write(0, (byte) irrigationPeriodDays);
      EEPROM.write(1, (byte) irrigationDay);
      EEPROM.write(2, (byte) cascadeAlternation);
      EEPROM.write(3, (byte) smallCascadeOnly);
    };
    /// saves the hour data
    bool SaveHour() {
      // 
      EEPROM.write(4, (byte) beginHour);
      EEPROM.write(5, (byte) beginMinute);
      EEPROM.write(6, (byte) endHour);
      EEPROM.write(7, (byte) endMinute);
    }
    /// saves the config to EEPROM
    bool Save() {
      // 
      return SavePeriod() && SaveHour();
    };
    /// dumps the config
    void Dump() {
      // 
      Serial.print("Period (days):");
      Serial.println(irrigationPeriodDays);
      Serial.print("Day:");
      Serial.println(irrigationDay);
      Serial.print("Hours ");
      Serial.print(beginHour, DEC);
      Serial.print(":");
      Serial.print(beginMinute, DEC);
      Serial.print("-");
      Serial.print(endHour, DEC);
      Serial.print(":");
      Serial.print(endMinute, DEC);
      Serial.println(". ");
      Serial.print("Today:");
      if ((today() % irrigationPeriodDays) == irrigationDay) {
        // 
        Serial.println("Y");
      } else {
        // 
        Serial.println("N");
      }

      Serial.print("Altern:");
      Serial.println(cascadeAlternation ? "Y" : "N");
      Serial.print("Small casc only:");
      Serial.println(smallCascadeOnly ? "Y" : "N");
    };
    /// prints the human readable config to serial port
    void Display() {
      // 
      Serial.print("Irigates every ");
      Serial.print(irrigationPeriodDays, DEC);
      Serial.print(" days, at day ");
      Serial.println(irrigationDay, DEC);
      Serial.print("today: ");
      if ((today() % irrigationPeriodDays) == irrigationDay) {
        // 
        Serial.println("Y, ");
      } else {
        // 
        Serial.println("N, ");
      }
      Serial.print("today + 1: ");
      if (((today() + 1) % irrigationPeriodDays) == irrigationDay) {
        // 
        Serial.println("Y, ");
      } else {
        // 
        Serial.println("N, ");
      }
      Serial.print("today + 2: ");
      if (((today() + 2) % irrigationPeriodDays) == irrigationDay) {
        // 
        Serial.println("Y, ");
      } else {
        // 
        Serial.println("N, ");
      }
      Serial.print("today + 3: ");
      if (((today() + 3) % irrigationPeriodDays) == irrigationDay) {
        // 
        Serial.println("Y,");
      } else {
        // 
        Serial.println("N,");
      }
    
      if (cascadeAlternation) {
        // 
        Serial.print("alternating cascs - today: ");
        if (((int)(today() / irrigationPeriodDays) % 2) == 0) {
          // 
          Serial.println("small casc, ");
        } else {
          // 
          Serial.println("big casc, ");
        }
        Serial.print("today + 1: ");
        if (((int)((today() + 1) / irrigationPeriodDays) % 2) == 0) {
          // 
          Serial.println("small casc, ");
        } else {
          // 
          Serial.println("big casc, ");
        }
        Serial.print("today + 2: ");
        if (((int)((today() + 2) / irrigationPeriodDays) % 2) == 0) {
          // 
          Serial.println("small casc, ");
        } else {
          // 
          Serial.println("big casc, ");
        }
        Serial.print("today + 3: ");
        if (((int)((today() + 3) / irrigationPeriodDays) % 2) == 0) {
          // 
          Serial.println("small casc.");
        } else {
          // 
          Serial.println("big casc.");
        }
      } else {
        // 
        if (smallCascadeOnly) {
          // 
          Serial.println(" just the small casc,");
        } else {
          //
          Serial.println(" just the big casc,");
        }
      }
      Serial.print(" between ");
      Serial.print(beginHour, DEC);
      Serial.print(":");
      Serial.print(beginMinute, DEC);
      Serial.print("-");
      Serial.print(endHour, DEC);
      Serial.print(":");
      Serial.print(endMinute, DEC);
      Serial.println(". ");
      Serial.print("Checking tank every ");
      Serial.print(WARNING_TANK_LEVEL_PROBING_INTERVAL, DEC);
      Serial.print(" seconds. Today is #");
      Serial.print(today(), DEC);  
      Serial.println(".");
    }
    
    /// once every irrigationPeriodDays days
    byte irrigationPeriodDays;
    /// between 0 and irrigationPeriodDays-1
    byte irrigationDay;
    /// alternating the cascades
    bool cascadeAlternation;
    /// fill just the small cascade
    bool smallCascadeOnly;
    /// irrigation begin hour: between 0 and 23
    byte beginHour;
    /// irrigation end hour: between 0 and 23
    byte endHour;
    /// irrigation begin minute: between 0 and 59
    byte beginMinute;
    /// irrigation end minute: between 0 and 59
    byte endMinute;


  private:

    /// validates hour
    bool ValidHour(byte value) {
      //
      return ((value >= 0) && (value <= 23));
    }
    /// validates minute
    bool ValidMinute(byte value) {
      //
      return ((value >= 0) && (value <= 59));
    }
    /// validates period and day
    bool ValidPeriodAndDay(byte period, byte days) {
      //
      return ((period >= 1) && (period <= 10) && (days < period));
    }
};


/*
 * The app specific serial command class.
 */
class MySerialCommand : public SerialCommand {

  public:

    /// default constructor
    MySerialCommand() : SerialCommand() {
      // 
      pConfig = 0;
    }
    /// destructor
    virtual ~MySerialCommand() {
      // 
    }

    /// sets the config object pointer
    void SetConf(MyConfig* pC) {
      // 
      pConfig = pC;
    }

  protected:

    /// runs the command
    bool Run() {
      // 
      switch (data[0]) {
        // 
        case '?':
          // identify
          Identify();
          return true;
        case 'i':
          // store new config
          Parse();
          StorePeriod();
          return true;
        case 'h':
          // store new config
          Parse();
          StoreHour();
          return true;
        case 't':
          // performs self-test
          SelfTest();
          return true;
        case 'd':
          // dump the FSM state
          return Dump();
      }
      // unknown command
      Serial.println("Unkn cmd!");
      return false;
    }
    /// identifies the app
    void Identify() {
      // 
      Serial.println("Scheduled Off-Grid Irrigation System");
/*
      Serial.println("Commands:");
*/
      Serial.println("? - help");
      Serial.println("d - dump");
      Serial.println("t - test");
      Serial.println("i:X:Y:A:B - X=period, Y=start, A=altern, B=onlySmall");
      Serial.println("h:A:B:C:D - A=startHr, B=startMin, C=endHr, D=endMin");
    };
    /// parses the command data
    bool Parse() {
      // 
//      Serial.print("Command: ");
//      Serial.println(data);
      char buff[16] = {0};
      byte j = 0, k = 0;
      for (byte i = 1; /*data[i] != '\0'*/ i < index; i ++) {
        // 
        if (data[i] == ':') {
          // delimitor
          buff[j] = '\0';
          var[k] = atoi(buff);
          j = 0;
          k ++;
        } else {
          // acquire data
          buff[j ++] = data[i];
        }
      }
      buff[j] = '\0';
      var[k] = atoi(buff);
    }
    /// stores the period data
    void StorePeriod() {
      //
      if (!pConfig->SetPeriod(var)) {
        // 
        return;
      }
      pConfig->SavePeriod();
      state = 200;
    }
    /// stores the hours
    void StoreHour() {
      //
      if (!pConfig->SetHour(var)) {
        //
        return;
      }
      pConfig->SaveHour();
      state = 200;
    }
    /// self test
    void SelfTest() {
      //
      state = 10;
    }
    /// dumps the FSM state
    bool Dump() {
      // 
      trace("");
      Serial.println("-FSM DUMP-");
      Serial.print("State:");
      Serial.println(state);
      Serial.print("DBG:");
      Serial.println(DEBUG ? "Y" : "N");
      Serial.print("Today is #");
      Serial.print(today(), DEC);  
      Serial.println(".");
      pConfig->Dump();
      Serial.print("Empty:");
      Serial.println(tankEmpty() ? "Y" : "N");
      Serial.print("Big casc full:");
      Serial.println(bigCascadeFull() ? "Y" : "N");
      Serial.print("Small casc full:");
      Serial.println(smallCascadeFull() ? "Y" : "N");
      Serial.print("BT power:");
      Serial.println(digitalRead(BLUETOOTH_POWER_PORT) ? "N" : "Y");
      Serial.print("BT conn:");
      Serial.println(digitalRead(BLUETOOTH_CONNECTION_PORT) ? "Y" : "N");
      Serial.println("-EO DUMP-");
      return true;
    }

    /// a sixteen bytes config
    byte var[16];
    /// pointer to the config object
    MyConfig* pConfig;
};


/// app's configuration object
MyConfig conf;
/// app's serial command object
MySerialCommand sc;
/// general purpose index
int i = 0;

bool irrigationInterval() {
  // 
  if ((today() % conf.irrigationPeriodDays) != conf.irrigationDay) {
    // not the irrigation day
    return false;
  }
  // the irrigation day
  if ((time.hour() < conf.beginHour) 
    || (time.hour() > conf.endHour)) {
    // outside the irrigation hours
    return false;
  }
  // within the irrigation hours
  if ((time.minute() < conf.beginMinute) 
    || (time.minute() > conf.endMinute)) {
    // outside the irrigation minutes
    return false;
  }
  // within the irrigation interval
  return true;
}

bool cascadeFull() {
  // 
  if (conf.cascadeAlternation) {
    // alternating the big and small cascade filling
    if (((int)(today() / conf.irrigationPeriodDays) % 2) == 0) {
      // the small cascade;
      // for safety, tanking into account the big cascade filling
      return smallCascadeFull() 
        || bigCascadeFull();
    }
    // the big cascade
    return bigCascadeFull();
  }
  // filling just a cascade
  if (conf.smallCascadeOnly) {
    // just the small one;
    // for safety, tanking into account the big cascade filling
    return smallCascadeFull() 
      || bigCascadeFull();
  }
  // just the big one
  return bigCascadeFull();
}

void setup() {
  // 
  Wire.begin();
  RTC.begin();
  if (CLOCK_SETUP) {
    // sets the clock to the compile date and time
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }
  
  sc.Init(9600);
  sc.SetConf(&conf);
  
  pinMode(PUMP_MOTOR_PORT, OUTPUT);
  pinMode(SIGNALING_LED_PORT, OUTPUT);
  pinMode(EMPTY_TANK_PORT, INPUT);
  pinMode(BIG_CASCADE_FULL_PORT, INPUT);
  pinMode(SMALL_CASCADE_FULL_PORT, INPUT);
  pinMode(BLUETOOTH_POWER_PORT, INPUT);
  pinMode(BLUETOOTH_CONNECTION_PORT, INPUT);
  
  tsProbeWarningTankLevel = 0;
  stateWarningLED = false;
  tsChangeWarningLED = 0;
  state = 0;
  
  Serial.println("Fresh start!");
}

bool execute() {
  //
  time = RTC.now();
  
  if (state == 0) {
    // init
    if (!conf.Load()) {
      // 
      conf.Default();
    }
    conf.Display();
    trace("Sys init.");
    state = 1000;
  }
  if (state == 200) {
    // restart after config change
    if (!conf.Load()) {
      // 
      conf.Default();
    }
    conf.Display();
    trace("Sys init.");
    state = 1;
  }
  if (state == 1000) {
    // 
    if (SELF_TEST 
      && wait(10000)) return false;
    state = 2000;
  }
  if (state == 2000) {
    // 
    trace("Sys start.");
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
    trace("Test: Begin");
    trace("Test: LED");
    digitalWrite(SIGNALING_LED_PORT, HIGH);
    state = 1010;
  }
  if (state == 1010) {
    // 
    if (wait(1000)) return false;
    state = 11;
  }
  if (state == 11) {
    // self-test: empty tank LED off
    digitalWrite(SIGNALING_LED_PORT, LOW);
    state = 1011;
  }
  if (state == 1011) {
    // 
    if (wait(1000)) return false;
    state = 12;
  }
  if (state == 12) {
    // self-test: empty tank LED on
    digitalWrite(SIGNALING_LED_PORT, HIGH);
    state = 1012;
  }
  if (state == 1012) {
    // 
    if (wait(1000)) return false;
    state = 13;
  }
  if (state == 13) {
    // self-test: empty tank LED off
    digitalWrite(SIGNALING_LED_PORT, LOW);
    state = 1013;
  }
  if (state == 1013) {
    // 
    if (wait(1000)) return false;
    state = 14;
  }
  if (state == 14) {
    // self-test: pump motor on
    trace("Test: Pump");
    digitalWrite(PUMP_MOTOR_PORT, HIGH);
    state = 1014;
  }
  if (state == 1014) {
    // 
    if (wait(8000)) return false;
    state = 15;
  }
  if (state == 15) {
    // self-test: pump motor off
    digitalWrite(PUMP_MOTOR_PORT, LOW);
    state = 16;
  }
  if (state == 16) {
    // self-test: testing cascade full switches
    i = 0;
    state = 17;
  }
  if (state == 17) {
    // 
    if (i >= 30) {
      // last test iteration
      if (CONTINUOUS_TEST) {
        // 
        state = 16;
      } else {
        // 
        trace("Test: End");
        digitalWrite(SIGNALING_LED_PORT, HIGH);
        state = 1016;
      }
    } else {
      // 
      i ++;
      int cnt = 0;
      if (smallCascadeFull()) {
        // 
        trace("Test: Small casc full.");
        cnt ++;
      }
      if (bigCascadeFull()) {
        //
        trace("Test: Big casc full.");
        cnt ++;
      }
      if (tankEmpty()) {
        //
        trace("Test: Water tank empty.");
        cnt ++;
      }
      if (cnt == 0) {
        // 
        trace("Test: All switches open.");
      }
      state = 2017;
    }
  }
  
  if (state == 2017) {
    // 
    if (wait(1000)) return false;
    state = 17;
  }
  if (state == 1016) {
    // 
    if (wait(3000)) return false;
    state = 2016;
  }
  if (state == 2016) {
    // 
    digitalWrite(SIGNALING_LED_PORT, LOW);
    state = 1;
  }
  
  if (state == 1) {
    // tests
    if (tankEmpty()) {
      // 
      trace("Water tank empty!");
      state = 100;
    } else {
      // the water tank is not empty
      if (irrigationInterval()) {
        // 
        trace("It's irrigation time.");
        state = 2;
      } else {
        // other
        return true;
      }
    }
  }
  if (state == 2) {
    // 
    trace("Starting pump.");
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
      trace("Water casc filled-up.");
      state = 4;
    }
    else if (!irrigationInterval()) {
      // 
      trace("Irrigation time is up.");
      state = 4;
    }
    else {
      // the bottle cascade is being filled
      return false;
    }
  }
  if (state == 4) {
    // 
    trace("Stopping pump.");
    digitalWrite(PUMP_MOTOR_PORT, LOW);
    state = 5;
  }
  if (state == 5) {
    // wating for the irrigation time to pass
    if (!irrigationInterval()) {
      // 
      trace("Irrigation time has passed.");
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
    if (timestamp > tsProbeWarningTankLevel + WARNING_TANK_LEVEL_PROBING_INTERVAL) {
      // time to test the water tank level
      if (tankEmpty()) {
        // the tank is still empty
        state = 100;
      } else {
        // 
        digitalWrite(SIGNALING_LED_PORT, LOW);
        trace("Tank no longer empty.");
        state = 1;
      }
    }
  }
  return false;
}

void loop() {
  // 
  if (sc.Read()) {
    // 
    sc.Execute();
    return;
  }
  if (execute()) {
    // nothing to do in the near future
    if (lowPowerMode()) {
      // zoning out for 8 seconds
      LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    } else {
      // staying alert
    }
  } else {
    // 
    if (lowPowerMode()) {
      // zoning out for 1 second
      LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
    } else {
      // staying alert
    }
  }
}
