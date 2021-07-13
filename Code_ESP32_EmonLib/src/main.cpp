/* ESP32 MQTT with JSON
   Libraries used: Nick O'Leary PubSubClient@2.8 & ArduinoJson@6.17.3
   Author: Jithin Isaac  
   16 April 2021 */

#include <Arduino.h>
#include <Wifi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "EmonLib.h" // Include Emon Library

EnergyMonitor emon1;         // Create an instance
#define MEAS_METHOD_C0_CV1 0 //0 for current only, 1 for voltage-current

const char *ssid = "Slomo_2.4GHz";
const char *pass = "TW@pLLj6de!Yft%^9yuF";
const char *brokerUser = "user_test";
const char *brokerPass = "extc@dbit";
const char *broker_url = "mqttbroker.dblabs.in";
const uint16_t broker_port = 1883;
const char *outTopic = "esp32/lora-pv-rms/tx";
const char *inTopic = "esp32/lora-pv-rms/rx";

WiFiClient espClient;
PubSubClient client(espClient);

void setupWifi()
{
  delay(100);
  Serial.print("\nConnecting to WiFi AP: ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print("-");
  }

  Serial.print("\nConnected to WiFi AP: ");
  Serial.println(ssid);
}

void reconnect()
{
  while (!client.connected())
  {
    Serial.print("\nConnecting to MQTT Broker @");
    Serial.println(broker_url);
    delay(100);
    Serial.print("-");
    if (client.connect("unique_client_name123", brokerUser, brokerPass))
    {
      Serial.print("\nConnected to MQTT Broker @");
      Serial.println(broker_url);
      client.subscribe(inTopic);
    }
    else
    {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(client.state());
      Serial.println("\n Trying to reconnect in 5 seconds.");
      delay(5000);
    }
  }
  Serial.println();
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Subscribing Payload: ");
  Serial.print(topic);
  Serial.print(": '");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("'");
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  analogReadResolution(ADC_BITS);

  SPIFFS.begin(true); // Will format on the first run after failing to mount

  // Use stored credentials to connect to your WiFi access point.
  // If no credentials are stored or if the access point is out of reach,
  // an access point will be started with a captive portal to configure WiFi.
  WiFiSettings.connect();

  setupWifi();
  client.setServer(broker_url, broker_port);
  client.setCallback(callback);

  if (!MEAS_METHOD_C0_CV1)
  {
    emon1.current(34, 70); // Current: input pin, calibration.  emon1.current(1, 111.1);
    Serial.println("Current only method, with apparent power.");
  }

  if (MEAS_METHOD_C0_CV1)
  {
    emon1.voltage(35, 234.26, 1.7); // Voltage: input pin, calibration, phase_shift
    emon1.current(34, 111.1);       // Current: input pin, calibration.
    Serial.println("Current & Voltage method, with real & apparent power.");
  }
}

int interval = 10000;
float realPower, apparentPower, powerFactor, supplyVoltage, Irms;
double energyWh = 0, energykWh = 0;
unsigned long currentTimeE, lastTimeE = 0; //For Energy measurement
unsigned long currentTimeS, lastTimeS = 0; //For data Sending interval

void loop()
{
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  /* *********************** */
  /* Energy Payload generation */
  /* *********************** */

  //CURRENT ONLY METHOD WITH APPARENT POWER
  if (!MEAS_METHOD_C0_CV1)
  {
    Irms = emon1.calcIrms(1480); // Calculate Irms only
    apparentPower = Irms * 230.0;
    Serial.print("aP: ");
    Serial.print(apparentPower); // Apparent power
    Serial.print(" ");
    Serial.print("Irms: ");
    Serial.println(Irms); // Irms
  }

  //CURRENT & VOLTAGE METHOD WITH REAL & APPARENT POWER
  if (MEAS_METHOD_C0_CV1)
  {
    emon1.calcVI(20, 2000); // Calculate all. No.of half wavelengths (crossings), time-out
    emon1.serialprint();    // Print out all variables (realpower, apparent power, Vrms, Irms, power factor)

    realPower = emon1.realPower;         //extract Real Power into variable
    apparentPower = emon1.apparentPower; //extract Apparent Power into variable
    powerFactor = emon1.powerFactor;     //extract Power Factor into Variable
    supplyVoltage = emon1.Vrms;          //extract Vrms into Variable
    Irms = emon1.Irms;                   //extract Irms into Variable
  }

  currentTimeE = millis();
  if (!MEAS_METHOD_C0_CV1)
    energyWh += (apparentPower * (currentTimeE - lastTimeE)) / (3600 * 1000);
  if (MEAS_METHOD_C0_CV1)
    energyWh += (realPower * (currentTimeE - lastTimeE)) / (3600 * 1000);

  energykWh = energyWh / 1000;
  lastTimeE = currentTimeE;

  /* *********************** */
  /* JSON Payload generation */
  /* *********************** */

  StaticJsonDocument<200> JSONbuffer;

  // Add values in the JSONbuffer document
  JSONbuffer["NodeID"] = "SS_AC_Node1";
  JSONbuffer["Voltage"] = supplyVoltage;
  JSONbuffer["Current"] = Irms;
  JSONbuffer["Apparent Power"] = apparentPower;
  JSONbuffer["Real Power"] = realPower;
  JSONbuffer["Power factor"] = powerFactor;
  if (!MEAS_METHOD_C0_CV1)
    JSONbuffer["Energy Wh"] = energyWh;
  if (MEAS_METHOD_C0_CV1)
    JSONbuffer["Energy Wh"] = energyWh;
  JSONbuffer["Energy kWh"] = energykWh;

  // Generate the minified Serialized JSON and send it to the Serial port for debugging
  Serial.println();
  //serializeJsonPretty(JSONbuffer, Serial);

  char jsonBufferMQTT[256]; //jsonBufferMQTT is the MQTT Payload
  serializeJson(JSONbuffer, jsonBufferMQTT);

  /* ********************** */
  /* JSON Tx via MQTT */
  /* ********************** */

  currentTimeS = millis();
  if (currentTimeS - lastTimeS > 5000)
  {
    Serial.print("Publishing JSON Message: ");
    Serial.println(jsonBufferMQTT); //Prints the JSON Payload that we are sending
    client.publish(outTopic, jsonBufferMQTT);
    lastTimeS = currentTimeS;
    //Serial.println(lastTimeS);
  }
}