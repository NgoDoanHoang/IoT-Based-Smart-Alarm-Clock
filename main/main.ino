#include <LiquidCrystal_I2C.h>
#include "RTClib.h"
#include <Wire.h> // Library for I2C communication
#include <SPI.h>  // not used here, but needed to prevent a RTClib compile error
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

//wifi and broker
const char* ssid = "K3";
const char* pass = "hoanghoang02";
const char* mqtt_server = "test.mosquitto.org";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;
//---------------------------------------------------------------

//others
#define buzzerPin D6
int f_stop_counting_timeUntilAlarm = 1;
int f_to_prevent_2nd_afjustment_if_any = 1;
const int sleep_cycle_duration = 120;
const int length_of_first_2_stages = 60;
int total_minute_after_adjust = 0;
int total_minutes_slept = 0;
int updated_hour = 0;
int updated_minute = 0;
int time_to_fall_asleep = 10;   //assume that an average being will fall asleep in 20 minutes

//defines variables for SR04
#define trigPin D4
#define echoPin D3
long duration;
int distance;

//defines variables for SR501
#define SR501 D7
int sleepyTime = 0;
bool state = false;

//defines variables for RTC
int Year = 2023;          
int Month = 12;
int Day = 24;
int Hour = 0;
int Minute = 0;
int Second = 0;
#define CLOCK_INTERRUPT_PIN D5
int temp_alarm_hour = 0;
int temp_alarm_minute = 0;

// initialize object, class, etc
RTC_DS3231 RTC; 
LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 16, 2);    // set LCD address, number of columns and rows
DateTime startSleepingTime;
DateTime updatedAlarmTime;
TimeSpan timeUntilAlarm = 0;

void setup_wifi() 
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) 
{
 // Serial.println();

  StaticJsonDocument<256> jsonDoc; // Adjust the capacity as needed
  deserializeJson(jsonDoc, payload, length);
  
  // Extract values from the JSON data
  int blynk_hour = jsonDoc["value_of_hour"];
  int blynk_minute = jsonDoc["value_of_minute"];
  
  // Print the values
  Serial.print("Alarm_hour_from_Blynk is: ");
  Serial.println(blynk_hour);
  Serial.print("Alarm_minute_from_Blynk is: ");
  Serial.print(blynk_minute);
  Serial.println();

  temp_alarm_hour = blynk_hour;
  temp_alarm_minute = blynk_minute;
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("sensingData", "hello IoT Gateway...");
      // ... and resubscribe
      client.subscribe("sensorData");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



void setup()
{
  //basic setup
  Serial.begin(57600);

  //wifi and mqtt
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);  

  //buzzer
  pinMode(buzzerPin, OUTPUT);
  
  //SR04
  pinMode(trigPin,OUTPUT);   // trig phát tín hiệu    //setup for sr04
  pinMode(echoPin,INPUT);    // echo nhận tín hiệu

  //initialize LCD
  lcd.init();              
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Real Time Clock");
  delay(3000);
  lcd.clear();

  //RTC
  RTC.begin();  // Init RTC
  RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));  // Time and date is expanded to date and time on your computer at compiletime
  pinMode(CLOCK_INTERRUPT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CLOCK_INTERRUPT_PIN), onAlarm, FALLING);
  RTC.clearAlarm(1);
  RTC.clearAlarm(2);
  RTC.disable32K();
  RTC.writeSqwPinMode(DS3231_OFF);
  RTC.disableAlarm(2);

  //SR501
  pinMode(SR501, INPUT);
}


void loop()
{
  if (!client.connected()) 
  {
    reconnect();
  } client.loop();
  
  unsigned long noww = millis();
  if (noww - lastMsg > 1000) 
  {
    lastMsg = noww;
    ++value;
    client.publish("sensingData", msg);
  }


  DateTime alarmTime(Year, Month, Day, temp_alarm_hour, temp_alarm_minute, Second);

  DateTime now = RTC.now();

  //DateTime alarmTime(Year, Month, Day, Hour, Minute, Second); // Year, Month, Day, Hour, Minute, Second

  if(!RTC.setAlarm1(alarmTime, DS3231_A1_Date)) // Set the mode for the alarm trigger
  {
    Serial.println("Error, alarm wasn't set!");
  }

  // Display on LCD & serial monitor
  displayDateTime(now);

  // Check for the alarm
  if (RTC.alarmFired(1)) 
  {
    Serial.println("Alarm occurred!");
    digitalWrite(buzzerPin, HIGH);
    Serial.println("Give me your hand to stop!");
    howToStop();
  }

  //now count sleep time
  countSleepyTime();

  // Calculate time until alarm only when sleepyTime has reached time_to_fall_asleep
  timeUntilAlarmFunction(alarmTime);

  //now update alarm time so no one wakes up at REM stage
  updateAlarm(alarmTime);

}

void displayDateTime(DateTime currentTime) 
{ 
  //print to serial moniter
  Serial.print(currentTime.year(), DEC);
  Serial.print('/');
  Serial.print(currentTime.month(), DEC);
  Serial.print('/');
  Serial.print(currentTime.day(), DEC);
  Serial.print(' ');
  Serial.print(currentTime.hour(), DEC);
  Serial.print(':');
  Serial.print(currentTime.minute(), DEC);
  Serial.print(':');
  Serial.print(currentTime.second(), DEC);
  Serial.println();

  //then LCD
  lcd.clear(); // Clear the LCD
  
  lcd.setCursor(0, 0);
  lcd.print("Date: ");
  lcd.print(currentTime.year(), DEC);
  lcd.print("-");
  lcd.print(currentTime.month(), DEC);
  lcd.print("-");
  lcd.print(currentTime.day(), DEC);

  lcd.setCursor(0, 1);
  lcd.print("Time: ");
  lcd.print(currentTime.hour(), DEC);
  lcd.print(":");
  lcd.print(currentTime.minute(), DEC);
  lcd.print(":");
  lcd.print(currentTime.second(), DEC);
  
  delay(1000); // Delay to control LCD refresh rate
}

ICACHE_RAM_ATTR void onAlarm() 
{
  Serial.println("Alarm occurred!");
  digitalWrite(D6, HIGH);
}

void howToStop() 
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  distance = int(duration / 2 / 29.412);

  // If the hand is within 20cm, stop the alarm
  if (distance < 20) {
    Serial.println("Hand detected. Stopping alarm.");
    RTC.clearAlarm(1);
    digitalWrite(D6, LOW); // Turn off the alarm indicator - buzzer
  }

  //set this to 0
  f_stop_counting_timeUntilAlarm = 0;
}

void countSleepyTime()
{
  if(digitalRead(SR501) == HIGH)
  {
    //Serial.println("Motion detected! Start counting!");
    state = true;
  }
  else{}

  if(state == true)
  {
    sleepyTime++;
    Serial.print("Time Slept: ");
    Serial.println(sleepyTime);
    Serial.println();

    if(sleepyTime < time_to_fall_asleep)
    {
      if(digitalRead(SR501) == HIGH)
      {
        sleepyTime = 0;
      }
    }
    
    if(sleepyTime == time_to_fall_asleep)
    {
      startSleepingTime = RTC.now();
      Serial.print("This is startSleepingTime value: ");
      Serial.print(startSleepingTime.hour(), DEC);
      Serial.print(':');
      Serial.print(startSleepingTime.minute(), DEC);
      Serial.print(':');
      Serial.print(startSleepingTime.second(), DEC);
      Serial.println();
    }

    if(digitalRead(SR501) == HIGH)
    {
        sleepyTime = sleepyTime - 1;
    }
  }
}

void timeUntilAlarmFunction(DateTime alarmTime)
{
  if(sleepyTime < time_to_fall_asleep)
  {
    Serial.println();
    Serial.print("Not yet");
    Serial.println();
  }
  else if(sleepyTime == time_to_fall_asleep) 
  {
    timeUntilAlarm = alarmTime - startSleepingTime;
    Serial.print("Time until alarm: ");
    Serial.print(timeUntilAlarm.days());
    Serial.print(" days ");
    Serial.print(timeUntilAlarm.hours());
    Serial.print(" hours ");
    Serial.print(timeUntilAlarm.minutes()); // Minutes left
    Serial.print(" minutes "); 
    Serial.println();
  }
  else
  {
    if(f_stop_counting_timeUntilAlarm)
    {
      TimeSpan timeLeftUntilAlarm = alarmTime - RTC.now();          
      Serial.print("Time left until alarm: ");
      Serial.print(timeLeftUntilAlarm.days());
      Serial.print(" days ");
      Serial.print(timeLeftUntilAlarm.hours());
      Serial.print(" hours ");
      Serial.print(timeLeftUntilAlarm.minutes()); // Minutes left      
      Serial.print(" minutes "); 
      Serial.println();
    }
  }
}

void updateAlarm(DateTime alarmTime)
{ 
  if(f_to_prevent_2nd_afjustment_if_any)      //to update total_minutes_slept, if not, it will be overide with old value, not total_minute_after_adjust
  {
    total_minutes_slept = timeUntilAlarm.hours() * 60 + timeUntilAlarm.minutes();
  }
  if(total_minutes_slept != 0)
  { 
    //Serial.println();
    Serial.print("total_minutes_slept = ");
    Serial.print(total_minutes_slept);
    Serial.println();
  }

  if(total_minutes_slept % sleep_cycle_duration > length_of_first_2_stages && f_to_prevent_2nd_afjustment_if_any == 1)
  {
    Serial.print("Your alarm will be modified as such: ");
    Serial.println();
    f_to_prevent_2nd_afjustment_if_any = 0;

    total_minute_after_adjust = total_minutes_slept - ((total_minutes_slept % sleep_cycle_duration) - length_of_first_2_stages);
    Serial.print("total_minute_after_adjust = ");
    Serial.print(total_minute_after_adjust);
    Serial.println();
    total_minutes_slept = total_minute_after_adjust;

  updated_hour = total_minute_after_adjust / 60;
  updated_minute = total_minute_after_adjust % 60;

  //update alarm 1
  TimeSpan elapsed_time(0, updated_hour, updated_minute, 0);
  updatedAlarmTime = startSleepingTime + elapsed_time;                    
  RTC.setAlarm1(updatedAlarmTime, DS3231_A1_Date);

  Serial.print("This is old alarm time: ");
  Serial.print(alarmTime.year(), DEC);
  Serial.print('/');
  Serial.print(alarmTime.month(), DEC);
  Serial.print('/');
  Serial.print(alarmTime.day(), DEC);
  Serial.print(' ');
  Serial.print(alarmTime.hour(), DEC);
  Serial.print(':');
  Serial.print(alarmTime.minute(), DEC);
  Serial.print(':');
  Serial.print(alarmTime.second(), DEC);
  Serial.println();

  Serial.print("This is updated alarm time: ");
  Serial.print(updatedAlarmTime.year(), DEC);
  Serial.print('/');
  Serial.print(updatedAlarmTime.month(), DEC);
  Serial.print('/');
  Serial.print(updatedAlarmTime.day(), DEC);
  Serial.print(' ');
  Serial.print(updatedAlarmTime.hour(), DEC);
  Serial.print(':');
  Serial.print(updatedAlarmTime.minute(), DEC);
  Serial.print(':');
  Serial.print(updatedAlarmTime.second(), DEC);
  Serial.println();  
  
  //this is a savage way to stop countdown since i cant figure out any other way
  timeUntilAlarmFunction(updatedAlarmTime);
  f_stop_counting_timeUntilAlarm = 0;
  }
}
