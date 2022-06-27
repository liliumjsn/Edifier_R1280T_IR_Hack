#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>
#include "Wire.h"
#include "EspMQTTClient.h"
#include <Ticker.h> 
#include <ArduinoJson.h>
#include "SSD1306.h"  
#include "icons.h"

SSD1306 display(0x3c, SDA, SCL);
unsigned long lastDisplayResetMillis = 0;
uint16_t displayTimeout = 5000; //millis

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


uint8_t clientUpdateEveryMillis = 100;
unsigned long clientLastMillis = 0;

EspMQTTClient client(
  "xxx",
  "xxx",
  "xxx",  // MQTT Broker server ip
  "xxx",   // Can be omitted if not needed
  "xxx",   // Can be omitted if not needed
  "TVSpeakers"      // Client name that uniquely identify your device
);

Ticker publishTimer;
Ticker checkConnectionTimer;

const uint16_t kRecvPin = 13;

uint16_t autoPublishTimerSeconds = 60;
IRrecv irrecv(kRecvPin);
decode_results results;
uint8_t master_volume = 60; //-26bB
bool isMuted = false;
bool stateChanged = false;
bool isDiplayOn = true;
int8_t command = -1;
uint16_t commandTimeOut = 1000; //millis
unsigned long commandPressTS = 0;
bool showCommand;
int8_t lastCommand;
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
void commandLoop();
void commandHandler(int8_t com);
void displayTimeoutLoop();

void setup() {  
  delay(1000);
  Serial.begin(115200);  
  Wire.begin();
  irrecv.enableIRIn();  // Start the receiver
  while (!Serial)  // Wait for the serial connection to be establised.
    delay(50);
  Serial.println("Setup begin");
  delay(200);
  if(!display.init()) Serial.println("Oled failed");
  delay(500);
  displaySplash();
  ampInit();
  delay(1200);    
  client.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  client.enableHTTPWebUpdater(); // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overridded with enableHTTPWebUpdater("user", "password").
  client.enableOTA(); // Enable OTA (Over The Air) updates. Password defaults to MQTTPassword. Port is the default OTA port. Can be overridden with enableOTA("password", port).
  client.enableLastWillMessage("TVSpeakers/lastwill", "I am going offline");  // You can activate the retain flag by setting the third parameter to true  
  delay(200);
  publishTimer.attach(autoPublishTimerSeconds, publishState);
  delay(200);  
  setVolumeLevel(master_volume);  
  Serial.println("Setup end");
}

void loop() {  
  irLoop();

  client.loop();

  commandLoop();

  displayTimeoutLoop();  
}

void onConnectionEstablished()
{
  client.subscribe(MQTT_TOPIC_CONFIG, [](const String & payload) {
    Serial.print(F("Message arrived in topic: "));  Serial.println(MQTT_TOPIC_CONFIG); 
    Serial.print(F("Message:"));
    Serial.println(payload);
    Serial.println(F("-----------------------")); 
    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload);
    bool volUp = doc["volUp"];
    bool volDown = doc["volDown"];
    bool mute = doc["mute"];
    if(volUp) volumeUp();
    if(volDown) volumeDown();
    if(mute) volumeMute();
  });
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
  if(stateChanged && client.isMqttConnected()){
    stateChanged = false;
    DynamicJsonDocument doc(256);
    doc["volume"] = getUserVolume();
    doc["muted"] = isMuted;
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
  lastCommand = com;
  showCommand = true;
  updateScreen();  
}

void volumeUp(){  
  if(master_volume > 0) master_volume -= getVolStepSize();  
  setVolumeLevel(master_volume);
  isMuted = false;
}

void volumeDown(){  
  if(master_volume < 150) master_volume += getVolStepSize();
  if(master_volume >= 150) master_volume = 150;
  setVolumeLevel(master_volume);
  isMuted = false;  
}

void volumeMute(){
  if(isMuted){
    isMuted = false;
    setVolumeLevel(master_volume);    
  }
  else {
    isMuted = true;
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
  if(isMuted)Serial.println("ON");
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
  if(isMuted){
    display.drawXbm(40, 0, 50, 50, epd_bitmap_mute_icon);
  }else if(showCommand){
    display.setFont(Roboto_40);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, "#" + String(lastCommand) + "#");
    showCommand = false;
  }else{
    display.setFont(Roboto_40);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, String(getUserVolume()));
  }

  if(client.isMqttConnected()) display.drawXbm(0, 49, 15, 15, epd_bitmap_mqtt_ok_icon);
  else display.drawXbm(0, 49, 15, 15, epd_bitmap_mqtt_failed_icon);

  if(client.isWifiConnected()) display.drawXbm(113, 49, 15, 15, epd_bitmap_wifi_on_icon);
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
  uint8_t dv = ((150 - master_volume)/2);
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

    if(results.value == COM_0_IR_CODE) commandHandler(0);
    else if(results.value == COM_1_IR_CODE) commandHandler(1);
    else if(results.value == COM_2_IR_CODE) commandHandler(2);
    else if(results.value == COM_3_IR_CODE) commandHandler(3);
    
    irrecv.resume();  // Receive the next value
  }
}

void commandHandler(int8_t com){
  if(command >=0) {
    if((unsigned long) millis() - commandPressTS > 200){
      publishCommand(10*(command+1) + com);
      commandPressTS = millis();
      command = -1;
    }    
  }
  else{
    if((unsigned long) millis() - commandPressTS > 200){
      commandPressTS = millis();
      command = com;
    }
}
}

void commandLoop(){
  if(command >=0 && (unsigned long) millis() - commandPressTS > commandTimeOut){
    publishCommand(command);
    commandPressTS = millis();
    command = -1;
  }
}

void displayTimeoutLoop(){  
  if(!isMuted && isDiplayOn && ((unsigned long) millis() - lastDisplayResetMillis > displayTimeout)){
    setDisplayBlack();
  }    
}