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
  hp.debugPrint = true; // Set to true to enable debug output of frames sent/received on the LIN bus
  hp.connect(&Serial1, true); 
  Serial1.setTimeout(30); // Set a timeout for reading from the LIN bus
  
  // Init HomeSpan accessory and services
  homeSpan.setLogLevel(0); // -1 = no log, 0 = errors only, 1 = normal, 2 = verbose
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
  homeSpan.enableOTA(false);  // Enable OTA updates (false = no password, true = password required)
  // 2. Activate the integrated web server (Max 50 messages in cache, NTP server, Time zone, Reltive URL)
  // The page is http://<ESP32_IP>/status
  homeSpan.enableWebLog(50, "pool.ntp.org", "CET-1CEST,M3.5.0,M10.5.0/3", "status");
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
    myClim = new HK_CompleteThermostat();
//  homeSpan.begin(Category::Thermostats, HOSTNAME, "Atlantic");      
}
bool isHomeSpanInitialized = false;
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
      Serial.printf("Updating HomeKit fan speed to %d%%\n", targetPct);
      myClim->fanSpeed->setVal(targetPct);
    }
    byte currentFujiSwing = hp.getSwingMode();
    if(myClim->swingMode->getVal() != currentFujiSwing) {
      Serial.printf("Updating HomeKit swing mode to %d\n", currentFujiSwing);
      myClim->swingMode->setVal(currentFujiSwing);
    }

    byte mode = hp.getOnOff() ? hp.getMode() : 0; // If the heat pump is off, set mode to 0 (off)
    byte currentMode = 0;
    if (mode == 0) {
      currentMode = 0; // Off
    } else if (mode == static_cast<byte>(FujiMode::HEAT)) {
      currentMode = 1; // Heat
    } else if (mode == static_cast<byte>(FujiMode::COOL)) {
      currentMode = 2; // Cool
    } else if (mode == static_cast<byte>(FujiMode::AUTO)) {
      currentMode = 3; // Auto
    }
    if (myClim->currentMode->getVal() != currentMode) {
      Serial.printf("Updating HomeKit current mode to %d\n", currentMode);
      myClim->currentMode->setVal(currentMode);
      myClim->targetMode->setVal(currentMode, false); // Also update the target mode to match the current mode
    }
    if (myClim->currentTemp->getVal() != hp.getControllerTemp()) {
      Serial.printf("Updating HomeKit current temperature to %d\n", hp.getControllerTemp());
      myClim->currentTemp->setVal(hp.getControllerTemp());
    }
    if (myClim->targetTemp->getVal() != hp.getTemp()) {
      Serial.printf("Updating HomeKit target temperature to %d\n", hp.getTemp());
      myClim->targetTemp->setVal(hp.getTemp(), false);
    }
  }
  hp.sendPendingFrame();  // send any frame waiting in the buffer
  if(!isHomeSpanInitialized) {
      homeSpan.begin(Category::Thermostats, HOSTNAME, "Atlantic");
      isHomeSpanInitialized = true;
  } else {
    // Manage HomeKit protocol
    homeSpan.poll();
  }
}
