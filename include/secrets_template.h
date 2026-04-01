#ifndef SECRETS_TEMPLATE_H
#define SECRETS_TEMPLATE_H

// Copy this file to include/secrets_local.h and fill with real values.
// include/secrets_local.h is git-ignored.

#define WIFI_HOSTNAME "WioTerminalSensorPrototype"

#define API_SERVER_URL "https://example.com/api/reading"
#define API_BEARER_TOKEN "REPLACE_WITH_TOKEN"
#define API_ACK_HMAC_KEY "REPLACE_WITH_SHARED_HMAC_KEY"
// Endpoint returning JSON: {"version":"v2026.04.01","url":"https://...","size":205000}
#define API_OTA_VERSION_URL "https://example.com/api/firmware"

#define WIFI_SSID_PRIMARY "REPLACE_PRIMARY_SSID"
#define WIFI_PASSWORD_PRIMARY "REPLACE_PRIMARY_PASSWORD"

#define WIFI_SSID_ALTERNATE "REPLACE_ALTERNATE_SSID"
#define WIFI_PASSWORD_ALTERNATE "REPLACE_ALTERNATE_PASSWORD"

#define WIFI_SSID_MOBILE "REPLACE_MOBILE_SSID"
#define WIFI_PASSWORD_MOBILE "REPLACE_MOBILE_PASSWORD"

#endif // SECRETS_TEMPLATE_H
