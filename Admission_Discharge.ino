#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <zoho-iot-client.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "certificate.h"
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0,U8X8_PIN_NONE);
Preferences preferences;

#define SSID ""
#define PASSWORD ""

#define RST_PIN 15
#define SS_PIN  5
MFRC522 mfrc522(SS_PIN, RST_PIN);

#define MQTT_USERNAME (char *)""
#define MQTT_PASSWORD (char *)""

WiFiClientSecure espClient;
ZohoIOTClient zClient(&espClient, true);

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
}

void setup() {
  Serial.begin(9600);
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  setup_wifi();
  espClient.setCACert(root_ca);
  preferences.begin("uidstore", false);

  zClient.init(MQTT_USERNAME, MQTT_PASSWORD);
  zClient.connect();
  Serial.println("Connected and subscribed to Zoho IoT");

  SPI.begin();
  mfrc522.PCD_Init();
  delay(4);
  mfrc522.PCD_DumpVersionToSerial();
}

String readUID() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial())
    return "";

  String uid = "";
  for (uint8_t i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

void displayMessage(const char* msg) {
  u8g2.clearBuffer();
  u8g2.drawStr(5, 24, msg);
  u8g2.sendBuffer();
}

void loop() {
  zClient.zyield();

  displayMessage("Scan Tag");

  String uid = readUID();
  if (uid == "") return;

  Serial.println("Scanned UID: " + uid);

  // Only store UID if it's not a doctor
  if (uid != "036A1B30" && uid != "5420FB98") {
    preferences.putString("last_uid", uid);
  }

  if (uid == "036A1B30" || uid == "5420FB98") {
    displayMessage("Doctor scanned");
    delay(2000);
    displayMessage("Scan Patient");

    String patient_uid = "";
    unsigned long start = millis();
    while ((millis() - start) < 8000) {  
      patient_uid = readUID();
      if (patient_uid != "") break;
    }

    if (patient_uid != "") {
      Serial.println("Patient UID: " + patient_uid);
      zClient.addDataPointString("doctor", uid.c_str(), patient_uid.c_str());
      displayMessage("Patient Tagged");
    } else {
      displayMessage("No Patient Found");
    }

  } else {
    if (!preferences.isKey(uid.c_str())) {
      preferences.putBool(uid.c_str(), true);
      zClient.addDataPointString("status", "admitted", uid.c_str());
      displayMessage("Admitted");
    } else {
      preferences.putBool(uid.c_str(), false);
      zClient.addDataPointString("status", "discharge", uid.c_str());
      preferences.remove(uid.c_str());
      displayMessage("Discharged");
    }
  }

  String payload = zClient.getPayload().c_str();
  Serial.println("dispatching message: " + payload);
  int rc = zClient.dispatch();
  if (rc == zClient.SUCCESS) {
    Serial.println("Message published successfully");
  } else {
    Serial.println("Failed to publish message");
  }

  delay(3000);
}
