#include <WiFiEsp.h>
#include <WiFiEspClient.h>
#include <WiFiEspServer.h>
#include <DHTesp.h>
#include <DFRobot_EC10.h>
#include <DFRobot_PH.h>

// including the AI models
#include "EC.h"
#include "pH.h"
#include "Humidity.h"
#include "Temperature.h"
Eloquent::ML::Port::RandomForestEC ForestEC;
Eloquent::ML::Port::RandomForestpH ForestpH;
Eloquent::ML::Port::RandomForestHumidity ForestHumidity;
Eloquent::ML::Port::RandomForestTemperature ForestTemperature;

#define AP_SSID "SmartHydro"
#define AP_PASS "Hydro123!"
#define EC_DOWN_PUMP_PIN 8
#define EC_UP_PUMP_PIN 7
#define PH_DOWN_PUMP_PIN 6
#define PH_UP_PUMP_PIN 5
#define CIRCULATION_PUMP_PIN 9
#define EXTRACTOR_FAN_PIN 10
#define TENT_FAN_PIN 11
#define LIGHT_PIN 12
#define TEMP_HUMID_SENS_PIN 50
#define FLOW_SENS_PIN 52
#define AMB_LIGHT_SENS_PIN A2
#define PH_SENS_PIN A3
#define EC_SENS_PIN A4

WiFiEspServer WebServer(80);
DHTesp TempHumid;
DFRobot_EC10 EC10;
DFRobot_PH PH;
volatile int FlowSensorPulseCount = 0;
unsigned long CurrentTime, PreviousTime;
bool AI_Flag;
String TemperatureAI = "Temperature: NaN - NaN", HumidityAI = "Humidity: NaN - NaN", PH_AI = "pH: NaN - NaN", EC_AI = "EC: NaN - NaN";

void setup() {
  // USB Serial Connection
  Serial.begin(115200);

  // DHT22 Temp/Humidity Sensor
  TempHumid.setup(TEMP_HUMID_SENS_PIN, 'AUTO_DETECT');

  // YF-B6 Flow Sensor
  pinMode(FLOW_SENS_PIN, INPUT);
  attachInterrupt(20, PulseCounterFlowSensor, FALLING);
  CurrentTime = millis();
  PreviousTime = 0;

  // Ambient Light Sensor
  pinMode(AMB_LIGHT_SENS_PIN, INPUT);

  // SEN0161 pH Sensor
  pinMode(PH_SENS_PIN, INPUT);
  PH.begin();

  // EC10 Current Sensor
  pinMode(EC_SENS_PIN, INPUT);

  // ESP module
  Serial1.begin(115200);
  WiFi.init(&Serial1);
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.print("no wifi shield, panicking!!!");
    while (true)
      ;
  }
  IPAddress SysIP(192, 168, 1, 10);
  WiFi.configAP(SysIP);
  WiFi.beginAP(AP_SSID, 13, AP_PASS, ENC_TYPE_WPA2_PSK, false);
  WebServer.begin();

  // setting all relay pins to output, and defaulting all off
  for (int i = 5; i <= 12; i++) {
    pinMode(i, OUTPUT);
    TogglePin(i);
  }
  // turning on equipment that should be on by default
  TogglePin(LIGHT_PIN);
  TogglePin(TENT_FAN_PIN);
  TogglePin(CIRCULATION_PUMP_PIN);
}

void loop() {
  if (AI_Flag) {
    TemperatureAI = PredictTemperature();
    HumidityAI = PredictHumidity();
    // EC_AI = PredictEC();
    // PH_AI = PredictPH();
  }

  WiFiEspClient WebClient = WebServer.available();
  if (!WebClient) { return; }
  String ClientRequest = WebClient.readStringUntil('\r');
  Serial.println("Client Request: " + ClientRequest);
  String Payload = "";

  while (WebClient.available()) {
    WebClient.read();
  }
  if (ClientRequest.indexOf("GET /hardware") >= 0) {
    Payload = GetHardwareStatus();
  }
  if (ClientRequest.indexOf("GET /sensor") >= 0) {
    Payload = GetSensorReadings();
  }
  if (ClientRequest.indexOf("GET /predictions") >= 0) {
    Payload = GetModelPredictions();
  }
  if (ClientRequest.indexOf("GET /toggleAI") >= 0) {
    AI_Flag = !AI_Flag;
    TemperatureAI = "Temperature: NaN - NaN";
    HumidityAI = "Humidity: NaN - NaN";
    EC_AI = "EC: NaN - NaN";
    PH_AI = "pH: NaN - NaN";
  }
  if (ClientRequest.indexOf("GET /ph_in") >= 0) {
    TogglePin(PH_UP_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /ph_out") >= 0) {
    TogglePin(PH_DOWN_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /circ_pump") >= 0) {
    TogglePin(CIRCULATION_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /ec_in") >= 0) {
    TogglePin(EC_UP_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /ec_out") >= 0) {
    TogglePin(EC_DOWN_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /fan_circ") >= 0) {
    TogglePin(TENT_FAN_PIN);
  }
  if (ClientRequest.indexOf("GET /fan_extractor") >= 0) {
    TogglePin(EXTRACTOR_FAN_PIN);
  }
  if (ClientRequest.indexOf("GET /light") >= 0) {
    TogglePin(LIGHT_PIN);
  }
  SendClientResponse(WebClient, Payload);
  WebClient.stop();
  Serial.println("Disconnecting from WiFi Client.");

  delay (3600);
}

void SendClientResponse(WiFiEspClient WebClient, String Payload) {
  // return a HTTP reponse to the client
  WebClient.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: close\r\n");

  // check if there is a payload to send, if yes, calculate the content length
  if (Payload.length() > 0) {
    WebClient.print("Content-Length: " + String(Payload.length()) + "\r\n\r\n" + Payload);
  }
}

String GetHardwareStatus() {
  // return all current hardware data in json format
  return "{\n \"pH_Up_Pump\" : \"" + String(digitalRead(PH_UP_PUMP_PIN))
         + "\",\n \"pH_Down_Pump\" : \"" + String(digitalRead(PH_DOWN_PUMP_PIN))
         + "\",\n \"EC_Up_Pump\" : \"" + String(digitalRead(EC_UP_PUMP_PIN))
         + "\",\n \"EC_Down_Pump\" : \"" + String(digitalRead(EC_DOWN_PUMP_PIN))
         + "\",\n \"Circulation_Pump\" : \"" + String(digitalRead(CIRCULATION_PUMP_PIN))
         + "\",\n \"Extractor_Fan\" : \"" + String(digitalRead(EXTRACTOR_FAN_PIN))
         + "\",\n \"Tent_Fan\" : \"" + String(digitalRead(TENT_FAN_PIN))
         + "\",\n \"Grow_Light\" : \"" + String(digitalRead(LIGHT_PIN)) + "\"\n}";
}

String GetSensorReadings() {
  float Temperature = TempHumid.getTemperature();
  // return data from all sensors in json format
  return "{\n \"Temperature\" : \"" + String(Temperature)
         + "\",\n \"Humidity\" : \"" + String(TempHumid.getHumidity())
         + "\",\n \"LightLevel\" : \"" + String(GetLightReading())
         + "\",\n \"FlowRate\" : \"" + String(GetFlowRateReading())
         + "\",\n \"pH\" : \"" + String(GetPHReading(Temperature))
         + "\",\n \"EC\" : \"" + String(GetECReading(Temperature)) + "\"\n}";
}

String GetModelPredictions() {
  return "{\n \"AI_Status\" : \"" + String(AI_Flag)
         + "\",\n \"Temperature\" : \"" + String(TemperatureAI)
         + "\",\n \"Humidity\" : \"" + String(HumidityAI)
         + "\",\n \"pH\" : \"" + String(PH_AI)
         + "\",\n \"EC\" : \"" + String(EC_AI) + "\"\n}";
}

int GetLightReading() {
  // return ambient light sensor reading
  return (int)analogRead(AMB_LIGHT_SENS_PIN);
}

float GetECReading(float Temperature) {
  // calculate current voltage and return EC reading
  float EC_Voltage = (float)analogRead(EC_SENS_PIN) / 1024.0 * 5000.0;
  return EC10.readEC(EC_Voltage, Temperature);
}

float GetPHReading(float Temperature) {
  // calculate current voltage and return PH reading
  float PH_Voltage = (float)analogRead(PH_SENS_PIN) / 1024.0 * 5000.0;
  return PH.readPH(PH_Voltage, Temperature);

}

float GetFlowRateReading() {
  // return number of L/s flowing through the sensor
  float FlowCalibrationFactor = 6.6;
  if ((millis()) - PreviousTime > 1000) {
    detachInterrupt(20);
    float WaterFlowRate = ((1000.0 / (millis() - PreviousTime)) * FlowSensorPulseCount) / FlowCalibrationFactor;
    PreviousTime = millis();
    FlowSensorPulseCount = 0;
    return WaterFlowRate;
  }
}

String PredictTemperature() {
  // gets the current prediction and adjusts the light accordingly
  // HIGH is OFF, LOW is ON
  float Temperature = TempHumid.getTemperature();
  int Prediction = ForestTemperature.predict(&Temperature);
  int LightStatus = digitalRead(LIGHT_PIN);
  switch (Prediction) {
    case 0:  // HIGH
      if (LightStatus == 0) {
        digitalWrite(LIGHT_PIN, HIGH);
        return "Temperature: HIGH - Light Off";
      } else {
        return "Temperature: HIGH - Light Off";
      }

    case 1:  // LOW
      if (LightStatus == 1) {
        digitalWrite(LIGHT_PIN, LOW);
        return "Temperature: LOW - Light On";
      } else {
        return "Temperature: LOW - Light On";
      }

    case 2:  // OK
      if (LightStatus == 0) {
        digitalWrite(LIGHT_PIN, HIGH);
        return "Temperature: OK - Light Off";
      } else {
        return "Temperature: OK - Light Off";
      }
  }
}

String PredictHumidity() {
  // gets the current prediction and adjusts the extractor accordingly
  // HIGH is OFF, LOW is ON
  float Humidity = TempHumid.getHumidity();
  int Prediction = ForestHumidity.predict(&Humidity);
  int FanStatus = digitalRead(EXTRACTOR_FAN_PIN);
  switch (Prediction) {
    case 0:  // HIGH
      if (FanStatus == 1) {
        digitalWrite(EXTRACTOR_FAN_PIN, LOW);
        return "Humidity: HIGH - Extractor On";
      } else {
        return "Humidity: HIGH - Extractor On";
      }

    case 1:  // LOW
      if (FanStatus == 0) {
        digitalWrite(EXTRACTOR_FAN_PIN, HIGH);
        return "Humidity: LOW - Extractor Off";
      } else {
        return "Humidity: LOW - Extractor Off";
      }


    case 2:  // OK
      if (FanStatus == 0) {
        digitalWrite(EXTRACTOR_FAN_PIN, HIGH);
        return "Humidity: OK - Extractor Off";
      } else {
        return "Humidity: OK - Extractor Off";
      }
  }
}

String PredictEC() {
  // gets the current prediction and adjusts the pumps accordingly
  // HIGH is OFF, LOW is ON
  float Temperature = TempHumid.getTemperature();
  float EC = 0, ec[5], sum =0;
  int size = 1;
  for ( int i = 0; i < 6; i++)
      {
        ec[i] = GetPHReading(Temperature);
        sum += ec[i];
      }
  size = sizeof(ec)/sizeof(ec[0]);
  EC = (sum / size);
  int Prediction = ForestEC.predict(&EC);
  int EC_Up_Status = digitalRead(EC_UP_PUMP_PIN);
  int EC_Down_Status = digitalRead(EC_DOWN_PUMP_PIN);
  switch (Prediction) {
    case 0:  // HIGH
      if (EC_Down_Status == 1) {
        digitalWrite(EC_UP_PUMP_PIN, HIGH);
        digitalWrite(EC_DOWN_PUMP_PIN, LOW);
        return "EC: HIGH - Adding Water";
      } else {
        return "EC: HIGH - Adding Water";
      }

    case 1:  // LOW
      if (EC_Up_Status == 1) {
        digitalWrite(EC_DOWN_PUMP_PIN, HIGH);
        digitalWrite(EC_UP_PUMP_PIN, LOW);
        return "EC: LOW - Adding Nutrient Solution";
      } else {
        return "EC: LOW - Adding Nutrient Solution";
      }

    case 2:  // OK
      if (EC_Up_Status == 0 || EC_Down_Status == 0) {
        digitalWrite(EC_DOWN_PUMP_PIN, HIGH);
        digitalWrite(EC_UP_PUMP_PIN, HIGH);
        return "EC: OK - All Pumps Off";
      } else {
        return "EC: OK - All Pumps Off";
      }
  }
}

String PredictPH() {
  // gets the current prediction and adjusts the pumps accordingly
  // HIGH is OFF, LOW is ON
  float Temperature = TempHumid.getTemperature();
  float PH = 0, ph[5], sum =0;
  int size = 1;
  for ( int i = 0; i < 6; i++)
      {
        ph[i] = GetPHReading(Temperature);
        sum += ph[i];
      }
  size = sizeof(ph)/sizeof(ph[0]);
  PH = (sum / size);
   
  int Prediction = ForestpH.predict(&PH);
  int PH_Up_Status = digitalRead(PH_UP_PUMP_PIN);
  int PH_Down_Status = digitalRead(PH_DOWN_PUMP_PIN);
  switch (Prediction) {
    case 0:  // HIGH
      if (PH_Down_Status == 1) {
        digitalWrite(PH_UP_PUMP_PIN, HIGH);
        digitalWrite(PH_DOWN_PUMP_PIN, LOW);
        return "PH: HIGH - Adding Solution";
      } else {
        return "PH: HIGH - Adding Solution";
      }

    case 1:  // LOW
      if (PH_Up_Status == 1) {
        digitalWrite(PH_DOWN_PUMP_PIN, HIGH);
        digitalWrite(PH_UP_PUMP_PIN, LOW);
        return "PH: LOW - Adding Solution";
      } else {
        return "PH: LOW - Adding Solution";
      }

    case 2:  // OK
      if (PH_Up_Status == 0 || PH_Down_Status == 0) {
        digitalWrite(PH_DOWN_PUMP_PIN, HIGH);
        digitalWrite(PH_UP_PUMP_PIN, HIGH);
        return "PH: OK - All Pumps Off";
      } else {
        return "PH: OK - All Pumps Off";
      }
  }
}

void PulseCounterFlowSensor() {
  // used to keep track of pulses from the flow rate sensor
  FlowSensorPulseCount++;
}

void TogglePin(int PinToToggle) {
  // used to toggle a pins state to the opposite of what it currently is
  digitalWrite(PinToToggle, !digitalRead(PinToToggle));
}