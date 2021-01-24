
//#define BLYNK_PRINT Serial
#define BLYNK_HEARTBEAT 5

#include "credentials.h" 
#include <EmonLib.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <RCSwitch.h>
#include <ArduinoOTA.h>
#include <TimeLib.h>
#include <WidgetRTC.h>

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = BLINK_KEY;

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWD;

// RF 433 Transmitter
RCSwitch myTX = RCSwitch();

// LEDs
WidgetLED ledArmed(V0);
WidgetLED ledIntrusion(V1);

// Attach virtual serial terminal to Virtual Pin V1
WidgetTerminal terminal(V99);

// RTC Clock
WidgetRTC rtc;



// Energy Consumption
EnergyMonitor emon;
BlynkTimer timer; 
int sensorPin = A0;
float Vrms = 220.0;

// Home security
int armedStatus;
int armTries = 0;
int armTimer;
const int MAX_ARMTRIES = 5;
int toogleBuiltinLed = LOW;

int lastNightmode = 999;
int chkIntrusion, chkArmed = 999;
int digIntrusion, digArmed;
unsigned long lastCheckPins, lastSendData = 0;

char timestr[9];


BLYNK_CONNECTED() { 
    rtc.begin();
    addLog("Connected");
    Blynk.syncVirtual(V13);    
}



BLYNK_WRITE(V9)   // Restart button
{
  if (param.asInt()==1) {
    addLog("Restarting...");
    ESP.restart();    
  }
}

void checkArmed()
{
  int pinArmed = digitalRead(D6); // 1 armed 0 disarmed

  // ARM/Disarm only if not in state
  if (!(armedStatus == pinArmed)) {
    sendRF(CODE_ARM, 5);
    if (armTries <= MAX_ARMTRIES) {
      armTries++;
      timer.disable(armTimer); // Disable time if user clicked to arm/disarm while in loop
      armTimer = timer.setTimeout(500, checkArmed); //check if it was successfull
    } else {
      addLog("Failed to arm after " + String(MAX_ARMTRIES) + " tries");
      armTries = 0;
      Blynk.virtualWrite(V11, pinArmed);
    }
  } else {
    armTries = 0;

  }
  
}

BLYNK_WRITE(V11)
{
  
  armedStatus = param.asInt(); // 1 Arm 0 Disarm
  armTries = 0;
  checkArmed();

  
}

BLYNK_WRITE(V12)
{
  // Portao Garagem
  sendRF(CODE_GARAGE, 15);  
  addLog("Garage code");
  Blynk.virtualWrite(V12, 0);
}

BLYNK_WRITE(V14)
{
  // Portao da frente
  sendRF(CODE_GATE, 15);  
  addLog("Gate code");
  Blynk.virtualWrite(V14, 0);
}


// NIGHTMODE
BLYNK_WRITE(V13)
{
  int pinValue = param.asInt();

  if (pinValue != lastNightmode) {
    lastNightmode = pinValue;
    if (pinValue == 0) {
      digitalWrite(D2, LOW);
      addLog("Night mode deactivated");
    } else {
      digitalWrite(D2, HIGH);
      addLog("Night mode activated");
    }
  }
}

//triggered from app
BLYNK_READ(V20)
{
  // Uptime
  Blynk.virtualWrite(V20, float(millis() / 1000 / 60)); //uptime
}

void sendRF(int code, int repeat)
{
  myTX.setRepeatTransmit(repeat);
  myTX.send(code, 28);
}

void notifyOnArmedStatus(int pinValue)
{ 
  if (pinValue > 0) {
    ledArmed.on();
    addLog("System Armed");
  } else {
    ledArmed.off();
    addLog("System Disarmed");
    Blynk.virtualWrite(V13, 0); //Disable nightmode on disarm
  }

  //Change armbutton
  Blynk.virtualWrite(V11, pinValue);
}

void notifyOnIntrusion(int pinValue)
{
  if (pinValue > 0) {
    ledIntrusion.off();
    addLog("Siren is off.");
  } else {
    
    ledIntrusion.on();
    addLog("Intrusion detected!");
    Blynk.notify("Intrusion Alarm!");
  }
  
}


void addLog(String text) {
  timestr[0] = '0' + hour() / 10;
  timestr[1] = '0' + hour() % 10;
  timestr[2] = ':';
  timestr[3] = '0' + minute() / 10;
  timestr[4] = '0' + minute() % 10;
  timestr[5] = ':';
  timestr[6] = '0' + second() / 10;
  timestr[7] = '0' + second() % 10;
  timestr[8] = '\0';

  
  String line = String(timestr) + " " + text;

  #if defined(BLYNK_PRINT)
    Serial.println(line);
  #endif
  
  terminal.println(line);  
  terminal.flush();
}

void sendData()
{
    double IrmsTemp = emon.calcIrms(1480);
    IrmsTemp = 0.6508*IrmsTemp + 0.2214;
    Blynk.virtualWrite(V5, IrmsTemp); //current
    Blynk.virtualWrite(V6, IrmsTemp * Vrms); //power
    
}

void checkPins() {
  toogleBuiltinLed = (toogleBuiltinLed == LOW) ? HIGH : LOW;
  digitalWrite(BUILTIN_LED, toogleBuiltinLed);

  digArmed = digitalRead(D6);
  if (chkArmed != digArmed) {
    chkArmed = digArmed;
    notifyOnArmedStatus(chkArmed);
  }

  digIntrusion = digitalRead(D8);
  if (chkIntrusion != digIntrusion) {
    chkIntrusion = digIntrusion;
    notifyOnIntrusion(chkIntrusion);
  }
}

void setup()
{
  #if defined(BLYNK_PRINT)
  // Debug console
    Serial.begin(115200);
  #endif

  pinMode(BUILTIN_LED, OUTPUT); //LED ONBOARD

  // Blink Connect
  Blynk.begin(auth, ssid, pass);

  // Transmitter is connected to Arduino Pin D5
  myTX.enableTransmit(D5);
  myTX.setProtocol(6);
  myTX.setRepeatTransmit(10);

  
  pinMode(D6,INPUT_PULLUP); // Armed status pin
  pinMode(D8,INPUT_PULLUP); // Intrusion status pin
  
  // bypass pin
  pinMode(D2,OUTPUT);

  

  // OTA UPDATES
  ArduinoOTA.setHostname("homesecurity");
  
  ArduinoOTA.begin();

  // RTC / Sync interval in seconds (10 minutes)
  setSyncInterval(10 * 60); 

  // Energy Monitor
  emon.current(sensorPin, 30);
  
  // Test
  addLog(F("Blynk v" BLYNK_VERSION ": Device started "));
  addLog("Device IP: " + WiFi.localIP().toString());
  addLog(String("Compile: ") + __DATE__ + " " + __TIME__);
  #if defined(BLYNK_PRINT)
    addLog("Debug enabled");
  #endif
  addLog("End of setup");
}

void loop()
{
  Blynk.run();
  ArduinoOTA.handle();
  timer.run();

  if ((millis() - lastSendData) > 5000) {
    sendData();
    lastSendData = millis();
  }

  if ((millis() - lastCheckPins) > 300) {
    checkPins();
    lastCheckPins = millis();
  }
}
