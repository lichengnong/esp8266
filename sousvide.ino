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

// prototypes

// on/off/state callbacks
int slowCookerOn();
int slowCookerOff();
int getSlowCookerState();
void toggleSlowCooker();
void publishSlowCookerState();

void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();

//------- Replace the following! ------
#define HOST "SlowCooker"
#define SSID "XXXX"       // your network SSID (name)
#define PASSWORD "XXXX"  // your network key
#define DEVICE_NAME "slow_cooker"

#define SLOW_COOKER_SETTING_EEPROM_ADDR 0

volatile byte slowCookerState;
volatile byte slowCookerSetting;
volatile byte pendingPublish = 0;

volatile float f;

#define ONE_WIRE_BUS 2  // GPIO 2
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

//WemoManager wemoManager;
//WemoSwitch *dehumidifier = NULL;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiClient wifiClient;

#define MQTT_SERVER "192.168.1.88"

PubSubClient mqttClient(wifiClient);

#define SlowCookerStateTopic "home/slowcooker/state"
#define SlowCookerSettingTopic "home/slowcooker/setting"
#define SlowCookerAvailabilityTopic "home/slowcooker/available"
#define SlowCookerTemperatureSensorTopic "home/slowcooker/temperature"

void setup()
{
  pinMode(0, OUTPUT);
  pinMode(3, OUTPUT);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, 1);

  EEPROM.begin(64);
  
  slowCookerSetting = EEPROM.read(SLOW_COOKER_SETTING_EEPROM_ADDR);

  if (slowCookerSetting < 120 || slowCookerSetting > 195)
  {
    //initialize
    slowCookerSetting = 0;

    Serial.print("Initialize eeprom slow cooker setting = ");
    Serial.println(slowCookerSetting);
    EEPROM.write(SLOW_COOKER_SETTING_EEPROM_ADDR, slowCookerSetting);
    EEPROM.commit();
  }

  Serial.print("\nInitial slow cooker setting is ");
  Serial.println(slowCookerSetting);

  //turn off slow cooker at the beginning
  digitalWrite(0, HIGH);
  slowCookerState = 0;
  
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
  //  wemoManager.addDevice(*dehumidifier);

  delay(10);

  MDNS.begin(HOST);

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", HOST);

  delay(10);

  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(mqttCallback);

  delay(1000);

  DS18B20.begin();

  delay(1000);
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
  static long last_slow_cooker_toggle_time = 0;

  if (slowCookerSetting == 0) { //OFF
    if (slowCookerState == 1) {
      digitalWrite(0, HIGH);
      slowCookerState = 0;
      pendingPublish = 1;
    }
    return;
  }

  if (isnan(f)) {
      if (slowCookerState == 1) {
        // turn off slowCooker if unknown humidity
        digitalWrite(0, HIGH);
        slowCookerState = 0;
        pendingPublish = 1;
      }
      return;
  }

  if ( (f < (slowCookerSetting - 2)) && (slowCookerState == 0)) {
      // turn on
      unsigned long now = millis();
      if (now - last_slow_cooker_toggle_time > 60000) {
        last_slow_cooker_toggle_time = now;
        digitalWrite(0, LOW);
        slowCookerState = 1;
        pendingPublish = 1;
      }
  }
  else if ( (f > (slowCookerSetting + 2)) && (slowCookerState == 1)) {
      // turn off
      unsigned long now = millis();
      if (now - last_slow_cooker_toggle_time > 60000) {
        last_slow_cooker_toggle_time = now;
        digitalWrite(0, HIGH);
        slowCookerState = 0;
        pendingPublish = 1;
      }
  }
}

int getSlowCookerState() {
    return slowCookerState;
}

void publishSlowCookerState() {
  if (pendingPublish == 1) {
    if (mqttClient.connected()) {
      if (getSlowCookerState())
          mqttClient.publish(SlowCookerStateTopic, "ON", true);
      else
          mqttClient.publish(SlowCookerStateTopic, "OFF", true);
    }
    
    pendingPublish = 0;
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
 if (length < 10) {
  for (int i=0; i<length; ++i)
    buf[i] = payload[i];
  buf[length] = '\0';

  String p(buf);
  int pn = p.toInt();

  if (pn == 0 || (pn >= 120 && pn <= 195)){
    slowCookerSetting = pn;
    EEPROM.write(SLOW_COOKER_SETTING_EEPROM_ADDR, slowCookerSetting);
    EEPROM.commit();
  }
 } 
}

void mqttReconnect() {
  static long lastReconnectAttempt = 0;
  long now = millis();
  if ((now - lastReconnectAttempt) > 6000) {
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

        mqttClient.subscribe(SlowCookerSettingTopic);

        mqttClient.publish(SlowCookerAvailabilityTopic, "online", true);

        pendingPublish = 1;
      } else {
        Serial.println("failed");
      }
    }
  }
}

void readT() {
  static long lastReadAttempt = 0;
  long now = millis();
  if ((now - lastReadAttempt) > 3000) {
    lastReadAttempt = now;

    Serial.print("Requesting temperatures...");
    DS18B20.requestTemperatures(); // Send the command to get temperatures
    Serial.println("DONE");
    // After we got the temperatures, we can print them here.
    // We use the function ByIndex, and as an example get the temperature from the first sensor only.

    float c = DS18B20.getTempCByIndex(0);

    f = DallasTemperature::toFahrenheit(c);
  
    Serial.print("Temperature for the device 1 (index 0) is: ");
    Serial.println(f);

    yield();
  
    if (mqttClient.connected()) {
      mqttClient.publish(SlowCookerTemperatureSensorTopic, String(f).c_str(), false);
    }
  }
}

