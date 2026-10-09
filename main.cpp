#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <WiFiClientSecure.h>
#include <Adafruit_INA219.h>
#include <Adafruit_BME680.h>
#include <time.h>

// wifi
const char* WIFI_SSID     = "REDACTED";
const char* WIFI_PASSWORD = "REDACTED";

// mqtt (hivemq cloud)
const char*   MQTT_HOST = "REDACTED";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "REDACTED";
const char* MQTT_PASS = "REDACTED";
const char* CLIENT_ID = "REDACTED";

WiFiClientSecure espClient;
PubSubClient mqtt(espClient);



// sensors
Adafruit_INA219 ina(0x45);
bool inaOK = false;
Adafruit_BME680 bme;
bool bmeOK = false;

// pins
#define PIN_RESIDENTIAL    16
#define PIN_COMMERCIAL     27
#define PIN_STADIUM        25
#define PIN_STREET         26
#define PIN_MOTOR          23
#define PIN_LDR            32
#define PIN_RELAY_SOLAR    17
#define PIN_BAT_ADC        33

// pwm channels
#define CH_RESIDENTIAL     0
#define CH_COMMERCIAL      1
#define CH_STADIUM         2
#define CH_STREET          3
#define CH_MOTOR           4

#define PWM_FREQ           5000
#define PWM_RESOLUTION     8
#define MOTOR_DUTY_MAX     255
#define MOTOR_MIN_PCT      70   // below this the motor stalls

// zones
struct Zone {
  const char* name;
  uint8_t     channel;
  uint8_t     duty;
  uint8_t     target;
  bool        active;
};

Zone zones[] = {
  {"residential", CH_RESIDENTIAL, 0, 0, true},
  {"commercial",  CH_COMMERCIAL,  0, 0, true},
  {"stadium",     CH_STADIUM,     0, 0, true},
  {"street",      CH_STREET,      0, 0, true},
};
#define NUM_ZONES 4

uint8_t motorDuty   = 0;
uint8_t motorTarget = 0;
bool    motorActive = false;

// scenarios
enum Scenario {
  SCENARIO_MANUAL,
  SCENARIO_AUTO_LDR,
  SCENARIO_LOAD_SHEDDING,
  SCENARIO_EVENT,
  SCENARIO_WEATHER,
};

Scenario currentScenario = SCENARIO_AUTO_LDR;

enum ShedLevel {
  SHED_NORMAL,
  SHED_ALERT,
  SHED_CRITICAL,
  SHED_EMERGENCY,
  SHED_BLACKOUT,
};

ShedLevel shedLevel = SHED_NORMAL;

// the ldr is inverted: the darker it gets, the higher the reading
#define THRESHOLD_DAY        1000
#define THRESHOLD_DAWN       1200
#define THRESHOLD_TWILIGHT   1800
#define THRESHOLD_DUSK       2000

enum DayPeriod { PERIOD_DAY, PERIOD_DAWN, PERIOD_TWILIGHT, PERIOD_DUSK, PERIOD_NIGHT };
DayPeriod currentPeriod = PERIOD_DAY;

// power thresholds used for the load shedding suggestion
#define POWER_NORMAL_MW     1500.0
#define POWER_ALERT_MW      3000.0
#define POWER_CRITICAL_MW   4500.0
#define POWER_EMERGENCY_MW  6000.0

// stadium event
int  evtStartHour = -1, evtStartMin = -1;
int  evtEndHour   = -1, evtEndMin   = -1;
bool evtScheduled = false;

enum EventPhase { EVT_WAITING, EVT_BEFORE, EVT_DURING, EVT_ENDED };
EventPhase currentEvtPhase = EVT_WAITING;

// anomalies
#define VOLTAGE_MIN       11.0
#define VOLTAGE_MAX       13.0
#define CURRENT_MAX       3000.0
#define OVERCONS_RATIO    1.5
float avgPower         = 0;
int   numSamples       = 0;
bool  anomalyDetected  = false;

// weather
float temperature = 0;
float humidity    = 0;
float pressure    = 0;
float gas         = 0;
#define TEMP_HIGH      35.0
#define TEMP_LOW       5.0
#define HUMIDITY_HIGH  80.0
#define PRESSURE_LOW   1000.0
#define PRESSURE_HIGH  1025.0

// solar / grid
bool  sourceIsSolar    = false;
bool  autoSourceMode   = true;
float batteryVoltage   = 0;

// timers
unsigned long lastMeasure     = 0;
unsigned long lastPublish     = 0;
unsigned long lastFade        = 0;
unsigned long lastScenario    = 0;
unsigned long lastEnv         = 0;
unsigned long lastSwitch      = 0;
#define INTERVAL_MEASURE     500
#define INTERVAL_PUBLISH     1000
#define INTERVAL_FADE        20
#define INTERVAL_SCENARIO    1000
#define INTERVAL_ENV         2000
#define INTERVAL_SWITCH      5000

float voltage = 0;
float current = 0;
float power   = 0;

unsigned long lastMqttMsg = 0;
#define MQTT_DEBOUNCE 200


uint8_t pctToDuty(int pct) {
  return map(constrain(pct, 0, 100), 0, 100, 0, 255);
}

uint8_t pctToMotorDuty(int pct) {
  if (pct <= 0) return 0;
  if (pct < MOTOR_MIN_PCT) pct = MOTOR_MIN_PCT;
  return map(constrain(pct, 0, 100), 0, 100, 0, MOTOR_DUTY_MAX);
}

void setZones(int pctRes, int pctCom, int pctStadium, int pctStreet, int pctMotor) {
  zones[0].target = pctToDuty(pctRes);
  zones[1].target = pctToDuty(pctCom);
  zones[2].target = pctToDuty(pctStadium);
  zones[3].target = pctToDuty(pctStreet);
  motorTarget     = pctToMotorDuty(pctMotor);
}

// the strings returned here go out over mqtt, the dashboard expects them as is
const char* getScenarioName() {
  switch (currentScenario) {
    case SCENARIO_MANUAL:         return "MANUEL";
    case SCENARIO_AUTO_LDR:       return "AUTO";
    case SCENARIO_LOAD_SHEDDING:  return "DELESTAGE";
    case SCENARIO_EVENT:          return "EVENEMENT";
    case SCENARIO_WEATHER:        return "METEO";
    default:                      return "INCONNU";
  }
}

const char* getShedLevelName() {
  switch (shedLevel) {
    case SHED_NORMAL:     return "NORMAL";
    case SHED_ALERT:      return "ALERTE";
    case SHED_CRITICAL:   return "CRITIQUE";
    case SHED_EMERGENCY:  return "URGENCE";
    case SHED_BLACKOUT:   return "BLACKOUT";
    default:              return "INCONNU";
  }
}

void updateFading() {
  if (millis() - lastFade < INTERVAL_FADE) return;
  lastFade = millis();
  for (int i = 0; i < NUM_ZONES; i++) {
    if (zones[i].duty < zones[i].target) zones[i].duty++;
    else if (zones[i].duty > zones[i].target) zones[i].duty--;
    ledcWrite(zones[i].channel, zones[i].active ? zones[i].duty : 0);
  }
  if (motorDuty < motorTarget) motorDuty++;
  else if (motorDuty > motorTarget) motorDuty--;
  ledcWrite(CH_MOTOR, motorDuty);
}


void applyScenario(Scenario scenario) {
  currentScenario = scenario;
  for (int i = 0; i < NUM_ZONES; i++) zones[i].active = true;
  motorActive = true;

  switch (scenario) {
    case SCENARIO_MANUAL:
      break;

    case SCENARIO_AUTO_LDR:
      currentPeriod = (DayPeriod)99;  // forces an update on the next pass
      break;

    case SCENARIO_LOAD_SHEDDING:
      shedLevel = SHED_NORMAL;
      setZones(100, 100, 100, 100, 100);
      mqtt.publish("smartgrid/delestage/niveau", "NORMAL");
      break;

    case SCENARIO_EVENT:
      currentEvtPhase = EVT_WAITING;
      break;

    case SCENARIO_WEATHER:
      setZones(100, 100, 100, 100, 100);
      break;
  }

  mqtt.publish("smartgrid/scenario/actuel", getScenarioName());
  Serial.printf("[SCENARIO] -> %s\n", getScenarioName());
}


void applyShedLevel(ShedLevel level) {
  shedLevel = level;
  for (int i = 0; i < NUM_ZONES; i++) zones[i].active = true;
  motorActive = true;

  switch (level) {
    case SHED_NORMAL:
      setZones(100, 100, 100, 100, 100);
      break;
    case SHED_ALERT:
      setZones(100, 100, 100, 50, 100);
      break;
    case SHED_CRITICAL:
      setZones(100, 50, 100, 50, 80);
      break;
    case SHED_EMERGENCY:
      setZones(50, 50, 100, 30, 70);
      break;
    case SHED_BLACKOUT:
      setZones(0, 0, 100, 0, 70);
      zones[0].active = false;
      zones[1].active = false;
      zones[3].active = false;
      break;
  }

  mqtt.publish("smartgrid/delestage/niveau", getShedLevelName());
  Serial.printf("[LOAD SHEDDING] -> %s\n", getShedLevelName());
}


void scenario_DayNight() {
  int ldr = analogRead(PIN_LDR);
  DayPeriod newPeriod;
  if      (ldr < THRESHOLD_DAY)      newPeriod = PERIOD_DAY;
  else if (ldr < THRESHOLD_DAWN)     newPeriod = PERIOD_DAWN;
  else if (ldr < THRESHOLD_TWILIGHT) newPeriod = PERIOD_TWILIGHT;
  else if (ldr < THRESHOLD_DUSK)     newPeriod = PERIOD_DUSK;
  else                               newPeriod = PERIOD_NIGHT;

  if (newPeriod != currentPeriod) {
    currentPeriod = newPeriod;
    switch (currentPeriod) {
      case PERIOD_DAY:       setZones(20, 80, 0,   0,   100); break;
      case PERIOD_DAWN:      setZones(30, 70, 20,  20,  90);  break;
      case PERIOD_TWILIGHT:  setZones(50, 60, 40,  50,  85);  break;
      case PERIOD_DUSK:      setZones(70, 55, 50,  80,  80);  break;
      case PERIOD_NIGHT:     setZones(80, 50, 100, 100, 75);  break;
    }
    // payloads stay in french, the dashboard matches on them
    const char* s[] = {"jour","crepuscule_matin","crepuscule","crepuscule_soir","nuit"};
    mqtt.publish("smartgrid/periode", s[currentPeriod]);
    Serial.printf("[DAY/NIGHT] LDR=%d -> %s\n", ldr, s[currentPeriod]);
  }
}


// the level is still picked by hand, here we only suggest one
void scenario_LoadShedding() {
  int suggestion;
  if      (power > POWER_EMERGENCY_MW) suggestion = 4;
  else if (power > POWER_CRITICAL_MW)  suggestion = 3;
  else if (power > POWER_ALERT_MW)     suggestion = 2;
  else if (power > POWER_NORMAL_MW)    suggestion = 1;
  else                                 suggestion = 0;

  char buf[4]; snprintf(buf, sizeof(buf), "%d", suggestion);
  mqtt.publish("smartgrid/delestage/suggestion", buf);
}


// config format: "HH:MM,HH:MM" (start,end)
// before 16:00 the industrial zone stays at 100%, after that it gets lowered
void scenario_Event() {
  if (!evtScheduled) return;

  struct tm t;
  if (!getLocalTime(&t)) {
    Serial.println("[EVT] NTP error — time not available");
    return;
  }

  int nowMin   = t.tm_hour * 60 + t.tm_min;
  int startMin = evtStartHour * 60 + evtStartMin;
  int endMin   = evtEndHour * 60 + evtEndMin;
  int beforeMin = startMin - 30;

  int industrialLoad = (evtStartHour < 16) ? 100 : MOTOR_MIN_PCT;

  if (nowMin >= beforeMin && nowMin < startMin) {
    if (currentEvtPhase != EVT_BEFORE) {
      currentEvtPhase = EVT_BEFORE;
      setZones(80, 70, 50, 100, industrialLoad);
      mqtt.publish("smartgrid/evenement/phase", "AVANT");
      Serial.printf("[EVT] BEFORE phase — Industrial=%d%%\n", industrialLoad);
    }
  }
  else if (nowMin >= startMin && nowMin < endMin) {
    if (currentEvtPhase != EVT_DURING) {
      currentEvtPhase = EVT_DURING;
      setZones(50, 50, 100, 100, industrialLoad);
      mqtt.publish("smartgrid/evenement/phase", "PENDANT");
      Serial.printf("[EVT] DURING phase — Industrial=%d%%\n", industrialLoad);
    }
  }
  else if (nowMin >= endMin) {
    if (currentEvtPhase != EVT_ENDED) {
      currentEvtPhase = EVT_ENDED;
      evtScheduled = false;
      mqtt.publish("smartgrid/evenement/phase", "FIN");
      Serial.println("[EVT] END phase — back to AUTO");
      applyScenario(SCENARIO_AUTO_LDR);
    }
  }
}


void scenario_Weather() {
  if (!bmeOK) return;

  if (temperature > TEMP_HIGH) {
    setZones(60, 60, 50, 70, 100);
    mqtt.publish("smartgrid/meteo/alerte", "CHALEUR");
    Serial.printf("[WEATHER] HEAT T=%.1f°C — motor max, lighting reduced\n", temperature);
  }
  else if (temperature < TEMP_LOW) {
    setZones(100, 80, 70, 100, 80);
    mqtt.publish("smartgrid/meteo/alerte", "FROID");
    Serial.printf("[WEATHER] COLD T=%.1f°C — residential + street max\n", temperature);
  }
  else if (humidity > HUMIDITY_HIGH) {
    setZones(80, 70, 60, 80, 90);
    mqtt.publish("smartgrid/meteo/alerte", "HUMIDITE");
    Serial.printf("[WEATHER] HUMIDITY H=%.1f%% — ventilation increased\n", humidity);
  }
  else if (pressure < PRESSURE_LOW) {
    setZones(70, 60, 30, 100, 80);
    mqtt.publish("smartgrid/meteo/alerte", "TEMPETE");
    Serial.printf("[WEATHER] STORM P=%.0fhPa — safety mode\n", pressure);
  }
  else if (pressure > PRESSURE_HIGH) {
    setZones(60, 70, 40, 50, 90);
    mqtt.publish("smartgrid/meteo/alerte", "BEAU_TEMPS");
    Serial.printf("[WEATHER] FAIR WEATHER P=%.0fhPa — outdoor eco mode\n", pressure);
  }
  else {
    setZones(80, 80, 70, 80, 90);
    mqtt.publish("smartgrid/meteo/alerte", "NORMAL");
  }
}


// runs whatever the scenario
void scenario_Anomalies() {
  if (!inaOK) return;
  numSamples++;
  avgPower += (power - avgPower) / numSamples;

  if (numSamples > 10 && power > avgPower * OVERCONS_RATIO) {
    if (!anomalyDetected) {
      anomalyDetected = true;
      mqtt.publish("smartgrid/anomalie/type", "SURCONSOMMATION");
      char buf[16]; dtostrf(power, 1, 0, buf);
      mqtt.publish("smartgrid/anomalie/valeur", buf);
    }
  } else {
    anomalyDetected = false;
  }

  if (current > CURRENT_MAX) {
    mqtt.publish("smartgrid/anomalie/type", "COURT_CIRCUIT");
    for (int i = 0; i < NUM_ZONES; i++) { zones[i].target = 0; zones[i].active = false; }
    motorTarget = 0;
  }

  if (voltage > 0 && (voltage < VOLTAGE_MIN || voltage > VOLTAGE_MAX)) {
    mqtt.publish("smartgrid/anomalie/type", "DERIVE_TENSION");
    char buf[10]; dtostrf(voltage, 1, 2, buf);
    mqtt.publish("smartgrid/anomalie/valeur", buf);
  }
}


void readEnvironment() {
  if (!bmeOK) return;
  if (millis() - lastEnv < INTERVAL_ENV) return;
  lastEnv = millis();

  if (!bme.performReading()) {
    Serial.println("[BME680] Read error");
    return;
  }

  temperature = bme.temperature;
  humidity    = bme.humidity;
  pressure    = bme.pressure / 100.0;
  gas         = bme.gas_resistance / 1000.0;

  char buf[8];
  dtostrf(temperature, 1, 1, buf);
  mqtt.publish("smartgrid/meteo/temperature", buf);

  dtostrf(humidity, 1, 1, buf);
  mqtt.publish("smartgrid/meteo/humidite", buf);

  dtostrf(pressure, 1, 1, buf);
  mqtt.publish("smartgrid/meteo/pression", buf);

  dtostrf(gas, 1, 1, buf);
  mqtt.publish("smartgrid/meteo/gaz", buf);

  Serial.printf("[BME680] T=%.1f°C H=%.1f%% P=%.0fhPa Gas=%.1fkOhm\n",
                temperature, humidity, pressure, gas);
}


void handleSourceSwitching() {
  if (millis() - lastSwitch < INTERVAL_SWITCH) return;
  lastSwitch = millis();

  int raw = analogRead(PIN_BAT_ADC);
  batteryVoltage = (raw / 4095.0) * 3.3 * 2.0;  // /2 voltage divider

  char buf[8];
  dtostrf(batteryVoltage, 1, 2, buf);
  mqtt.publish("smartgrid/energy/battery_voltage", buf);

  // two different thresholds (3.0 / 3.2) so the relay doesn't flutter
  if (autoSourceMode) {
    if (sourceIsSolar && batteryVoltage < 3.0) {
      digitalWrite(PIN_RELAY_SOLAR, LOW);
      sourceIsSolar = false;
      mqtt.publish("smartgrid/energy/source", "GRID");
      Serial.println("[ENERGY] Battery empty -> GRID");
    }
    else if (!sourceIsSolar && batteryVoltage > 3.2) {
      digitalWrite(PIN_RELAY_SOLAR, HIGH);
      sourceIsSolar = true;
      mqtt.publish("smartgrid/energy/source", "SOLAR");
      Serial.println("[ENERGY] Battery OK -> SOLAR");
    }
  }

  if (batteryVoltage > 3.8)      mqtt.publish("smartgrid/energy/battery_status", "PLEINE");
  else if (batteryVoltage > 3.5) mqtt.publish("smartgrid/energy/battery_status", "MOYENNE");
  else if (batteryVoltage > 3.0) mqtt.publish("smartgrid/energy/battery_status", "FAIBLE");
  else                           mqtt.publish("smartgrid/energy/battery_status", "VIDE");

  mqtt.publish("smartgrid/energy/source", sourceIsSolar ? "SOLAR" : "GRID");
  mqtt.publish("smartgrid/energy/mode_status", autoSourceMode ? "AUTO" : "MANUEL");

  Serial.printf("[ENERGY] Bat=%.2fV Source=%s Mode=%s\n",
                batteryVoltage, sourceIsSolar ? "SOLAR" : "GRID",
                autoSourceMode ? "AUTO" : "MANUAL");
}


void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (millis() - lastMqttMsg < MQTT_DEBOUNCE) return;
  lastMqttMsg = millis();

  String t = String(topic);
  String message = "";
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  message.trim();
  String msgUpper = message; msgUpper.toUpperCase();

  Serial.printf("[MQTT RX] %s -> %s\n", topic, message.c_str());

  if (t == "smartgrid/scenario") {
    if      (msgUpper == "MANUEL")    applyScenario(SCENARIO_MANUAL);
    else if (msgUpper == "AUTO")      applyScenario(SCENARIO_AUTO_LDR);
    else if (msgUpper == "DELESTAGE") applyScenario(SCENARIO_LOAD_SHEDDING);
    else if (msgUpper == "EVENEMENT") applyScenario(SCENARIO_EVENT);
    else if (msgUpper == "METEO")     applyScenario(SCENARIO_WEATHER);
    else Serial.printf("[SCENARIO] Unknown: %s\n", message.c_str());
    return;
  }

  // shed levels are only accepted while the scenario is active
  if (t == "smartgrid/delestage/mode") {
    if (currentScenario != SCENARIO_LOAD_SHEDDING) {
      Serial.printf("[LOAD SHEDDING] Ignored — active scenario = %s\n", getScenarioName());
      mqtt.publish("smartgrid/delestage/erreur", "DELESTAGE_NON_ACTIF");
      return;
    }
    if      (msgUpper == "NORMAL")   applyShedLevel(SHED_NORMAL);
    else if (msgUpper == "ALERTE")   applyShedLevel(SHED_ALERT);
    else if (msgUpper == "CRITIQUE") applyShedLevel(SHED_CRITICAL);
    else if (msgUpper == "URGENCE")  applyShedLevel(SHED_EMERGENCY);
    else if (msgUpper == "BLACKOUT") applyShedLevel(SHED_BLACKOUT);
    return;
  }

  for (int i = 0; i < NUM_ZONES; i++) {
    char ledTopic[20]; snprintf(ledTopic, sizeof(ledTopic), "smartgrid/led%d", i + 1);
    if (t == ledTopic) {
      zones[i].active = (msgUpper == "ON");
      zones[i].target = (msgUpper == "ON") ? 255 : 0;
      currentScenario = SCENARIO_MANUAL;
      return;
    }
  }

  for (int i = 0; i < NUM_ZONES; i++) {
    char bTopic[30]; snprintf(bTopic, sizeof(bTopic), "smartgrid/led%d/brightness", i + 1);
    if (t == bTopic) {
      int pct = message.toInt();
      zones[i].target = pctToDuty(pct);
      zones[i].active = (pct > 0);
      currentScenario = SCENARIO_MANUAL;
      return;
    }
  }

  if (t == "smartgrid/moteur") {
    if (msgUpper == "ON") {
      motorActive = true;
      motorTarget = pctToMotorDuty(100);
    } else if (msgUpper == "OFF") {
      motorActive = false;
      motorTarget = 0;
    } else {
      int p = message.toInt();
      motorTarget = pctToMotorDuty(p);
      motorActive = (p > 0);
    }
    currentScenario = SCENARIO_MANUAL;
  }
  else if (t == "smartgrid/moteur/brightness") {
    int p = message.toInt();
    motorTarget = pctToMotorDuty(p);
    motorActive = (p > 0);
    currentScenario = SCENARIO_MANUAL;
  }

  else if (t == "smartgrid/evenement/config") {
    int sh, sm, eh, em;
    if (sscanf(message.c_str(), "%d:%d,%d:%d", &sh, &sm, &eh, &em) == 4) {
      evtStartHour = sh;
      evtStartMin  = sm;
      evtEndHour   = eh;
      evtEndMin    = em;
      evtScheduled = true;
      currentEvtPhase = EVT_WAITING;
      currentScenario = SCENARIO_EVENT;
      mqtt.publish("smartgrid/scenario/actuel", "EVENEMENT");
      Serial.printf("[EVT] Scheduled: %02d:%02d → %02d:%02d\n", sh, sm, eh, em);
    } else {
      Serial.println("[EVT] Format error — expected HH:MM,HH:MM");
    }
  }

  else if (t == "smartgrid/evenement/annuler") {
    evtScheduled = false;
    currentEvtPhase = EVT_WAITING;
    mqtt.publish("smartgrid/evenement/phase", "ANNULE");
    Serial.println("[EVT] Event cancelled — back to AUTO");
    applyScenario(SCENARIO_AUTO_LDR);
  }

  else if (t == "smartgrid/energy/mode") {
    if (msgUpper == "AUTO") {
      autoSourceMode = true;
      mqtt.publish("smartgrid/energy/mode_status", "AUTO");
      Serial.println("[ENERGY] Mode -> AUTO");
    } else if (msgUpper == "MANUEL") {
      autoSourceMode = false;
      mqtt.publish("smartgrid/energy/mode_status", "MANUEL");
      Serial.println("[ENERGY] Mode -> MANUAL");
    }
  }
  else if (t == "smartgrid/energy/select") {
    if (!autoSourceMode) {
      if (msgUpper == "SOLAR") {
        digitalWrite(PIN_RELAY_SOLAR, HIGH);
        sourceIsSolar = true;
        mqtt.publish("smartgrid/energy/source", "SOLAR");
        Serial.println("[ENERGY] Manual -> SOLAR");
      } else if (msgUpper == "GRID") {
        digitalWrite(PIN_RELAY_SOLAR, LOW);
        sourceIsSolar = false;
        mqtt.publish("smartgrid/energy/source", "GRID");
        Serial.println("[ENERGY] Manual -> GRID");
      }
    } else {
      Serial.println("[ENERGY] Ignoring select: AUTO mode is active");
    }
  }

  // simulated values to test without the bme680
  else if (t == "smartgrid/sim/temperature") temperature = message.toFloat();
  else if (t == "smartgrid/sim/humidite")    humidity = message.toFloat();
  else if (t == "smartgrid/sim/pression")    pressure = message.toFloat();
}


void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("[MQTT] Connecting...");
    if (mqtt.connect(CLIENT_ID)) {
      Serial.println(" OK!");
      mqtt.subscribe("smartgrid/scenario");
      mqtt.subscribe("smartgrid/delestage/mode");
      mqtt.subscribe("smartgrid/led1");
      mqtt.subscribe("smartgrid/led2");
      mqtt.subscribe("smartgrid/led3");
      mqtt.subscribe("smartgrid/led4");
      mqtt.subscribe("smartgrid/led1/brightness");
      mqtt.subscribe("smartgrid/led2/brightness");
      mqtt.subscribe("smartgrid/led3/brightness");
      mqtt.subscribe("smartgrid/led4/brightness");
      mqtt.subscribe("smartgrid/moteur");
      mqtt.subscribe("smartgrid/moteur/brightness");
      mqtt.subscribe("smartgrid/evenement/config");
      mqtt.subscribe("smartgrid/evenement/annuler");
      mqtt.subscribe("smartgrid/sim/temperature");
      mqtt.subscribe("smartgrid/sim/humidite");
      mqtt.subscribe("smartgrid/sim/pression");
      mqtt.subscribe("smartgrid/energy/mode");
      mqtt.subscribe("smartgrid/energy/select");

      mqtt.publish("smartgrid/status", "ONLINE");
      mqtt.publish("smartgrid/scenario/actuel", getScenarioName());
      mqtt.publish("smartgrid/energy/source", "GRID");
      mqtt.publish("smartgrid/energy/mode_status", "AUTO");
      Serial.println("[MQTT] Subscriptions OK");
    } else {
      Serial.printf(" ERROR rc=%d\n", mqtt.state());
      delay(5000);
    }
  }
}


void connectWiFi() {
  Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) { delay(500); Serial.print("."); t++; }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\n[WIFI] OK! IP: %s\n", WiFi.localIP().toString().c_str());
  else { Serial.println("\n[WIFI] FAILED"); delay(5000); ESP.restart(); }
}


void readSensors() {
  if (millis() - lastMeasure < INTERVAL_MEASURE) return;
  lastMeasure = millis();
  if (inaOK) {
    voltage = ina.getBusVoltage_V();
    current = abs(ina.getCurrent_mA());
    power   = abs(ina.getPower_mW());
  }
}


void publishMeasurements() {
  if (millis() - lastPublish < INTERVAL_PUBLISH) return;
  lastPublish = millis();

  char buf[16];

  if (inaOK) {
    dtostrf(voltage, 1, 2, buf); mqtt.publish("smartgrid/voltage", buf);
    dtostrf(current, 1, 2, buf); mqtt.publish("smartgrid/current", buf);
    dtostrf(power, 1, 2, buf);   mqtt.publish("smartgrid/power", buf);
  }

  int ldr = analogRead(PIN_LDR);
  dtostrf(ldr, 1, 0, buf); mqtt.publish("smartgrid/ldr", buf);

  mqtt.publish("smartgrid/scenario/actuel", getScenarioName());

  if (currentScenario == SCENARIO_LOAD_SHEDDING) {
    mqtt.publish("smartgrid/delestage/niveau", getShedLevelName());
  }

  for (int i = 0; i < NUM_ZONES; i++) {
    char tb[50];
    int pct = map(zones[i].duty, 0, 255, 0, 100);
    snprintf(tb, sizeof(tb), "smartgrid/led%d/duty", i + 1);
    snprintf(buf, sizeof(buf), "%d", pct);
    mqtt.publish(tb, buf);

    snprintf(tb, sizeof(tb), "smartgrid/led%d/state", i + 1);
    mqtt.publish(tb, (zones[i].active && zones[i].duty > 0) ? "ON" : "OFF");
  }

  int mp = map(motorDuty, 0, MOTOR_DUTY_MAX, 0, 100);
  snprintf(buf, sizeof(buf), "%d", mp);
  mqtt.publish("smartgrid/moteur/duty", buf);
  mqtt.publish("smartgrid/moteur/state", motorDuty > 0 ? "ON" : "OFF");

  Serial.printf("[PUB] V=%.2f I=%.1f P=%.1f LDR=%d Scenario=%s\n",
                voltage, current, power, ldr, getScenarioName());
  Serial.printf("      Res=%d%% Com=%d%% Stadium=%d%% Street=%d%% Indus=%d%%\n",
                map(zones[0].duty, 0, 255, 0, 100), map(zones[1].duty, 0, 255, 0, 100),
                map(zones[2].duty, 0, 255, 0, 100), map(zones[3].duty, 0, 255, 0, 100), mp);
}


void runScenarios() {
  if (millis() - lastScenario < INTERVAL_SCENARIO) return;
  lastScenario = millis();

  scenario_Anomalies();

  switch (currentScenario) {
    case SCENARIO_AUTO_LDR:
      scenario_DayNight();
      break;
    case SCENARIO_LOAD_SHEDDING:
      scenario_LoadShedding();
      break;
    case SCENARIO_EVENT:
      scenario_Event();
      break;
    case SCENARIO_WEATHER:
      scenario_Weather();
      break;
    case SCENARIO_MANUAL:
      break;
  }
}


void setup() {
  Serial.begin(115200); delay(2000);
  Serial.println("\n============================================");
  Serial.println("  SMART GRID for SMART CITIES — ESI-SBA v4");
  Serial.println("  5 Scenarios | MQTT Dashboard | NTP");
  Serial.println("============================================\n");

  pinMode(21, INPUT_PULLUP);
  pinMode(22, INPUT_PULLUP);
  Wire.begin(21, 22);

  if (ina.begin()) { inaOK = true; Serial.println("[INA219] OK 0x44"); }
  else { inaOK = false; Serial.println("[INA219] Not detected"); }

  if (bme.begin(0x77)) {
    bmeOK = true;
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);
    Serial.println("[BME680] OK 0x77");
  } else {
    bmeOK = false;
    Serial.println("[BME680] Not detected");
  }

  // UTC+1 for Algeria
  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.println("[NTP] Syncing...");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 5000)) {
    Serial.printf("[NTP] Time: %02d:%02d:%02d\n",
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("[NTP] Sync failed");
  }

  pinMode(PIN_LDR, INPUT);

  pinMode(PIN_BAT_ADC, INPUT);

  pinMode(PIN_RELAY_SOLAR, OUTPUT);
  digitalWrite(PIN_RELAY_SOLAR, LOW);

  ledcSetup(CH_RESIDENTIAL, PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(CH_COMMERCIAL,  PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(CH_STADIUM,     PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(CH_STREET,      PWM_FREQ, PWM_RESOLUTION);
  ledcSetup(CH_MOTOR,       1000,     PWM_RESOLUTION);

  ledcAttachPin(PIN_RESIDENTIAL, CH_RESIDENTIAL);
  ledcAttachPin(PIN_COMMERCIAL,  CH_COMMERCIAL);
  ledcAttachPin(PIN_STADIUM,     CH_STADIUM);
  ledcAttachPin(PIN_STREET,      CH_STREET);
  ledcAttachPin(PIN_MOTOR,       CH_MOTOR);

  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  connectMQTT();

  applyScenario(SCENARIO_AUTO_LDR);
  Serial.println("[READY] System started in AUTO (LDR) mode!\n");
}


void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  readSensors();
  handleSourceSwitching();
  readEnvironment();
  runScenarios();
  updateFading();
  publishMeasurements();
}