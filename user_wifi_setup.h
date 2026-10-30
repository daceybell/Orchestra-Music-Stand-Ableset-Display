#ifndef USER_WIFI_SETUP_H
#define USER_WIFI_SETUP_H

// ============================================================================
// WI-FI & NETWORK CONFIGURATION
// ============================================================================

struct WifiNetwork { 
  const char *ssid; 
  const char *pass; 
  const char *ablesetIp; // Static IP assigned per network
};

// Configured Wi-Fi networks with designated AbleSet IPs
// DELETE -or- ADD new lines if needed

static const WifiNetwork WIFI_NETWORKS[] = {
  { "YOUR_SSID_1",      "YOUR_PASSWORD_1",      "YOUR_ABLESET_IP_ADDRESS_1" },
  { "YOUR_SSID_2",      "YOUR_PASSWORD_2",      "YOUR_ABLESET_IP_ADDRESS_2" },
  { "YOUR_SSID_3",      "YOUR_PASSWORD_3",      "YOUR_ABLESET_IP_ADDRESS_3" },
};

static const int N_WIFI = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

static const uint32_t WIFI_TRY_MS   = 10000;  // per-network connect timeout
static const uint32_t WIFI_RETRY_MS = 10000;  // wait between reconnect attempts

// How many times to go through the network list before showing Retry screen
static const int WIFI_ROUNDS = 2;

// Leave empty ("") to auto-select via SSID or mDNS discovery.
static const char *ABLESET_HOST = "";

static const uint16_t ABLESET_PORT = 39051;
static const uint16_t LOCAL_PORT   = 8000;

static const char *DEVICE_NAME = "AbleSet Display C6"; // shown in AbleSet's device list

static const uint8_t  BACKLIGHT_PCT = 40;     // Set brightness to 40%
static const bool     USE_RGB_LED   = false;
static const bool     USE_FILTERS   = true;   // Filter OSC feedback stream

#endif // USER_WIFI_SETUP_H