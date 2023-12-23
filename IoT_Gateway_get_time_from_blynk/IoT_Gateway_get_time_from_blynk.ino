/*
 Basic ESP8266 MQTT example
 This sketch demonstrates the capabilities of the pubsub library in combination
 with the ESP8266 board/library.
 It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic" every two seconds
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary
  - If the first character of the topic "inTopic" is an 1, switch ON the ESP Led,
    else switch it off
 It will reconnect to the server if the connection is lost using a blocking
 reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
 achieve the same result without blocking the main loop.
 To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"
*/
#define BLYNK_TEMPLATE_ID "TMPL6DYvmYhae"
#define BLYNK_TEMPLATE_NAME "final"
#define BLYNK_AUTH_TOKEN "uRZJj__2_ka8agIPiWQ-GEeTzGp-LVO6"
#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <BlynkSimpleEsp8266.h>

//Define pins
int LED1 = D4;
int LED2 = D2;


// Update these with values suitable for your network.

char auth[] = BLYNK_AUTH_TOKEN;
const char* ssid = "K3";
const char* pass = "hoanghoang02";
const char* mqtt_server = "test.mosquitto.org";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;
int count = 0;

int temp1 = 0;
int temp2 = 0;

BLYNK_WRITE(V1) 
{
  int value_LED1 = param.asInt();
  temp1 = value_LED1;
}
BLYNK_WRITE(V3) 
{
  int value_LED2 = param.asInt();
  temp2 = value_LED2;
}

void setup_wifi() {

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

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
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
      client.publish("controlData", "hello IoT Node...");
      // ... and resubscribe
      client.subscribe("sensingData");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(57600);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  Blynk.begin(auth, ssid, pass);
}

void loop() {
  Blynk.run();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;
    ++value;

    StaticJsonDocument<256> jsonDoc; // Adjust the capacity as needed
    jsonDoc["value_of_hour"] = temp1;
    jsonDoc["value_of_minute"] = temp2;

    String jsonString; // serialize
    serializeJson(jsonDoc, jsonString);
    const char* payload = jsonString.c_str(); // Convert to char*

    client.publish("sensorData", payload); // publish it
  }
}
