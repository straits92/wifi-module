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
Receives remote MQTT messages and transmits them on Serial to pico. 
**/

/* libraries for necessary methods */
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <string.h>

/* libraries for private data, strings */
#include "wifi.h"
#include "broker.h"
#include "format.h"

/* libraries dictated by HiveMQ */
#include <time.h>
#include <TZ.h>
#include <FS.h>
#include <LittleFS.h>
#include <CertStoreBearSSL.h>

// the selected LED hardware to control
int ledPin = LED_BUILTIN;

// state variables. offline devices and sensors are -1
int connection_status = 0; // 0: no wifi, 1: wifi, 2: wifi+mqtt
int sensor_array[3] = {-1, -1, -1}; // humidity, temperature, light intensity
int device_array[1] = {-1}; // LED
int device_array_old[1] = {-1}; // LED

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

/*** mqtt and connectivity functions ***/
void initMQTTClient(int verbose) {
  /* setting up the certificate */
  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  if (verbose != 0) {Serial.printf("Number of CA certs read: %d\n", numCerts);}
  if (numCerts == 0) {
    if (verbose != 0) {Serial.printf("No certs found. Run certs-from-mozilla.py, upload LittleFS directory, then run.\n");}
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

void setup_mqtt() {
     // Attempt to connect to broker; chosen clientID is the device id
    // Serial.println("Attempting MQTT connection...");
    String clientID = "WemosD1Mini-";
    clientID+=String(random(0xffff), HEX);
    shortBlink(150);
    if (clientptr->connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("WiFi module connected to MQTT broker");
      // Subscribe to the control topic of device0
      if(clientptr->subscribe(topic_device0_value)) {
        clientptr->publish(topic_device0_value, "D=0;");
        clientptr->publish(topic_general, "WiFi module subscribed to topic:");
        clientptr->publish(topic_general, topic_device0_value);

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

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("WiFi name: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());
   
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP()); // or just the ssid
}

/* Loop until reconnected to wifi, then to mqtt */
void reconnect() {
  // Loop until we're reconnected
  // Serial.println("In the reconnect() method.");
  while (!clientptr->connected()) {
  
  // the assumption is that if WiFi dropped, client must have lost connection too
  if(WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  } 
  setup_mqtt();

  }
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


/*** helpers ***/
void shortBlink(int duration){
  digitalWrite(ledPin, LOW); 
  delay(duration);
  digitalWrite(ledPin, HIGH); 
  delay(duration*6);
}

void printMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived for topic: [");
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

void readFromMCU() {
    char incomingChar;
    int i = 0;
    while (Serial.available() > 0) {
      // read the incoming byte:
      incomingChar = Serial.read();
      received[i] = incomingChar;
      i++;
    }
    received[i]='\0';
    // return i;
}


 /*
  * MQTT callback function. Receives the remote payload and the topic it was published to.
  * Forwards this payload to MCU and its devices.
  ****/
void callback(char* topic, byte* payload, unsigned int length) {
  int i;
  int device_index = 0;
  char string[50];
  char lowerString[50];

  // send to Pico. the topic may specify the device.
  // check if this is done constantly again, maybe best
  // done in conditionals below
  printToMCU(topic, payload, length);

  // Copy the payload into a C-String and convert all letters into lower-case
  for (i = 0; i < length; i++) {
    string[i] = (char)payload[i];
    lowerString[i] = tolower(string[i]);
  }
  string[i]=0;
  lowerString[i]=0;

  /* Check if payload fits protocol for a given device; for now only device0, LED */
  if (strcmp(topic, topic_device0_value) == 0){
    device_index = 0;

    // check delimiters and extract num value
    strcpy(payload_copy, string);
    substring = strtok(payload_copy, "=");
    substring = strtok(NULL, ";");
    int val = String(substring).toInt();

    // remember old state of device
    device_array_old[device_index] = device_array[device_index];
    device_array[device_index] = val;

    // a sanity check on the wifi board, for device0 only
    if(val > 0) {
     digitalWrite(ledPin, LOW); 
    }
    else {
     digitalWrite(ledPin, HIGH); 
    }

    // state change could be tracked for each index (i.e. for each device)
    if (device_array_old[device_index] != device_array[device_index]) {
      sprintf(msg, "%s%s is [%d]", "New state of topic ", topic, val);
      clientptr->publish(topic_general, msg);
      clientptr->publish(topic_device0_status, String(val).c_str());
    }    
  }
}

/*
 * Called once during the initialization phase after reset
 ****/
void setup() {
  // setup serial port with same baud rate as UART on pico
  delay(500);
  Serial.begin(115200);
  delay(500);
  
  // initialize onboard LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // high is off
  
  // start certification module, connect to the internet
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
    Serial.print("Connection dropped - reconnecting...");
    connection_status = 0;
    reconnect();
  }

  /* send an online-verified time to the pico */
  // ...

  /* Read from pico and publish its msgs, only if wifi+mqtt */
  if (Serial.available() > 0 /*&& connection_status == 2*/) {
    
    /* build string from Pico in "received" */
//    int message_length = readFromMCU();
    readFromMCU();

    /* determine which topic to post Pico data to, based on format */
    // sensors topics if received contains Sn=num; device topics if Dn=num;
    // update the sensors or devices array accordingly


    /* publish message from MCU to status topic, indiscriminately (to be differentiated by conditional case) */
    clientptr->publish(topic_pico_status, received);

    /* message length checked for debugging */
//    clientptr->publish(topic_pico_status, "Pico msg length: ");
//    String str_i = String(message_length);
//    clientptr->publish(topic_pico_status, str_i.c_str());
  }
  
  clientptr->loop();

  // is this delay at all needed? remove?
  delay(200);
}