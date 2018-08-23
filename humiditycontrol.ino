#include <CallbackFunction.h>
#include <WemoManager.h>
#include <WemoSwitch.h>

#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <DHT.h>

// prototypes

// on/off/state callbacks
int dehumidifierOn();
int dehumidifierOff();
int getDehumidifierState();
void toggleDehumidifier();
void publishDehumidifierState();

void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();

void dhtRead();

//------- Replace the following! ------
#define HOST "FreezerHumidityController"
#define SSID "XXXX"       // your network SSID (name)
#define PASSWORD "XXXX"  // your network key
#define DEVICE_NAME "freezer_humidity_controller"

#define HUMIDITY_SETTING_EEPROM_ADDR 0

volatile byte dehumidifierState;
volatile byte humiditySetting;
volatile byte pendingPublish = 0;

volatile float h; 
volatile float f;

//WemoManager wemoManager;
//WemoSwitch *dehumidifier = NULL;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

#define DHTPIN 2 
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

WiFiClient wifiClient;

#define MQTT_SERVER "192.168.1.88"

PubSubClient mqttClient(wifiClient);

#define HumidifierStateTopic "home/freezer/freezer1/humidifier/state"
#define DehumidifierStateTopic "home/freezer/freezer1/dehumidifier/state"
#define HumiditySettingTopic "home/freezer/freezer1/humidityController/setting"
#define HumidityControllerAvailabilityTopic "home/freezer/freezer1/humidityController/available"
#define TemperatureSensorTopic "home/freezer/freezer1/temperature"
#define HumiditySensorTopic "home/freezer/freezer1/humidity"

void setup()
{
  pinMode(0, OUTPUT);
  //pinMode(3, OUTPUT);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, 1);

  EEPROM.begin(64);
  
  humiditySetting = EEPROM.read(HUMIDITY_SETTING_EEPROM_ADDR);

  if (humiditySetting < 20 || humiditySetting > 90)
  {
    //initialize
    humiditySetting = 65;

    Serial.print("Initialize eeprom humdifity setting = ");
    Serial.println(humiditySetting);
    EEPROM.write(HUMIDITY_SETTING_EEPROM_ADDR, humiditySetting);
    EEPROM.commit();
  }

  Serial.print("\nInitial dehumidifier setting is ");
  Serial.println(humiditySetting);

  //turn off dehumidifier at the beginning
  digitalWrite(0, HIGH);
  dehumidifierState = 0;

  dht.begin();
  
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

  delay(3000);
}

void loop()
{
  mqttReconnect();

  yield(); 

  mqttClient.loop();

  yield();
  
  //wemoManager.serverLoop();
  //yield();

  dhtRead();

  yield();

  toggleDehumidifier();

  yield();
  
  publishDehumidifierState();

  yield();
  
  httpServer.handleClient();
}

void toggleDehumidifier() {
  static long last_dehumidifier_toggle_time = 0;

  if (isnan(h)) {
      if (dehumidifierState == 1) {
        // turn off dehumidifier if unknown humidity
        digitalWrite(0, HIGH);
        dehumidifierState = 0;
        pendingPublish = 1;
      }
      return;
  }

  if ( (h > (humiditySetting + 2.5)) && (dehumidifierState == 0)) {
      // turn on
      unsigned long now = millis();
      if (now - last_dehumidifier_toggle_time > 180000) {
        last_dehumidifier_toggle_time = now;
        digitalWrite(0, LOW);
        dehumidifierState = 1;
        pendingPublish = 1;
      }
  }
  else if ( (h < (humiditySetting - 2.5)) && (dehumidifierState == 1)) {
      // turn off
      unsigned long now = millis();
      if (now - last_dehumidifier_toggle_time > 180000) {
        last_dehumidifier_toggle_time = now;
        digitalWrite(0, HIGH);
        dehumidifierState = 0;
        pendingPublish = 1;
      }
  }
}

int getDehumidifierState() {
    return dehumidifierState;
}

void publishDehumidifierState() {
  if (pendingPublish == 1) {
    if (mqttClient.connected()) {
      if (getDehumidifierState())
          mqttClient.publish(DehumidifierStateTopic, "ON", true);
      else
          mqttClient.publish(DehumidifierStateTopic, "OFF", true);
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

  if (pn >= 20 && pn <= 90){
    humiditySetting = pn;
    EEPROM.write(HUMIDITY_SETTING_EEPROM_ADDR, humiditySetting);
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
      if (mqttClient.connect(clientId.c_str(), HumidityControllerAvailabilityTopic, 0, true, "offline")) {
        Serial.println("connected");

        mqttClient.subscribe(HumiditySettingTopic);

        mqttClient.publish(HumidityControllerAvailabilityTopic, "online", true);

        pendingPublish = 1;
      } else {
        Serial.println("failed");
      }
    }
  }
}

void dhtRead() {
  static long lastReadAttempt = 0;
  long now = millis();
  if ((now - lastReadAttempt) > 5000) {
    lastReadAttempt = now;
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    h = dht.readHumidity();

    // Read temperature as Celsius (the default)
    // float t = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    f = dht.readTemperature(true);

    yield();
    
    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(f)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }
  
    // Compute heat index in Fahrenheit (the default)
    // float hif = dht.computeHeatIndex(f, h);
    // Compute heat index in Celsius (isFahreheit = false)
    // float hic = dht.computeHeatIndex(t, h, false);
  
    //Serial.print("Humidity: ");
    //Serial.print(h);
    //Serial.print(" %\t");
    //Serial.print("Temperature: ");
    //Serial.print(t);
    //Serial.print(" *C ");
    //Serial.print(f);
    //Serial.println(" *F\t");
    //Serial.print("Heat index: ");
    //Serial.print(hic);
    //Serial.print(" *C ");
    //Serial.print(hif);
    //Serial.println(" *F");

    if (mqttClient.connected()) {
      mqttClient.publish(TemperatureSensorTopic, String(f).c_str(), false);
      mqttClient.publish(HumiditySensorTopic, String(h).c_str(), false);
    }
  }
}

