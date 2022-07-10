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
Sends/receives remote MQTT messages and transmits them on Serial to pico. 
**/

/* libraries for WiFi, MQTT methods */
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

/* libraries for time sync */
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>

// A single, global CertStore which can be used by all connections.
// Needs to stay live the entire time any of the WiFiClientBearSSLs
// are present.
BearSSL::CertStore certStore;

// the client objects 
WiFiClientSecure espClientSecure;
PubSubClient* clientptr;

// NTP client
const long utcOffsetInSeconds = 7800; // UTC+02 timezone offset
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

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
    // Serial.println("Attempting MQTT connection...");
    String clientID = "WemosD1Mini-";
    clientID+=String(random(0xffff), HEX);
    shortBlink(150);
    if (clientptr->connect(clientID.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("WiFi module connected to MQTT broker");
      if(clientptr->subscribe(topic_device0_value)) { // subscribing to one topic seems to suffice
        clientptr->publish(topic_device0_value, "D0=0;"); // initial off-value to device
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
  while (!clientptr->connected()) {
    if(WiFi.status() != WL_CONNECTED) {
      setup_wifi();
    } 
    setup_mqtt(); // if wifi dropped, mqtt lost connection too
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

void printToMCU(char* topic, byte* payload, unsigned int length) {
  // Serial.println();
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void readFromMCU() {
    char incomingChar;
    int i = 0;
    while (Serial.available() > 0) {
      incomingChar = Serial.read(); // incoming byte
      received[i] = incomingChar;
      i++;
      if (incomingChar == '\n') {break;}
    }
    received[i]='\0';
    // return i;
}

 /*
  * MQTT callback function. Receives the remote payload and the topic it was published to.
  * Forwards this payload to MCU and its devices.
  * Device topics are changed by being posted to from a mobile app or MQTT terminal. So
  * when the device integer and value are extracted here, they are just saved into
  * arrays on this WiFi module. But the entire payload is forwarded to the MCU, which
  * has to extract both of these for itself anyways.
  ****/
void callback(char* topic, byte* payload, unsigned int length) {
  int i;
  int device_index = 0;
  char string[50];

  // send payload from MQTT to Pico. the topic may specify the device.
  printToMCU(topic, payload, length);

  // Copy the payload into a C-String and convert all letters into lower-case
  for (i = 0; i < length; i++) {
    string[i] = (char)payload[i]; // if lowercase, tolower(string[i]);
  }
  string[i]=0;

  /* Check if payload fits protocol for a given device; for now only device0, LED
   * Protocol: payloads sent to device topics, starting with 'D', contain commands. */
  if (/*(strstr(topic, "devices") != NULL) &&*/ (payload[0] == 'D')){

    // extract the integer associated with the device
    strcpy(payload_copy_1, string);
    scanned_substring = strtok(payload_copy_1, "D");
    scanned_substring = strtok(NULL, "=");
    device_index = String(scanned_substring).toInt();

    // extract num value which will modulate device power
    strcpy(payload_copy_0, string);
    scanned_substring = strtok(payload_copy_0, "=");
    scanned_substring = strtok(NULL, ";");
    int val = String(scanned_substring).toInt();

    // remember old state of device
    device_array_old[device_index] = device_array[device_index];
    device_array[device_index] = val;

    // sanity check: Wemos LED is off if a device toggled to 0, and on for bigger than 0
    if(val > 0) {digitalWrite(ledPin, LOW); } else {digitalWrite(ledPin, HIGH);}

    // state change could be tracked for each index (i.e. for each device)
    if (device_array_old[device_index] != device_array[device_index]) {
      sprintf(device_msg_to_mqtt, "New state of topic [%s] is [%d]; device index: [%d]; device value [%d]", topic, val, device_index, val);
      clientptr->publish(topic_general, device_msg_to_mqtt);
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

  // start tracking time
  timeClient.begin();
  
  // set up MQTT and the certificate for secure comm with broker
  initMQTTClient(0);

  // set up the working sensor bits upon initialization
  for (int i = 0; i < sensors_online_qty; i++) {
    sensors_online = sensors_online | (1<<i);
  }
}

/*
 * Infinite loop: check connection, check if anything available on UART0 Rx,
 * from the MCU, and check if the MCU message fits any template.
 ****/
void loop() {  
  if (!clientptr->connected()) {
    Serial.print("Connection dropped - reconnecting...");
    connection_status = 0;
    reconnect();
  }


  /* Read from pico and publish its msgs, only if wifi+mqtt are connected. generally for sensors. */
  if (Serial.available() > 0 /*&& connection_status == 2*/) {
    
    /* build string from Pico in "received" */
    readFromMCU();

    /* determine which topic to post Pico data to, based on format, Sn=x; for sensors, Dn=x; for devices 
     * Protocol: strings sent from MCU to Serial, starting with 'S', contain sensor data.*/
    if (received[0] == 'S'){
      strcpy(received_copy_1, received);
      strcpy(received_copy_0, received);
      
      // extract sensor index: assume it's a single digit right after 'S'
      int sensor_index_element = String(received_copy_1[1]).toInt();
//      scanned_substring = strtok(received_copy_1, "S");
//      scanned_substring = strtok(NULL, "=");
//      int sensor_index = String(scanned_substring).toInt();
  
      // extract sensor reading
      scanned_substring = strtok(received_copy_0, "=");
      scanned_substring = strtok(NULL, ";");
      int sensor_value = String(scanned_substring).toInt(); // or toDouble()
      float sensor_value_float = String(scanned_substring).toFloat();
  
      // remember old reading of sensor and publish new
      sensor_array_old[sensor_index_element] = sensor_array[sensor_index_element];
      sensor_array[sensor_index_element] = sensor_value_float;
      clientptr->publish(sensor_topics[sensor_index_element],received);

      // record which sensors updated
      sensors_updated = (sensors_updated | (1<<sensor_index_element));

      // for debugging
      sprintf(debugging_msg, "Sensor index: [%d], sensor read value [%f], sensors_update: [%d], sensors_online; [%d]", 
        sensor_index_element, sensor_value_float, sensors_updated, sensors_online);
      clientptr->publish(topic_general, debugging_msg);
    }

   /* build the json template with timestamp, and post to topic when all sensors refreshed */
   if (sensors_updated == sensors_online) { 
      sensors_updated = 0; // reset updated sensors

      // get time
      timeClient.update();
      long epochtime = timeClient.getEpochTime();
      int date_day = day(epochtime);
      int date_month = month(epochtime);
      int date_year = year(epochtime);
      int hours = timeClient.getHours();

      // format time quantities to show leading zero if they are single-digit
      char formatted_date[16];
      char date_day_s[3];
      char date_month_s[3];
      char hours_s[3];
      if (date_day < 10) {sprintf(date_day_s, "0%d",date_day);} else {sprintf(date_day_s, "%d",date_day);}
      if (date_month < 10) {sprintf(date_month_s, "0%d",date_month);} else {sprintf(date_month_s, "%d",date_month);}
      if (hours < 10) {sprintf(hours_s, "0%d",hours);} else {sprintf(hours_s, "%d",hours);}
      sprintf(formatted_date, "%d-%s-%s", date_year, date_month_s, date_day_s);

      // minutes and seconds zeroed instead of timeClient.getFormattedTime()
      char formatted_time[32];
      sprintf(formatted_time, "%sT%s:00:00+%s", formatted_date, hours_s, "02:00"); // format 2022-07-09T12:00:00+02:00

      // format and publish the actual json datapoint
      sprintf(sensors_datapoint_json_msg, sensors_datapoint_json_template, (formatted_time), String(epochtime), 
        (sensor_array[1]), temperatureunit, (sensor_array[0]), (sensor_array[2]), mobilelink, link);
      clientptr->publish(topic_sensors_datapoint, sensors_datapoint_json_msg);
   }
   
    /* publish message from MCU to the general Pico status topic, indiscriminately */
    clientptr->publish(topic_pico_status, received);
  }
  
  clientptr->loop();
  delay(200);
}