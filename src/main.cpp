#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
MAX30105 particleSensor;
#define MAX_BRIGHTNESS 255
// WiFi config
const char *SSID = "your_wifi_ssid";
const char *PWD = "wifi_pwd";
// Web server running on port 80
AsyncWebServer server(80);
// Async Events
AsyncEventSource events("/events");
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">  <title>ESP32 Heart</title>
  <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script> 
  <script language="javascript">
      
    google.charts.load('current', {'packages':['gauge']});
    google.charts.setOnLoadCallback(drawChart);
  
    var chartHR;
    var chartSPOO2;
    var optionsHR;
    var optionsSPO2;
    var dataHR;
    var dataSPO2;
    function drawChart() {
        dataHR = google.visualization.arrayToDataTable([
          ['Label', 'Value'],
          ['HR', 0]
        ]);
    
        optionsHR = {
          min:40, max:230,
          width: 400, height: 120,
          greenColor: '#68A2DE',
          greenFrom: 40, greenTo: 90,
          yellowFrom: 91, yellowTo: 150,
          redFrom:151, redTo:230,
          minorTicks: 5
        };
    
        chartHR = new google.visualization.Gauge(document.getElementById('chart_div_hr'));
    
        
        dataSPO2 = google.visualization.arrayToDataTable([
          ['Label', 'Value'],
          ['SPO2', 0]
        ]);
    
        optionsSPO2 = {
          min:0, max:100,
          width: 400, height: 120,
          greenColor: '#68A2DE',
          greenFrom: 0, greenTo: 100,
          minorTicks: 5
        };
    
        chartSPO2 = new google.visualization.Gauge(document.getElementById('chart_div_spo2'));
    
    
        chartHR.draw(dataHR, optionsHR);
        chartSPO2.draw(dataSPO2, optionsSPO2);
        
   }
   
   if (!!window.EventSource) {
     var source = new EventSource('/events');
     source.addEventListener('open', function(e) {
        console.log("Events Connected");
     }, false);
     source.addEventListener('error', function(e) {
        if (e.target.readyState != EventSource.OPEN) {
          console.log("Events Disconnected");
        }
     }, false);
    source.addEventListener('message', function(e) {
        console.log("message", e.data);
    }, false);
    source.addEventListener('hr', function(e) {
        dataHR.setValue(0,1, e.data);
        chartHR.draw(dataHR, optionsHR);
    }, false);
 
   source.addEventListener('spo2', function(e) {
        dataSPO2.setValue(0,1, e.data);
        chartSPO2.draw(dataSPO2, optionsSPO2);
    }, false);
}
  </script>
  
  <style>
    .card {
      box-shadow: 0 4px 8px 0 rgba(0,0,0,0.2);
      transition: 0.3s;
      border-radius: 5px; /* 5px rounded corners */
   }
   /* On mouse-over, add a deeper shadow */
  .card:hover {
    box-shadow: 0 8px 16px 0 rgba(0,0,0,0.2);
  }
  /* Add some padding inside the card container */
  .container {
    padding: 2px 16px;
  }
  </style>
</head>
<body>
  <h2>ESP32 Heart</h2>
  <div class="content">
    <div class="card">
     <div class="container">
       <div id="chart_div_hr" style="width: 400px; height: 120px;"></div>
       <div id="chart_div_spo2" style="width: 400px; height: 120px;"></div>
  </div>
</div> 
  </div>
</body>
</html>
)rawliteral";
uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data
int32_t bufferLength; //data length
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid
byte pulseLED = 11; //Must be on PWM pin
byte readLED = 13; //Blinks with each data read

 // last time SSE
long last_sse = 0;
void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.println(SSID);
  
  WiFi.begin(SSID, PWD);
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    // we can even make the ESP32 to sleep
  }
 
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
}
void configureEvents() {
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client connections. Id: %u\n", client->lastId());
    }
    // and set reconnect delay to 1 second
    client->send("hello from ESP32",NULL,millis(),1000);
  });
  server.addHandler(&events);
}
void setup()
{
  Serial.begin(115200); // initialize serial communication at 115200 bits per second:
  pinMode(pulseLED, OUTPUT);
  pinMode(readLED, OUTPUT);
  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println(F("MAX30105 was not found. Please check wiring/power."));
    while (1);
  }
 
  byte ledBrightness = 60; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
 
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
  connectToWiFi();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, NULL);
  });
  configureEvents();
  server.begin();
}
void loop()
{
  bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps
  //read the first 100 samples, and determine the signal range
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check(); //Check the sensor for new data
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample
   //Serial.print(F("red="));
   // Serial.print(redBuffer[i], DEC);
   // Serial.print(F(", ir="));
   // Serial.println(irBuffer[i], DEC);
  }
  //calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
  //Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 1 second
  while (1)
  {
    //dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
    for (byte i = 25; i < 100; i++)
    {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }
    //take 25 sets of samples before calculating the heart rate.
    for (byte i = 75; i < 100; i++)
    {
      while (particleSensor.available() == false) //do we have new data?
        particleSensor.check(); //Check the sensor for new data
      digitalWrite(readLED, !digitalRead(readLED)); //Blink onboard LED with every data read
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
     
      particleSensor.nextSample(); //We're finished with this sample so move to next sample
    }
    if (millis() - last_sse > 2000) {
      if (validSPO2 == 1) {
        events.send(String(spo2).c_str(), "spo2", millis());
        Serial.println("Send event SPO2");
        Serial.print(F("SPO2="));
        Serial.println(spo2, DEC);
      }
      if (validHeartRate == 1) {
         events.send(String(heartRate).c_str(), "hr", millis());
         Serial.println("Send event HR");
         Serial.print(F("HR="));
         Serial.println(heartRate, DEC);
      }
      last_sse = millis();
    }
    //After gathering 25 new samples recalculate HR and SP02
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
    
  }
}