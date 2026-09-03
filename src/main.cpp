#include <Arduino.h>
#include "HomeSpan.h"
#define private protected // Re-define private as protected to allow access to private members of FujiHeatPump for the extended class
#include "FujiHeatPump.h"
#undef private
#include <ostream>

// Instanciation of 3 wire Atlantic heat pump controller
class FujiHeatPumpExt : public FujiHeatPump {
  public:
    FujiHeatPumpExt() : FujiHeatPump() {}
    bool isBound() {
      // Fix a missing test in genuine function to check if a frame has been received before checking if the controller is bound
      return (lastFrameReceived) ? FujiHeatPump::isBound() : false;  // Call the base class method to check if the controller is bound
    }
};
FujiHeatPumpExt hp;

// Pin definitions for ESP32-C3
#define LIN_RX_PIN 20
#define LIN_TX_PIN 21
#define BUTTON_PIN 9    // PROG button
#define LED_PIN 10      // WiFi status LED
#define HOSTNAME        "Clim Atlantic"  // Hostname for the ESP32-C3 device

// HomeKit complete Thermostat Homekit structure with all characteristics (including fan speed and swing mode)
struct HK_CompleteThermostat : Service::Thermostat {

  SpanCharacteristic *currentMode;
  SpanCharacteristic *targetMode;
  SpanCharacteristic *currentTemp;
  SpanCharacteristic *targetTemp;
  SpanCharacteristic *fanSpeed;
  SpanCharacteristic *swingMode;
  SpanCharacteristic *TemperatureDisplayUnits;
  
  HK_CompleteThermostat() : Service::Thermostat() {
    currentMode = new Characteristic::CurrentHeatingCoolingState(0);
    targetMode = new Characteristic::TargetHeatingCoolingState(0);
    
    currentTemp = new Characteristic::CurrentTemperature(21);
    targetTemp = new Characteristic::TargetTemperature(21);
    targetTemp->setRange(16, 30, 1); 

    TemperatureDisplayUnits = new Characteristic::TemperatureDisplayUnits(0); // 0 = Celsius, 1 = Fahrenheit

    // Fan speed (with 25% increments)
    fanSpeed = new Characteristic::RotationSpeed(50); 
    fanSpeed->setRange(0, 100, 25); 

    // Swing mode (0 = Off, 1 = On)
    swingMode = new Characteristic::SwingMode(0);
  }

  boolean update() override {
    // Heating/Cooling mode (Off / Heat / Cool / Auto)
    if(targetMode->updated()) {
      int mode = targetMode->getNewVal();
      if(mode == 0) { 
        hp.setOnOff(false); 
      } else {
        hp.setOnOff(true);
        if(mode == 1) hp.setMode(static_cast<byte>(FujiMode::HEAT));
        if(mode == 2) hp.setMode(static_cast<byte>(FujiMode::COOL));
        if(mode == 3) hp.setMode(static_cast<byte>(FujiMode::AUTO));
      }
    }

    // 2. Target temperature
    if(targetTemp->updated()) {
      float temp = targetTemp->getNewVal();
      hp.setTemp((byte)temp);
    }

    // 3. Fan speed
    if(fanSpeed->updated()) {
      int pct = fanSpeed->getNewVal();
      if (pct == 0)       { hp.setFanMode(0); } // Auto
      else if (pct <= 25) { hp.setFanMode(4); } // Quiet
      else if (pct <= 50) { hp.setFanMode(1); } // Low
      else if (pct <= 75) { hp.setFanMode(2); } // Medium
      else                { hp.setFanMode(3); } // High
    }

    // 4. Oscillation
    if(swingMode->updated()) {
      int swing = swingMode->getNewVal();
      hp.setSwingMode(swing);
    }

    return true;
  }
};

HK_CompleteThermostat *myClim = NULL;

void setup() {
  // Init debug console via USB CDC
  Serial.begin(115200);   // Use USB for Serial0
  // Wait for Serial to be ready (up to 2 seconds)
  uint32_t startTime = millis();
  while (!Serial && (millis() - startTime < 2000)) {
    delay(10);
  }

  // Config secondary UART for LIN bus communication with the Atlantic heat pump
  // The Atlantic protocol uses a 500 baud rate with 8 data bits, even parity, and 1 stop bit (8E1)
  Serial1.begin(500, SERIAL_8E1, LIN_RX_PIN, LIN_TX_PIN);

  // Connect to bus as SECONDARY controller (the UTY-RNNUM is the PRIMARY controller)
  hp.connect(&Serial1, true); 
  //hp.debugPrint = true; // Set to true to enable debug output of frames sent/received on the LIN bus
  
  // Init HomeSpan accessory and services
  homeSpan.setLogLevel(2); // -1 = no log, 0 = errors only, 1 = normal, 2 = verbose
  WiFi.setHostname(HOSTNAME); 
  homeSpan.setApSSID("Atlantic-AP");
  homeSpan.setApPassword(""); // Must be at least 8 characters if required
//  homeSpan.enableAutoStartAP(); 
  homeSpan.setSerialInputDisable(false); // Disable serial input to avoid conflicts with the LIN bus
  homeSpan.setControlPin(BUTTON_PIN);   // Set the pin for the PROG button to trigger HomeSpan actions
  homeSpan.setStatusPin(LED_PIN);       // Set the pin for the WiFi status LED
  digitalWrite(LED_PIN,HIGH);
  homeSpan.setCompileTime(); 
  // 1. Activate OTA (default password : "homespan-ota")
  homeSpan.enableOTA(false); 
  // 2. Activate the integrated web server (Max 50 messages in cache, NTP server, Time zone, Reltive URL)
  // The page is http://<ESP32_IP>/status
  homeSpan.enableWebLog(50, "pool.ntp.org", "CET-1CEST,M3.5.0,M10.5.0/3", "status");
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
    myClim = new HK_CompleteThermostat();
}
static int isFirstLoop = 60; // Wait 1 min. for first frame from heat pump before initializing HomeKit
void loop() {

  // Listen to the Atlantic heat pump bus for any changes
  // (e.g., if the user changes settings directly on the thermostat)
  if(hp.waitForFrame()) {
    // Automatically update HomeKit characteristics based on the current state of the heat pump

    byte currentFujiFan = hp.getFanMode();
    int targetPct = 0;
    if (currentFujiFan == 4) targetPct = 25;
    else if (currentFujiFan == 1) targetPct = 50;
    else if (currentFujiFan == 2) targetPct = 75;
    else if (currentFujiFan == 3) targetPct = 100;
    
    if(myClim->fanSpeed->getVal() != targetPct) {
      myClim->fanSpeed->setVal(targetPct);
    }

    byte currentFujiSwing = hp.getSwingMode();
    if(myClim->swingMode->getVal() != currentFujiSwing) {
      myClim->swingMode->setVal(currentFujiSwing);
    }
    
    myClim->currentTemp->setVal(hp.getTemp());  
    delay(55);              // frames should be sent 50-60ms after receiving - potentially other work can be done here
    hp.sendPendingFrame();  // send any frame waiting in the buffer
  }

  if (--isFirstLoop > 0) {
    if (hp.isBound() || (isFirstLoop < 2)) { // If we have received a frame from the heat pump, or if we have waited long enough, proceed with HomeKit initialization
      isFirstLoop = 0;
      homeSpan.begin(Category::Thermostats, HOSTNAME, "Atlantic");      
    } else {
      Serial.println("Waiting for first frame from heat pump...");
      delay(500); // Wait 0.5 seconds before checking again
      digitalWrite(LED_PIN,(digitalRead(LED_PIN) == LOW) ? HIGH : LOW); // Blink the status LED to indicate waiting for the first frame
      return; // Wait until we have received a frame from the heat pump before proceeding with HomeKit initialization
    }
  }

  // Manage HomeKit protocol
  homeSpan.poll();

}
