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

// light on/off/state callbacks
int lightOn();
int lightOff();
int getLightState();
void manualToggle();
void publishLightState();

void mqttCallback(char* topic, byte* payload, unsigned int length);
void mqttReconnect();

void dhtRead();

//------- Replace the following! ------
#define HOST "dining-light"
#define SSID "xxxxx"       // your network SSID (name)
#define PASSWORD "xxxxx"  // your network key
#define LIGHT_NAME "dining light"

#define LIGHT_ON 78
#define LIGHT_OFF 80

#define LIGHT_STATE_EEPROM_ADDR 0

volatile byte lightState;
volatile unsigned long last_light_toggle_time = 0;
volatile byte pendingPublish = 0;

WemoManager wemoManager;
WemoSwitch *light = NULL;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

#define DHTPIN 2 
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

WiFiClient wifiClient;

#define MQTT_SERVER "192.168.1.88"

PubSubClient mqttClient(wifiClient);

#define LightStateTopic "home/light/dining-light/state"
#define LightSwitchTopic "home/light/dining-light/switch"
#define LightAvailabilityTopic "home/light/dining-light/available"
#define TemperatureSensorTopic "home/sensor/hall/temperature"
#define HumiditySensorTopic "home/sensor/hall/humidity"

void setup()
{
  pinMode(0, OUTPUT);
  pinMode(3, INPUT_PULLUP);

  dht.begin();

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY, 1);
 
  EEPROM.begin(64);
  
  lightState = EEPROM.read(LIGHT_STATE_EEPROM_ADDR);

  if (lightState != LIGHT_ON && lightState != LIGHT_OFF)
  {
    //initialize
    Serial.print("Initialize eeprom light state, ");
    EEPROM.write(LIGHT_STATE_EEPROM_ADDR, LIGHT_OFF);
    EEPROM.commit();
    lightState = LIGHT_OFF;
  }

  last_light_toggle_time = millis();

  if (lightState == LIGHT_ON)
  {
    Serial.println("\nInitial light state is ON");
    digitalWrite(0, LOW);
  }
  else
  {
    Serial.println("\nInitial light state is OFF");
    digitalWrite(0, HIGH);
  }

  attachInterrupt(digitalPinToInterrupt(3), manualToggle, CHANGE);

  delay(100);
  
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

  wemoManager.begin();
  // Format: Alexa invocation name, local port no, on callback, off callback
  light = new WemoSwitch(LIGHT_NAME, 81, lightOn, lightOff, getLightState);
  wemoManager.addDevice(*light);

  delay(100);

  MDNS.begin(HOST);

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", HOST);

  delay(100);

  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(mqttCallback);

  delay(100);
}

void loop()
{
  mqttReconnect();

  mqttClient.loop();

  dhtRead();

  wemoManager.serverLoop();

  publishLightState();
  
  httpServer.handleClient();
}

int lightOn() {
    last_light_toggle_time = millis();
    
    lightState = LIGHT_ON;

    digitalWrite(0, LOW);

    EEPROM.write(LIGHT_STATE_EEPROM_ADDR, LIGHT_ON);
    EEPROM.commit();

    pendingPublish = 1;

    return 1;
}

int lightOff() {
    last_light_toggle_time = millis();

    lightState = LIGHT_OFF;

    digitalWrite(0, HIGH);

    EEPROM.write(LIGHT_STATE_EEPROM_ADDR, LIGHT_OFF);
    EEPROM.commit();

    pendingPublish = 1;
    
    return 0;
}

int getLightState() {
    if (lightState == LIGHT_OFF)
       return 0;
    else
       return 1;
}

voiid publishLightState() {
  if (pendingPublish == 1) {
    if (mqttClient.connected()) {
      if (lightState == LIGHT_OFF)
          mqttClient.publish(LightStateTopic, "OFF", true);
      else
          mqttClient.publish(LightStateTopic, "ON", true);
    }
    pendingPublish = 0;
  }
}

void manualToggle() {
   unsigned long interrupt_time = millis();
   // If interrupts come faster than 800ms, assume it's a bounce and ignore
   if (interrupt_time - last_light_toggle_time > 500) 
   {
     if (lightState == LIGHT_ON)
       lightOff();
     else
        lightOn();
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

  if (payload[0] == 'O' && payload[1] == 'N') 
    lightOn();
  else if (payload[0] == 'O' && payload[1] == 'F' && payload[2] == 'F')
    lightOff();
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
      if (mqttClient.connect(clientId.c_str(), LightAvailabilityTopic, 0, true, "offline")) {
        Serial.println("connected");

        mqttClient.subscribe(LightSwitchTopic);

        mqttClient.publish(LightAvailabilityTopic, "online", true);

        if (lightState == LIGHT_ON)
          mqttClient.publish(LightStateTopic, "ON", true);
        else
          mqttClient.publish(LightStateTopic, "OFF", true);
      } else {
        Serial.println("failed");
      }
    }
  }
}

void dhtRead() {
  static long lastReadAttempt = 0;
  long now = millis();
  if ((now - lastReadAttempt) > 60000) {
    lastReadAttempt = now;
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    // float t = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    float f = dht.readTemperature(true);
  
    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(f)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }
  
    // Compute heat index in Fahrenheit (the default)
    // float hif = dht.computeHeatIndex(f, h);
    // Compute heat index in Celsius (isFahreheit = false)
    // float hic = dht.computeHeatIndex(t, h, false);
  
    Serial.print("Humidity: ");
    Serial.print(h);
    Serial.print(" %\t");
    Serial.print("Temperature: ");
    //Serial.print(t);
    //Serial.print(" *C ");
    Serial.print(f);
    Serial.println(" *F\t");
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

