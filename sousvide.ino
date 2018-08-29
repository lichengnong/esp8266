#include <CallbackFunction.h>
#include <WemoManager.h>
#include <WemoSwitch.h>

#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

struct
{
  int id;
  DeviceAddress addr;
} T[4];

// prototypes

// on/off/state callbacks
int getSlowCookerState();
void toggleSlowCooker();
void publishSlowCookerState();
float getTempByID(int id);
void readT();

void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();

//------- Replace the following! ------
#define HOST "SlowCooker"
#define SSID "XXXXX"       // your network SSID (name)
#define PASSWORD "XXXXX"  // your network key
#define DEVICE_NAME "slow_cooker"

#define SC_OFF 0
#define SC_IN_DELAY 1
#define SC_IN_COOKING_OFF 2
#define SC_IN_COOKING_ON 3

volatile byte slowCookerState = SC_OFF;
volatile byte targetFoodTemp = 0;
volatile byte targetCookingTemp = 0;
volatile float cookingTemp;
volatile float foodTemp;
volatile unsigned long targetDelayTime = 0;
volatile unsigned long targetCookingTime = 0;
volatile unsigned long delayEndTime = 0;
volatile unsigned long cookingEndTime = 0;

volatile byte pendingStatePublish = 1;

#define ONE_WIRE_BUS 2  // GPIO 2
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
uint8_t deviceCount = 0;

//WemoManager wemoManager;
//WemoSwitch *slowCooker = NULL;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiClient wifiClient;

#define MQTT_SERVER "192.168.1.88"

PubSubClient mqttClient(wifiClient);

#define SlowCookerStateTopic "home/slowcooker/state"
#define SlowCookerSetDelayTimeTopic "home/slowcooker/setdelaytime"
#define SlowCookerSetTargetCookingTempTopic "home/slowcooker/setcookingtemp"
#define SlowCookerSetTargetFoodTempTopic "home/slowcooker/setfoodtemp"
#define SlowCookerSetCookingTimeTopic "home/slowcooker/setcookingtime"
#define SlowCookerSwitchCommandTopic "home/slowcooker/switch/set"
#define SlowCookerSwitchStateTopic "home/slowcooker/switch/state"
#define SlowCookerAvailabilityTopic "home/slowcooker/available"
#define SlowCookerCookingTempSensorTopic "home/slowcooker/cookingtemp"
#define SlowCookerFoodTempSensorTopic "home/slowcooker/foodtemp"
#define SlowCookerTimeRemainingSensorTopic "home/slowcooker/timeremaining"
#define SlowCookerSetupTopic "home/slowcooker/setup"

void setup()
{
  pinMode(0, OUTPUT);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, 1);

  DS18B20.begin();

  // count devices
  deviceCount = DS18B20.getDeviceCount();
  Serial.print("#devices: ");
  Serial.println(deviceCount);

  // Read ID's per sensor
  // and put them in T array
  for (uint8_t index = 0; index < deviceCount; index++)
  {
    // go through sensors
    DS18B20.getAddress(T[index].addr, index);
    T[index].id = DS18B20.getUserData(T[index].addr);
  }

  //turn off slow cooker at the beginning
  digitalWrite(0, HIGH);
  slowCookerState = SC_OFF;
  pendingStatePublish = 1;

  delay(10);
  
  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);

  WiFi.hostname(HOST);

  // wemoManager.begin();
  // Format: Alexa invocation name, local port no, on callback, off callback
  //  dehumidifier = new WemoSwitch(DEVICE_NAME, 81, dehumidifierOn, dehumidifierOff, getDehumidifierState);
  //  wemoManager.addDevice(*slowCooker);

  delay(10);

  MDNS.begin(HOST);

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", HOST);

  delay(10);

  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(mqttCallback);

  delay(2000);
}

void loop()
{
  mqttReconnect();

  yield(); 

  mqttClient.loop();

  yield();
  
  //wemoManager.serverLoop();
  //yield();

  readT();

  yield();

  toggleSlowCooker();

  yield();
  
  publishSlowCookerState();

  yield();
  
  httpServer.handleClient();
}

void toggleSlowCooker() {
  static unsigned long last_slow_cooker_toggle_time = 0;

  if (slowCookerState == SC_OFF)
    return;

  unsigned long now = millis();

  if (isnan(cookingTemp) || isnan(foodTemp)) {
      // turn off
      if (slowCookerState != SC_OFF) {
        last_slow_cooker_toggle_time = now;
        digitalWrite(0, HIGH);
        slowCookerState = SC_OFF;
        pendingStatePublish = 1;
      }
  }
  else if ((foodTemp >= targetFoodTemp) || 
           ((slowCookerState == SC_IN_COOKING_ON || 
             slowCookerState == SC_IN_COOKING_OFF) && cookingEndTime <= now)) {
      // turn off
      if (slowCookerState != SC_OFF) {
        last_slow_cooker_toggle_time = now;
        digitalWrite(0, HIGH);
        slowCookerState = SC_OFF;
        pendingStatePublish = 1;
      }
  } 
  else if (slowCookerState == SC_IN_DELAY) {
      if (delayEndTime < now) {
        cookingEndTime = targetCookingTime + now;
        slowCookerState = SC_IN_COOKING_OFF;
        pendingStatePublish = 1;
      }
  }
  else if (slowCookerState == SC_IN_COOKING_OFF) {
      if (cookingTemp < (targetCookingTemp - 3)) {
        // turn on
        if (now - last_slow_cooker_toggle_time > 60000) {
          digitalWrite(0, LOW);
          slowCookerState = SC_IN_COOKING_ON;
          pendingStatePublish = 1;
        }
      }
  }
  else if (slowCookerState == SC_IN_COOKING_ON) {
      if (cookingTemp >= targetCookingTemp) {
        // turn off
        last_slow_cooker_toggle_time = now;
        digitalWrite(0, HIGH);
        slowCookerState = SC_IN_COOKING_OFF;
        pendingStatePublish = 1;
      }
  }
}

int getSlowCookerState() {
    return slowCookerState;
}

void publishSlowCookerState() {
  if (getSlowCookerState() != SC_OFF) {
     static unsigned long lastReportAttempt = 0;
     unsigned long now = millis();
     
     if (now < lastReportAttempt || (now - lastReportAttempt) > 10000) {
        lastReportAttempt = now;

        int timeRemaining = 0;
        if (getSlowCookerState() == SC_IN_DELAY)
          timeRemaining = (delayEndTime > now ? delayEndTime - now : 0)/60000;
        else
          timeRemaining = (cookingEndTime > now ? cookingEndTime - now : 0)/60000;
      
        mqttClient.publish(SlowCookerTimeRemainingSensorTopic, String(timeRemaining).c_str(), false);
     }
  }
  
  if (pendingStatePublish == 1) {
    if (mqttClient.connected()) {
      switch (getSlowCookerState()) {
      case SC_OFF: 
          mqttClient.publish(SlowCookerStateTopic, "OFF", true);
          mqttClient.publish(SlowCookerSwitchStateTopic, "OFF", true);
          mqttClient.publish(SlowCookerSwitchCommandTopic, "0", true);
          break;
      case SC_IN_DELAY: 
          mqttClient.publish(SlowCookerStateTopic, "DELAY", true);
          mqttClient.publish(SlowCookerSwitchStateTopic, "ON", true);
          break;
      case SC_IN_COOKING_OFF: 
          mqttClient.publish(SlowCookerStateTopic, "RESTING", true);
          mqttClient.publish(SlowCookerSwitchStateTopic, "ON", true);
          break;
      case SC_IN_COOKING_ON: 
          mqttClient.publish(SlowCookerStateTopic, "HEATING", true);
          mqttClient.publish(SlowCookerSwitchStateTopic, "ON", true);
          break;
      default:
          break;
      }
    }
    
    pendingStatePublish = 0;
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  //Serial.print("Message arrived [");
  //Serial.print(topic);
  //Serial.print("] ");
  //for (int i = 0; i < length; i++) {
  //  Serial.print((char)payload[i]);
  //}
  //Serial.println();

  char buf[10];

  if (length >= 10) 
   return;
 
  for (int i=0; i<length; ++i)
    buf[i] = payload[i];
  buf[length] = '\0';

  String p(buf);
  int pn = p.toInt();

  unsigned long now = millis();

  if (!strcmp(topic, SlowCookerSetupTopic)) {
     // label device
     for (uint8_t index = 0; index < deviceCount; ++index)
      DS18B20.setUserDataByIndex(index, index);
  }
  else if (!strcmp(topic, SlowCookerSetDelayTimeTopic)) {
     targetDelayTime = pn*60000;

     if (slowCookerState == SC_IN_COOKING_ON || slowCookerState == SC_IN_COOKING_OFF)
        return;

     if (slowCookerState == SC_IN_DELAY) {
        if (pn == 0) {
            slowCookerState = SC_IN_COOKING_OFF;
            pendingStatePublish = 1;
        }
        else
          delayEndTime = targetDelayTime + now;
     }
  }
  else if (!strcmp(topic, SlowCookerSetCookingTimeTopic)) {
      targetCookingTime = pn*60000;

      if (slowCookerState == SC_IN_COOKING_OFF || SC_IN_COOKING_ON)
         cookingEndTime = targetCookingTime + now;
  }
  else if (!strcmp(topic, SlowCookerSetTargetCookingTempTopic)) {
      targetCookingTemp = pn;
  }
  else if (!strcmp(topic, SlowCookerSetTargetFoodTempTopic)) {
      targetFoodTemp = pn;
  }
  else if (!strcmp(topic, SlowCookerSwitchCommandTopic)) {
    if (pn == 0) {
      if (slowCookerState != SC_OFF) {
        slowCookerState = SC_OFF;
        digitalWrite(0, HIGH);
        pendingStatePublish = 1;
      }
    }
    else if (pn == 1) {
      if (slowCookerState != SC_OFF) 
        return;

      pendingStatePublish = 1;
       
      if (targetDelayTime > 0) {
          slowCookerState = SC_IN_DELAY;
          delayEndTime = targetDelayTime + now;
      }
      else {
        slowCookerState = SC_IN_COOKING_OFF;
        cookingEndTime = targetCookingTime + now;
      }
    }
  }
}

void mqttReconnect() {
  static unsigned long lastReconnectAttempt = 0;
  unsigned long now = millis();
  if (now < lastReconnectAttempt || (now - lastReconnectAttempt) > 6000) {
    lastReconnectAttempt = now;
    
    if (!mqttClient.connected()) {
      mqttClient.disconnect();

      Serial.print("Attempting MQTT connection...");
      // Create a random client ID
      String clientId = "ESP8266Client-";
      clientId += String(ESP.getChipId());
      
      // Attempt to connect
      if (mqttClient.connect(clientId.c_str(), SlowCookerAvailabilityTopic, 0, true, "offline")) {
        Serial.println("connected");

        mqttClient.subscribe(SlowCookerSetDelayTimeTopic);
        mqttClient.subscribe(SlowCookerSetCookingTimeTopic);
        mqttClient.subscribe(SlowCookerSetTargetFoodTempTopic);
        mqttClient.subscribe(SlowCookerSetTargetCookingTempTopic);
        mqttClient.subscribe(SlowCookerSwitchCommandTopic);
        mqttClient.subscribe(SlowCookerSetupTopic);

        mqttClient.publish(SlowCookerAvailabilityTopic, "online", true);

        pendingStatePublish = 1;
      } else {
        Serial.println("failed");
      }
    }
  }
}

float getTempByID(int id)
{
  for (uint8_t index = 0; index < deviceCount; index++)
  {
    if (T[index].id == id)
    {
      return DS18B20.getTempC(T[index].addr);
    }
  }
  return 999;
}

void readT() {
  static unsigned long lastReadAttempt = 0;
  unsigned long now = millis();
  if (now < lastReadAttempt || (now - lastReadAttempt) > 2000) {
    lastReadAttempt = now;

    //Serial.print("Requesting temperatures...");
    DS18B20.requestTemperatures(); // Send the command to get temperatures
    //Serial.println("DONE");
    // After we got the temperatures, we can print them here.
    // We use the function ByIndex, and as an example get the temperature from the first sensor only.

    float c = getTempByID(0);

    cookingTemp = DallasTemperature::toFahrenheit(c);
  
    //Serial.print("Temperature for the cooking (id 0) is: ");
    //Serial.println(cookingTemp);

    c = getTempByID(1);

    foodTemp = DallasTemperature::toFahrenheit(c);
  
    //Serial.print("Temperature for the food (id 1) is: ");
    //Serial.println(foodTemp);

    yield();
  
    if (mqttClient.connected()) {
      mqttClient.publish(SlowCookerCookingTempSensorTopic, String(cookingTemp).c_str(), false);
      mqttClient.publish(SlowCookerFoodTempSensorTopic, String(foodTemp).c_str(), false);
    }
  }
}
