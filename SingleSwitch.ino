#include <ESP8266WiFi.h>
#include <EEPROM.h>

#include "WemoSwitch.h"
#include "WemoManager.h"
#include "CallbackFunction.h"

// prototypes
boolean connectWifi();

//on/off callbacks
void lightOn();
void lightOff();

//------- Replace the following! ------
char ssid[] = "CCHOME";       // your network SSID (name)
char password[] = "angela18";  // your network key

WemoManager wemoManager;
WemoSwitch *light = NULL;

byte lightState;

const int lightStateAddr = 0;

const byte LIGHT_ON = 78 ;
const byte LIGHT_OFF = 80;

const int ledPin = BUILTIN_LED;

void setup()
{
  pinMode(0, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(3, INPUT_PULLUP);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

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

  if (lightState == LIGHT_ON)
  {
    Serial.println("Initial light state is ON");
  }
  else
  {
    Serial.println("Initial light state is OFF");
  }
 
  if (lightState == LIGHT_OFF)
  {
      digitalWrite(ledPin, HIGH); // Wemos BUILTIN_LED is active Low, so high is off
      digitalWrite(0, LOW);
  }
  else
  {
      digitalWrite(ledPin, LOW); // Wemos BUILTIN_LED is active Low, so high is off
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
  light = new WemoSwitch("dining table light", 80, lightOn, lightOff);
  wemoManager.addDevice(*light);

  delay(10);
}

void loop()
{
  wemoManager.serverLoop();
}

void lightOn() {
    Serial.println("Switch turn on ...");

    lightState = LIGHT_ON;

    digitalWrite(0, HIGH);
    digitalWrite(ledPin, LOW);

    EEPROM.write(lightStateAddr, LIGHT_ON);
    EEPROM.commit();
}

void lightOff() {
    Serial.println("Switch turn off ...");

    lightState = LIGHT_OFF;

    digitalWrite(0, LOW);
    digitalWrite(ledPin, HIGH);

    EEPROM.write(lightStateAddr, LIGHT_OFF);
    EEPROM.commit();
}

void manualToggle() {
   static unsigned long last_interrupt_time = 0;
   unsigned long interrupt_time = millis();
   // If interrupts come faster than 800ms, assume it's a bounce and ignore
   if (interrupt_time - last_interrupt_time > 800) 
   {
     Serial.println("Manual Toggle Triggered");

     if (lightState == LIGHT_ON)
       lightOff();
     else
        lightOn();
   }
   last_interrupt_time = interrupt_time;
}

