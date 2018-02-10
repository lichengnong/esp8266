#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#include "WemoSwitch.h"
#include "WemoManager.h"
#include "CallbackFunction.h"

// prototypes
boolean connectWifi();

//on/off callbacks
void lightOn();
void lightOff();
void manualToggle();

//------- Replace the following! ------
const char* host = "dining-light";
char ssid[] = "CCHOME";       // your network SSID (name)
char password[] = "angela18";  // your network key

WemoManager wemoManager;
WemoSwitch *light = NULL;

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

byte lightState;

const int lightStateAddr = 0;

const byte LIGHT_ON = 78 ;
const byte LIGHT_OFF = 80;

unsigned long last_light_toggle_time = 0;

void setup()
{
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

  pinMode(0, OUTPUT);
  pinMode(3, INPUT_PULLUP );

  EEPROM.begin(64);
  
  lightState = EEPROM.read(lightStateAddr);

  if (lightState != LIGHT_ON && lightState != LIGHT_OFF)
  {
    //initialize
    Serial.print("Initialize eeprom light state, ");
    EEPROM.write(lightStateAddr, LIGHT_OFF);
    EEPROM.commit();
    lightState = LIGHT_OFF;
  }

  last_light_toggle_time = millis();

  if (lightState == LIGHT_ON)
  {
    Serial.println("Initial light state is ON");
    digitalWrite(0, LOW);
  }
  else
  {
    Serial.println("Initial light state is OFF");
    digitalWrite(0, HIGH);
  }

  attachInterrupt(digitalPinToInterrupt(3), manualToggle, CHANGE);

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
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
  light = new WemoSwitch("dining light", 81, lightOn, lightOff);
  wemoManager.addDevice(*light);

  delay(100);

  MDNS.begin(host);

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", host);

  delay(10);
}

void loop()
{
  wemoManager.serverLoop();

  httpServer.handleClient();
}

void lightOn() {
    last_light_toggle_time = millis();
    
    Serial.println("Switch turn on ...");

    lightState = LIGHT_ON;

    digitalWrite(0, LOW);

    EEPROM.write(lightStateAddr, LIGHT_ON);
    EEPROM.commit();
}

void lightOff() {
    last_light_toggle_time = millis();

    Serial.println("Switch turn off ...");

    lightState = LIGHT_OFF;

    digitalWrite(0, HIGH);

    EEPROM.write(lightStateAddr, LIGHT_OFF);
    EEPROM.commit();
}

void manualToggle() {
   unsigned long interrupt_time = millis();
   // If interrupts come faster than 800ms, assume it's a bounce and ignore
   if (interrupt_time - last_light_toggle_time > 800) 
   {
     Serial.println("Manual Toggle Triggered");

     if (lightState == LIGHT_ON)
       lightOff();
     else
        lightOn();
   }
}

