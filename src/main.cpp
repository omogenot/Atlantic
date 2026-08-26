#include <Arduino.h>
#include "HomeSpan.h"
#include "FujiHeatPump.h"
#include <ostream>

// Instanciation de la bibliothèque de gestion du bus 3 fils Atlantic
FujiHeatPump hp;

// Définition des broches UART pour l'ESP32-C3
#define LIN_RX_PIN 20
#define LIN_TX_PIN 21
#define BUTTON_PIN 9    // PROG button
#define LED_PIN 10      // WiFi status LED

// Structure du service HomeKit Thermostat complet
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

    // Vitesse du ventilateur (paliers de 25%)
    fanSpeed = new Characteristic::RotationSpeed(50); 
    fanSpeed->setRange(0, 100, 25); 

    // Oscillation des volets
    swingMode = new Characteristic::SwingMode(0);
  }

  boolean update() override {
    // 1. Gestion des modes (Off / Chaud / Froid / Auto)
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

    // 2. Température cible
    if(targetTemp->updated()) {
      float temp = targetTemp->getNewVal();
      hp.setTemp((byte)temp);
    }

    // 3. Vitesse de ventilation
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
  Serial.begin(115200, SERIAL_8N1, 18,19);  // Use alternate pins for Serial0
  
  // Config secondary UART for LIN bus communication with the Atlantic heat pump
  // The Atlantic protocol uses a 500 baud rate with 8 data bits, even parity, and 1 stop bit (8E1)
  Serial1.begin(500, SERIAL_8E1, LIN_RX_PIN, LIN_TX_PIN);

  // Connect to bus as SECONDARY controller (the UTY-RNNUM is the PRIMARY controller)
  hp.connect(&Serial1, true); 

  // Init HomeSpan accessory and services
  homeSpan.setLogLevel(-1); // -1 = no log, 0 = errors only, 1 = normal, 2 = verbose
  
  homeSpan.setApSSID("Atlantic-AP");
  homeSpan.setApPassword(""); // Must be at least 8 characters if required
//  homeSpan.enableAutoStartAP(); 
  homeSpan.setSerialInputDisable(true); // Disable serial input to avoid conflicts with the LIN bus
  homeSpan.setControlPin(BUTTON_PIN);   // Set the pin for the PROG button to trigger HomeSpan actions
  homeSpan.setStatusPin(LED_PIN);       // Set the pin for the WiFi status LED
  homeSpan.begin(Category::Thermostats, "Clim Atlantic", "Atlantic");
  
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
    myClim = new HK_CompleteThermostat();
}

void loop() {

  // Manage HomeKit protocol
  homeSpan.poll();

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
  }
}
