#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <ESP8266WiFi.h>
#include <IRrecv.h>
#include <IRutils.h>
#include "Wire.h"
#include <PubSubClient.h>
#include <Ticker.h> 
#include <ArduinoJson.h>
#include "SSD1306.h"  
#include "icons.h"

SSD1306 display(0x3c, SDA, SCL);
unsigned long lastDisplayResetMillis = 0;
uint16_t displayTimeout = 10000; //millis

ICACHE_RAM_ATTR

#define AMPLIFIER_I2C_ADDRES_DEC 0x1B
#define AMPLIFIER_I2C_MV_REGISTER 0x07
#define AMPLIFIER_I2C_CH1_REGISTER 0x08
#define AMPLIFIER_I2C_CH2_REGISTER 0x09
#define MQTT_TOPIC_STATE "home/livingroom/tvspeakers/state"
#define MQTT_TOPIC_CONFIG "home/livingroom/tvspeakers/config"
#define MQTT_TOPIC_COMMAND "home/livingroom/tvspeakers/command"

#define COM_0_IR_CODE 3772790473
#define COM_1_IR_CODE 3772786903
#define COM_2_IR_CODE 3772819543
#define COM_3_IR_CODE 3772803223


WiFiClient espClient;
PubSubClient client(espClient);
uint8_t clientUpdateEveryMillis = 100;
unsigned long clientLastMillis = 0;

const char* ssid = "XXX"; // Enter your WiFi name
const char* password =  "XXX"; // Enter WiFi password
const char* mqttServer = "XXX";
const int mqttPort = 1883;
const char* mqttUser = "mqtt_node";
const char* mqttPassword = "XXX";

Ticker publishTimer;
Ticker checkConnectionTimer;

const uint16_t kRecvPin = 14;

uint16_t autoPublishTimerSeconds = 60;
IRrecv irrecv(kRecvPin);
decode_results results;
uint8_t master_volume = 60; //-26bB
bool muted = false;
bool stateChanged = false;
bool wifiOK = false;
bool mqttOk = false;
bool isDiplayOn = true;
void volumeUp();
void volumeDown();
void volumeMute();
void setVolumeLevel(uint8_t);
void ampInit();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void checkConnection();
void publishState();
uint8_t getVolStepSize();
void updateScreen();
uint8_t getUserVolume();
void displaySplash();
void resetDisplayTimeout();
void setDisplayBlack();
void irLoop();

void setup() {
  delay(1000);
  Serial.begin(115200);
  Wire.begin();
  irrecv.enableIRIn();  // Start the receiver
  while (!Serial)  // Wait for the serial connection to be establised.
    delay(50);
  delay(200);
  if(!display.init()) Serial.println("Oled failed");
  delay(500);
  displaySplash();
  ampInit();
  delay(400);  
  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);
  checkConnection();  
  client.subscribe(MQTT_TOPIC_CONFIG);
  delay(200);
  publishTimer.attach(autoPublishTimerSeconds, publishState);
  checkConnectionTimer.attach(autoPublishTimerSeconds/2, checkConnection);
  delay(200);  
  setVolumeLevel(master_volume);
}

void loop() {
  irLoop();

  if((unsigned long) millis() - clientLastMillis > clientUpdateEveryMillis){
    client.loop();
    clientLastMillis = millis();
  }  
  
  if(isDiplayOn && ((unsigned long) millis() - lastDisplayResetMillis > displayTimeout)){
    setDisplayBlack();
  }  
  
}

void mqttCallback(char* topic, byte* payload, unsigned int length) { 
  
  Serial.print(F("Message arrived in topic: "));  Serial.println(topic); 
  Serial.print(F("Message:"));
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  } 
  Serial.println();
  Serial.println(F("-----------------------")); 
  DynamicJsonDocument doc(256);
  deserializeJson(doc, payload);
  bool volUp = doc["volUp"];
  bool volDown = doc["volDown"];
  bool mute = doc["mute"];
  if(volUp) volumeUp();
  if(volDown) volumeDown();
  if(mute) volumeMute();
}

void publishState(){
  if(stateChanged){
    stateChanged = false;
    DynamicJsonDocument doc(256);
    doc["volume"] = getUserVolume();
    doc["muted"] = muted;
    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);
    Serial.print(F("Publishing state:"));
    Serial.println(jsonBuffer);
    client.publish(MQTT_TOPIC_STATE, jsonBuffer); //Topic name
  }
}

void publishCommand(uint8_t com){
  DynamicJsonDocument doc(256);
  doc["command"] = com;
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  Serial.print(F("Publishing command:"));
  Serial.println(jsonBuffer);
  client.publish(MQTT_TOPIC_COMMAND, jsonBuffer); //Topic name
}

void checkConnection(){
  Serial.println(F("Checking connection"));
  uint8_t cnt = 0;
  if(WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    WiFi.mode(WIFI_STA);
    while (WiFi.status() != WL_CONNECTED && cnt < 25) {
      delay(500);
      Serial.println(F("Connecting to WiFi.."));
      cnt++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiOK = true;
      Serial.println(F("Connected to the WiFi network"));
    }
    else {
      Serial.println(F("WiFi connection failed"));
      wifiOK = false;
    }
  } else{
    Serial.println(F("Wifi seems OK")); 
    wifiOK = true;
  }
  cnt = 0;
  if(client.state() != MQTT_CONNECTED){
    while (client.state() != MQTT_CONNECTED && cnt < 2) {
      Serial.println(F("Connecting to MQTT..."));   
      if (client.connect("TVSpeakers", mqttUser, mqttPassword )) {   
        Serial.println(F("connected"));     
        mqttOk = true;
      } else {   
        Serial.print(F("failed with state "));
        Serial.println(client.state());
        delay(2000);   
        mqttOk = false;
      }
      cnt++;
    }
  }else {
    Serial.println("MQTT seems OK"); 
    mqttOk = true;
  }
  //updateScreen();
}

void volumeUp(){  
  if(master_volume > 0) master_volume -= getVolStepSize();  
  setVolumeLevel(master_volume);
  muted = false;
}

void volumeDown(){  
  if(master_volume < 100) master_volume += getVolStepSize();
  if(master_volume >= 100) master_volume = 100;
  setVolumeLevel(master_volume);
  muted = false;  
}

void volumeMute(){
  if(muted){
    muted = false;
    setVolumeLevel(master_volume);    
  }
  else {
    muted = true;
    setVolumeLevel(255);
  }  
}

void ampInit(){
  Wire.beginTransmission(AMPLIFIER_I2C_ADDRES_DEC);
  Wire.write(0x1B);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(100);
  Wire.beginTransmission(AMPLIFIER_I2C_ADDRES_DEC);
  Wire.write(0x05);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(100);
}

void setVolumeLevel(uint8_t vol){
  Serial.print("Volume: ");
  Serial.println(getUserVolume());
  Serial.print("Mute: ");
  if(muted)Serial.println("ON");
  else Serial.println("OFF");
  Wire.beginTransmission(AMPLIFIER_I2C_ADDRES_DEC);
  Wire.write(AMPLIFIER_I2C_MV_REGISTER);
  Wire.write(vol);
  Wire.endTransmission();
  stateChanged = true;
  updateScreen();
}

uint8_t getVolStepSize(){
  if(master_volume > 50) return 5;
  else if(master_volume > 30) return 3;
  else if(master_volume > 15) return 2;
  else return 1;
}

void updateScreen(){  
  display.clear();
  if(muted){
    display.drawXbm(40, 0, 50, 50, epd_bitmap_mute_icon);
  }else{
    display.setFont(Roboto_40);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, String(getUserVolume()));
  }

  if(mqttOk) display.drawXbm(0, 49, 15, 15, epd_bitmap_mqtt_ok_icon);
  else display.drawXbm(0, 49, 15, 15, epd_bitmap_mqtt_failed_icon);

  if(wifiOK) display.drawXbm(113, 49, 15, 15, epd_bitmap_wifi_on_icon);
  else display.drawXbm(113, 49, 15, 15, epd_bitmap_wifi_off_icon);
  // write the buffer to the display
  display.display();
  resetDisplayTimeout();
}

void displaySplash(){
  display.clear();
  display.setFont(Roboto_40);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, String("JSN"));
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 40, String("Smart Speakers"));
  display.display();
  resetDisplayTimeout();
}

uint8_t getUserVolume(){
  uint8_t dv = ((100 - master_volume)/2);
  return dv;
}

void resetDisplayTimeout(){
  lastDisplayResetMillis = millis();
  isDiplayOn = true;
}

void setDisplayBlack(){
  display.clear();
  display.display();
  isDiplayOn = false;
}

void irLoop(){
  if (irrecv.decode(&results)) {
    /*
    Samsung Remote:
    Vol+: 3772833823
    Vol-: 3772829743
    Mute: 3772837903
    A: 3772790473 //COM0
    B: 3772786903 //COM1
    C: 3772819543 //COM2
    D: 3772803223 //COM3
    */     
       
    if(results.value == 3772833823) volumeUp();
    else if(results.value == 3772829743) volumeDown();
    else if(results.value == 3772837903) volumeMute();
    else if(results.value == COM_0_IR_CODE) publishCommand(0);
    else if(results.value == COM_1_IR_CODE) publishCommand(1);
    else if(results.value == COM_2_IR_CODE) publishCommand(2);
    else if(results.value == COM_3_IR_CODE) publishCommand(3);
    irrecv.resume();  // Receive the next value
  }
}