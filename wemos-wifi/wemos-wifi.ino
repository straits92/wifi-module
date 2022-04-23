/*
 * Set-up instructions for the WeMos:
 * Generate the necessary certificate by running certs-from-mozilla.py
 * Place this /data directory and its contents into the project folder 
 * of the .ino script (in this case, the directory is wemos-wifi)
 * Flash the resulting ./data directory and its contents (certs.ar) onto
 * the chip, using the ESP8266 LittleFS Data Upload tool (imported into 
 * the IDE), via USB-to-microUSB cable
 * Provide wifi.h, broker.h files defining ssid, password, and mqtt 
 * credentials as (const char *)
 * Flash the .ino script onto the WeMos in the Arduino IDE 
 */

/** 
Receives remote MQTT messages and transmits them on Serial. Tests the onboard LED blink. 
**/

/* libraries for necessary methods */
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <string.h>

/* libraries for private data */
#include "wifi.h"
#include "broker.h"

/* libraries dictated by HiveMQ */
#include <time.h>
#include <TZ.h>
#include <FS.h>
#include <LittleFS.h>
#include <CertStoreBearSSL.h>

#define DEVICE_ID "LED_0"
#define DEVICE_ID_STATUS "LED_0_STATUS"
#define DEVICE_ON "device=on"
#define DEVICE_OFF "device=off"
#define STR_CONFIRM_STATE "confirmNewState="
#define MSG_BUFFER_SIZE (500)

// strings to be checked for
const char* device_on = "device=on";
const char* device_off = "device=off";

// the selected LED hardware to control
int ledPin = LED_BUILTIN;

// A single, global CertStore which can be used by all connections.
// Needs to stay live the entire time any of the WiFiClientBearSSLs
// are present.
BearSSL::CertStore certStore;

// the client objects 
WiFiClientSecure espClientSecure;
PubSubClient* clientptr;

// additional from HiveMQ
unsigned long lastMsg = 0;
char msg[MSG_BUFFER_SIZE];
char received[MSG_BUFFER_SIZE];
int value = 0;

// State of the device
int state = LOW;

void initMQTTClient(int verbose) {
  /* setting up the certificate */
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  if (verbose != 0) {Serial.printf("Number of CA certs read: %d\n", numCerts);}
  if (numCerts == 0) {
    if (verbose != 0) {Serial.printf("No certs found. Did you run certs-from-mozilla.py and upload the LittleFS directory before running?\n");}
    return; // Can't connect to anything w/o certs!
  }

  BearSSL::WiFiClientSecure *bear = new BearSSL::WiFiClientSecure();
  // Integrate the cert store with this connection
  bear->setCertStore(&certStore);

  /* set up the client, server, and callback */
  clientptr = new PubSubClient(*bear);
  clientptr->setServer(mqtt_server, mqtt_port_tls);
  clientptr->setCallback(callback);
}

void shortBlink(int duration){
  digitalWrite(ledPin, LOW); 
  delay(duration);
  digitalWrite(ledPin, HIGH); 
  delay(duration*6);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());
   
  // Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP()); // or just the ssid
}

void setDateTime() {
  // You can use your own timezone, but the exact time is not used at all.
  // Only the date is needed for validating the certificates.
  configTime(TZ_Europe_Berlin, "pool.ntp.org", "time.nist.gov");

  // Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(100);
    // Serial.print(".");
    now = time(nullptr);
  }
  // Serial.println();

  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  // Serial.printf("%s %s", tzname[0], asctime(&timeinfo));
}

void printMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void printToMCU(char* topic, byte* payload, unsigned int length) {
  Serial.println();
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

 /*
  * MQTT callback function. It is always called when? we receive a message on the subscribed topics.
  ****/
void callback(char* topic, byte* payload, unsigned int length) {
  int i;
  char string[50];
  char lowerString[50];
  // printMessage(topic, payload, length);
  printToMCU(topic, payload, length);

  // Copy the payload into a C-String and convert all letters into lower-case
  for (i = 0; i < length; i++) {
    string[i] = (char)payload[i];
    lowerString[i] = tolower(string[i]);
  }
  string[i]=0;
  lowerString[i]=0;

  // If the payload contains one of the predefined messages then turn on/off the LED
  uint8_t newState = -1;
  if(strcmp(lowerString, device_on) == 0) {
   newState = LOW;
   digitalWrite(ledPin, LOW); 
   //Serial.println(device_on); // doesn't seem to be read properly on Pico
  }
  else if(strcmp(lowerString, device_off) == 0) {
   newState = HIGH;
   digitalWrite(ledPin, HIGH); 
   //Serial.println(device_off);
  }

  if(newState == LOW || newState == HIGH) {
    state = newState;
    sprintf(msg, "%s%d", STR_CONFIRM_STATE, state);
    clientptr->publish(mqtt_pubs_topic_status, msg);
  }  else {
    clientptr->publish(mqtt_pubs_topic_status, "Payload contained neither of the predefined messages.");
  }

}

/* in case connection is lost */
void reconnect() {
  // Loop until we're reconnected
  // Serial.println("In the reconnect() method.");
  while (!clientptr->connected()) {
  
  // the assumption is that if WiFi dropped, client must have lost connection too
  if(WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  } 
  
    // Attempt to connect to broker; chosen clientID is the device id
    // Serial.println("Attempting MQTT connection...");
    String clientID = "WemosD1Mini-";
    clientID+=String(random(0xffff), HEX);
    shortBlink(150);
    if (clientptr->connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("WiFi module connected to MQTT broker");
      // Subscribe to a topic
      if(clientptr->subscribe(mqtt_subs_topic)) {
        clientptr->publish(mqtt_subs_topic, DEVICE_OFF);
        clientptr->publish(mqtt_pubs_topic_status, "WiFi module subscribed to topic");
      } else {
        Serial.println("Failed to subscribe to the specified topic");
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(clientptr->state());
      Serial.println(" trying again in 2000 mseconds");
      delay(2000);
    }
  }
}

/*
 * Called once during the initialization phase after reset
 ****/
void setup() {
  // setup serial port with proper baud rate. seen as uart on pico.
  delay(500);
  Serial.begin(115200);
  delay(500);
  
  // initialize some pin for turning on onboard LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // high is off
  
  // setup prerequisites to safe MQTT
  LittleFS.begin();
  setup_wifi();
  setDateTime();
  
  // set up MQTT and the certificate for secure comm with broker
  initMQTTClient(0);
}

/*
 * This function will be called after setup() in an infinite loop
 ****/
void loop() {  
  if (!clientptr->connected()) {
    reconnect();
  }

  if (Serial.available() > 0) {
    /* form the received message */
    char incomingChar;
    int i = 0;
    while (Serial.available() > 0) {
      // read the incoming byte:
      incomingChar = Serial.read();
        received[i] = incomingChar;
        i++;
    }
    received[i]='\0';

    /* publish message from Pico to MQTT */
    clientptr->publish(mqtt_pubs_topic_status, received);

    /* length of msg from Pico */
    clientptr->publish(mqtt_pubs_topic_status, "Rec_len_pico: ");
    String str_i = String(i);
    clientptr->publish(mqtt_pubs_topic_status, str_i.c_str());
  }
  
  clientptr->loop();
  delay(300);
}