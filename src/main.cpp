#include <Arduino.h>
#include "HomeSpan.h"
#include "FujiHeatPump.h"
#include <ostream>

// Instanciation de la bibliothèque de gestion du bus 3 fils Atlantic
FujiHeatPump hp;

// Définition des broches UART pour l'ESP32-C3
#define LIN_RX_PIN 18
#define LIN_TX_PIN 19
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
  // Initialisation de la console de debogage via USB CDC
  Serial.begin(115200);
  
  // Configuration de l'UART secondaire matériel pour le transceiver LIN
  // Le protocole Atlantic utilise une vitesse stricte de 19200 bauds en mode 8E1 (8 bits, parité paire, 1 stop bit)
  //Serial1.begin(19200, SERIAL_8E1, LIN_RX_PIN, LIN_TX_PIN);

  Serial.println("Clim Atlantic PlatformIO - HomeSpan");

  // Connexion au bus en mode CONTROLEUR SECONDAIRE (paramètre true)
  // Indispensable pour laisser l'UTY-RNNUM d'origine opérer en maître sur le bus
  // hp.connect(&Serial1, true); 

  // Initialisation de l'accessoire HomeSpan
#if 1
  homeSpan.setLogLevel(0);
  
  homeSpan.setApSSID("Atlantic-AP");
  homeSpan.setApPassword(""); // Must be at least 8 characters if required
//  homeSpan.enableAutoStartAP(); 
  homeSpan.setSerialInputDisable(true);
  homeSpan.setControlPin(BUTTON_PIN,PushButton::TRIGGER_ON_LOW);
  homeSpan.setStatusPin(LED_PIN);
  homeSpan.begin(Category::Thermostats, "Clim Atlantic PlatformIO");
  
  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
    myClim = new HK_CompleteThermostat();
#endif
}

void loop() {

  // Traitement du protocole HomeKit
  homeSpan.poll();

#if 0
  // Écoute en arrière-plan des trames du bus LIN partagé avec l'UTY-RNNUM
  if(hp.waitForFrame()) {
    // Retour d'état automatique si modification depuis le thermostat physique
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
#endif
}
