// ─────────────────────────────────────────────────────────────────────────────
// ota.cpp  –  SD-assisted OTA with upper-flash staging for Seeed Wio Terminal
//
// HOW IT WORKS (three-phase, power-safe):
//   1. CHECK  : HTTPS GET /api/firmware  →  JSON {version, url, size}
//   2. DOWNLOAD: Stream .bin from server → SD card  (/wio_ota.bin)
//   3. STAGE  : Copy .bin from SD → upper internal flash (0x40000–0x7FFFF)
//              Store size in .noinit RAM, set magic, NVIC_SystemReset()
//   4. APPLY  : On very next boot, otaInit() detects the magic, calls the
//              .ramfunc trampoline which copies upper→lower flash, resets.
//
// Flash map (SAMD51P19A, 512 KB):
//   0x00000 – 0x03FFF   16 KB  UF2/SamBA bootloader  (write-protected)
//   0x04000 – 0x3FFFF  240 KB  Running application    (overwritten by trampoline)
//   0x40000 – 0x7FFFF  256 KB  OTA staging area       (written here in STAGE step)
//
// Risk: if power is lost during the APPLY step the device needs USB reflash.
// ─────────────────────────────────────────────────────────────────────────────
#include "ota.h"

#include <Arduino.h>
#include <rpcWiFiClientSecure.h>
#include "SD/Seeed_SD.h"
#include "app_state.h"
#include "network.h"
#include <ArduinoJson.h>

#if __has_include("secrets_local.h")
#include "secrets_local.h"
#else
#include "secrets_template.h"
#endif

#ifndef API_OTA_VERSION_URL
#define API_OTA_VERSION_URL ""
#endif
#ifndef API_BEARER_TOKEN
#define API_BEARER_TOKEN ""
#endif
#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

// ─── Flash-map constants ──────────────────────────────────────────────────────
static constexpr uint32_t kStagingBase       = 0x00040000UL; // upper half
static constexpr uint32_t kAppBase           = 0x00004000UL; // after bootloader
static constexpr uint32_t kMaxFirmwareBytes  = 240UL * 1024UL;
static constexpr uint32_t kEraseBlockBytes   = 8192UL;
static constexpr uint32_t kPageBytes         = 512UL;

static const char kOtaBinPath[] = "/wio_ota.bin";

// ─── Noinit fields – survive soft reset, cleared on cold power-on ─────────────
__attribute__((section(".noinit"))) static uint32_t sOtaMagic;
__attribute__((section(".noinit"))) static uint32_t sOtaMagicInv;
__attribute__((section(".noinit"))) static uint32_t sOtaStagedBytes;

static constexpr uint32_t kMagicA = 0xDEADC0DEUL;
static constexpr uint32_t kMagicB = 0x21523F21UL; // ~kMagicA

// ─── Runtime state ────────────────────────────────────────────────────────────
static OtaState       sState             = OtaState::IDLE;
static String         sAvailableVersion  = "";
static String         sOtaUrl            = "";
static String         sStatusMsg         = "";
static int            sDownloadPercent   = 0;
static bool           sCheckRequested    = false;
static unsigned long  sLastCheckMs       = 0;
static constexpr unsigned long kAutoCheckIntervalMs = 3600000UL; // 1 hour

// ─── NVMCTRL helpers (run from normal flash – only write to staging area) ─────
static void nvmWait() {
    while (!NVMCTRL->STATUS.bit.READY);
}

static void nvmEraseBlock(uint32_t addr) {
    nvmWait();
    NVMCTRL->ADDR.reg  = addr;
    NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_CMD_EB | NVMCTRL_CTRLB_CMDEX_KEY;
    nvmWait();
}

static void nvmWritePage(uint32_t dstAddr, const uint8_t* buf) {
    nvmWait();
    NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_CMD_PBC | NVMCTRL_CTRLB_CMDEX_KEY;
    nvmWait();
    volatile uint32_t* dst = (volatile uint32_t*)dstAddr;
    const  uint32_t*   src = (const uint32_t*)buf;
    for (uint32_t i = 0; i < kPageBytes / 4; i++) dst[i] = src[i];
    NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_CMD_WP | NVMCTRL_CTRLB_CMDEX_KEY;
    nvmWait();
}

// ─── RAM trampoline ───────────────────────────────────────────────────────────
// Placed in .ramfunc so it executes from SRAM, not flash.
// Only uses NVMCTRL hardware registers + pointer arithmetic – NO function calls
// into flash.  After rewriting lower flash it triggers SystemReset().
__attribute__((section(".ramfunc"), noinline, used))
static void applyTrampoline(uint32_t srcBase, uint32_t dstBase, uint32_t len) {
    __disable_irq();

    uint8_t pageBuf[512];

    for (uint32_t off = 0; off < len; off += kPageBytes) {
        // Erase 8 KB block at its boundary
        if ((off % kEraseBlockBytes) == 0) {
            while (!NVMCTRL->STATUS.bit.READY);
            NVMCTRL->ADDR.reg  = dstBase + off;
            NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_CMD_EB | NVMCTRL_CTRLB_CMDEX_KEY;
            while (!NVMCTRL->STATUS.bit.READY);
        }

        // Copy 512 bytes from source (upper flash) into local buffer in RAM
        uint32_t pageLen = (len - off < kPageBytes) ? (len - off) : kPageBytes;
        const uint8_t* src = (const uint8_t*)(srcBase + off);
        for (uint32_t i = 0; i < pageLen; i++)   pageBuf[i] = src[i];
        for (uint32_t i = pageLen; i < kPageBytes; i++) pageBuf[i] = 0xFF;

        // Write page via page buffer
        while (!NVMCTRL->STATUS.bit.READY);
        NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_CMD_PBC | NVMCTRL_CTRLB_CMDEX_KEY;
        while (!NVMCTRL->STATUS.bit.READY);

        volatile uint32_t* dst = (volatile uint32_t*)(dstBase + off);
        const  uint32_t*   pb4 = (const uint32_t*)pageBuf;
        for (uint32_t i = 0; i < kPageBytes / 4; i++) dst[i] = pb4[i];

        NVMCTRL->CTRLB.reg = NVMCTRL_CTRLB_CMD_WP | NVMCTRL_CTRLB_CMDEX_KEY;
        while (!NVMCTRL->STATUS.bit.READY);
    }

    NVIC_SystemReset();
    while (1); // never reached
}

// ─── HTTP helpers ─────────────────────────────────────────────────────────────
static bool parseUrl(const String& url, String& host, uint16_t& port, String& path) {
    if (!url.startsWith("https://")) return false;
    String rest = url.substring(8);
    int sl = rest.indexOf('/');
    String hp = (sl >= 0) ? rest.substring(0, sl) : rest;
    path = (sl >= 0) ? rest.substring(sl) : "/";
    int c = hp.indexOf(':');
    if (c >= 0) { host = hp.substring(0, c); port = (uint16_t)hp.substring(c + 1).toInt(); }
    else         { host = hp; port = 443; }
    return host.length() > 0;
}

// Reads response line + headers, returns HTTP status code or -1.
static int readHttpStatus(WiFiClientSecure& cl, uint32_t timeoutMs = 6000UL) {
    String line = "";
    int code = -1;
    bool firstLine = true;
    unsigned long t = millis();
    while (cl.connected() && (millis() - t) < timeoutMs) {
        while (cl.available()) {
            char ch = (char)cl.read();
            if (ch == '\r') continue;
            if (ch == '\n') {
                if (firstLine) {
                    int s1 = line.indexOf(' ');
                    if (s1 >= 0) code = line.substring(s1 + 1, s1 + 4).toInt();
                    firstLine = false;
                } else if (line.length() == 0) {
                    return code; // blank line = end of headers
                }
                line = "";
            } else {
                line += ch;
            }
            t = millis(); // reset stall timer on activity
        }
    }
    return code;
}

// ─── Stage from SD → upper flash ─────────────────────────────────────────────
static bool stageFromSd(uint32_t fileSize) {
    if (!sdcard) return false;
    File f = SD.open(kOtaBinPath, FILE_READ);
    if (!f) return false;

    // Erase staging blocks
    uint32_t eraseLen = ((fileSize + kEraseBlockBytes - 1) / kEraseBlockBytes) * kEraseBlockBytes;
    for (uint32_t off = 0; off < eraseLen; off += kEraseBlockBytes) {
        nvmEraseBlock(kStagingBase + off);
    }

    // Write pages
    uint8_t buf[kPageBytes];
    uint32_t written = 0;
    while (written < fileSize) {
        int nr = f.read(buf, sizeof(buf));
        if (nr <= 0) break;
        for (int i = nr; i < (int)kPageBytes; i++) buf[i] = 0xFF;
        nvmWritePage(kStagingBase + written, buf);
        written += kPageBytes;
        yield();
    }
    f.close();
    return (written >= fileSize);
}

// ─── Phase implementations ───────────────────────────────────────────────────
static void doCheck() {
    sStatusMsg = "Checking for updates...";
    Serial.println("EVT: ota: " + sStatusMsg);

    String url = String(API_OTA_VERSION_URL);
    if (url.length() == 0) {
        sState = OtaState::FAILED;
        sStatusMsg = "OTA URL not configured";
        return;
    }

    String host, path;
    uint16_t port = 443;
    if (!parseUrl(url, host, port, path)) {
        sState = OtaState::FAILED;
        sStatusMsg = "OTA URL invalid";
        return;
    }

    WiFiClientSecure cl;
    cl.setCACert(root_ca);
    cl.setTimeout(5);
    if (!cl.connect(host.c_str(), port)) {
        sState = OtaState::FAILED;
        sStatusMsg = "OTA check: connect failed";
        return;
    }

    cl.print(String("GET ") + path + " HTTP/1.1\r\n"
             + "Host: " + host + "\r\n"
             + "Authorization: Bearer " + String(API_BEARER_TOKEN) + "\r\n"
             + "Connection: close\r\n\r\n");

    int code = readHttpStatus(cl);
    if (code != 200) {
        cl.stop();
        sState = OtaState::FAILED;
        sStatusMsg = "OTA check: HTTP " + String(code);
        return;
    }

    String body = "";
    unsigned long t = millis();
    while (cl.connected() && (millis() - t) < 4000UL) {
        while (cl.available() && body.length() < 512) body += (char)cl.read();
        if (body.length() >= 512) break;
    }
    cl.stop();

    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        sState = OtaState::FAILED;
        sStatusMsg = "OTA check: bad JSON";
        return;
    }

    String serverVer = doc["version"] | "";
    String fwUrl     = doc["url"]     | "";
    uint32_t fwSize  = doc["size"]    | 0;

    if (serverVer.length() == 0 || fwUrl.length() == 0 || fwSize == 0) {
        sState = OtaState::FAILED;
        sStatusMsg = "OTA check: incomplete response";
        return;
    }

    if (serverVer == String(APP_VERSION)) {
        sState = OtaState::UP_TO_DATE;
        sStatusMsg = "Firmware up to date (" + serverVer + ")";
        Serial.println("EVT: ota: up to date");
        return;
    }

    sAvailableVersion = serverVer;
    sOtaUrl           = fwUrl;
    sOtaStagedBytes   = fwSize; // overwritten with actual bytes after download
    sState            = OtaState::AVAILABLE;
    sStatusMsg        = "Update available: " + serverVer;
    Serial.println("EVT: ota: available=" + serverVer);
}

static void doDownload() {
    sStatusMsg      = "Downloading " + sAvailableVersion + "...";
    sDownloadPercent = 0;
    Serial.println("EVT: ota: " + sStatusMsg);

    String host, path;
    uint16_t port = 443;
    if (!parseUrl(sOtaUrl, host, port, path)) {
        sState = OtaState::FAILED; sStatusMsg = "OTA download: bad URL"; return;
    }
    if (!sdcard) {
        sState = OtaState::FAILED; sStatusMsg = "OTA download: no SD"; return;
    }

    uint32_t expectedSize = sOtaStagedBytes;
    if (expectedSize == 0 || expectedSize > kMaxFirmwareBytes) {
        sState = OtaState::FAILED; sStatusMsg = "OTA download: bad size"; return;
    }

    WiFiClientSecure cl;
    cl.setCACert(root_ca);
    cl.setTimeout(15);
    if (!cl.connect(host.c_str(), port)) {
        sState = OtaState::FAILED; sStatusMsg = "OTA download: connect failed"; return;
    }

    cl.print(String("GET ") + path + " HTTP/1.1\r\n"
             + "Host: " + host + "\r\n"
             + "Authorization: Bearer " + String(API_BEARER_TOKEN) + "\r\n"
             + "Connection: close\r\n\r\n");

    int code = readHttpStatus(cl);
    if (code != 200) {
        cl.stop();
        sState = OtaState::FAILED; sStatusMsg = "OTA download: HTTP " + String(code); return;
    }

    SD.remove(kOtaBinPath);
    File f = SD.open(kOtaBinPath, FILE_WRITE);
    if (!f) {
        cl.stop();
        sState = OtaState::FAILED; sStatusMsg = "OTA download: SD open failed"; return;
    }

    uint8_t buf[512];
    uint32_t received = 0;
    unsigned long lastAct = millis();

    while (cl.connected() && received < expectedSize) {
        int avail = cl.available();
        if (avail > 0) {
            int toRead = min((int)sizeof(buf), min(avail, (int)(expectedSize - received)));
            int nr = cl.read(buf, toRead);
            if (nr > 0) {
                f.write(buf, nr);
                received += (uint32_t)nr;
                sDownloadPercent = (int)((received * 100L) / expectedSize);
                lastAct = millis();
            }
        } else if (millis() - lastAct > 12000UL) {
            break; // stall timeout
        }
        yield();
    }

    f.close();
    cl.stop();

    if (received < expectedSize) {
        SD.remove(kOtaBinPath);
        sState = OtaState::FAILED;
        sStatusMsg = "OTA download: incomplete " + String(received) + "/" + String(expectedSize);
        return;
    }

    sDownloadPercent = 100;
    sOtaStagedBytes  = received;
    sState           = OtaState::STAGING;
    sStatusMsg       = "Download complete. Staging...";
    Serial.println("EVT: ota: download complete " + String(received) + " bytes");
}

static void doStage() {
    sStatusMsg = "Staging to internal flash...";
    Serial.println("EVT: ota: " + sStatusMsg);

    if (sOtaStagedBytes == 0 || sOtaStagedBytes > kMaxFirmwareBytes) {
        sState = OtaState::FAILED; sStatusMsg = "OTA stage: invalid size"; return;
    }

    if (!stageFromSd(sOtaStagedBytes)) {
        sState = OtaState::FAILED; sStatusMsg = "OTA stage: write failed"; return;
    }

    sState     = OtaState::READY;
    sStatusMsg = "Ready to apply: " + sAvailableVersion + "  (UP in main to apply)";
    Serial.println("EVT: ota: staged ok, " + String(sOtaStagedBytes) + " bytes at 0x40000");
}

// ─── Public API ───────────────────────────────────────────────────────────────
void otaInit() {
    // A valid staging window exists when both halves of the magic match AND
    // the size is plausible.  False-positive risk on cold boot is negligible
    // with a 64-bit pattern.
    if (sOtaMagic    == kMagicA &&
        sOtaMagicInv == kMagicB &&
        sOtaStagedBytes > 0 &&
        sOtaStagedBytes <= kMaxFirmwareBytes) {
        uint32_t size = sOtaStagedBytes;
        sOtaMagic    = 0;
        sOtaMagicInv = 0;
        sOtaStagedBytes = 0;
        // Jump to RAM trampoline – never returns
        applyTrampoline(kStagingBase, kAppBase, size);
    }
}

void otaCheckForUpdate() {
    if (sState == OtaState::CHECKING   ||
        sState == OtaState::DOWNLOADING||
        sState == OtaState::STAGING    ||
        sState == OtaState::APPLYING)  return;
    sCheckRequested = true;
}

void otaProcessStateMachine() {
    // Periodic/user-requested version check
    if (sState == OtaState::IDLE || sState == OtaState::UP_TO_DATE ||
        sState == OtaState::FAILED) {
        bool due = String(API_OTA_VERSION_URL) != "" &&
                   (millis() - sLastCheckMs) >= kAutoCheckIntervalMs;
        if (sCheckRequested || due) {
            sCheckRequested = false;
            sLastCheckMs    = millis();
            sState = OtaState::CHECKING;
        }
    }

    switch (sState) {
        case OtaState::CHECKING:
            doCheck();
            break;
        case OtaState::AVAILABLE:
            sState = OtaState::DOWNLOADING;
            doDownload();
            break;
        case OtaState::STAGING:
            doStage();
            break;
        case OtaState::APPLYING:
            // Arm noinit magic then soft-reset; otaInit() on next boot applies.
            sOtaMagic    = kMagicA;
            sOtaMagicInv = kMagicB;
            // sOtaStagedBytes already holds the correct size from doStage()
            Serial.println("EVT: ota: rebooting to apply " + sAvailableVersion);
            delay(200);
            NVIC_SystemReset();
            break;
        default:
            break;
    }
}

OtaState otaGetState()               { return sState; }
String   otaGetAvailableVersion()    { return sAvailableVersion; }
String   otaGetStatusMessage()       { return sStatusMsg; }
int      otaGetDownloadPercent()     { return sDownloadPercent; }

void otaRequestApply() {
    if (sState == OtaState::READY) {
        sState = OtaState::APPLYING;
        sStatusMsg = "Applying update, rebooting...";
    }
}
