#include <stdint.h>
#include <stdio.h>

#include "Arduino.h"
#include "WifiEspNow.h"
#include "ESP8266WiFi.h"
#include "EEPROM.h"

#include "print.h"

static const char AP_NAME[] = "revspace-espnow";
static const uint8_t SKIP_TXT[] = "SKIP";

typedef enum {
    E_SEND,
    E_ACK,
    E_DISCOVER,
    E_SLEEP
} skip_mode_t;

static skip_mode_t mode = E_SEND;

void setup(void)
{
    // welcome
    PrintInit(115200);
    print("\nESPNOW-SENSOR\n");

    // show blue LED to indicate we are on
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 0);

    WifiEspNow.begin();
    EEPROM.begin(512);
}

static bool find_ap(const char *name, struct WifiEspNowPeerInfo *peer)
{
    // scan for networks and try to find our AP
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        print("%s\n", WiFi.SSID(i).c_str());
        if (strcmp(name, WiFi.SSID(i).c_str()) == 0) {
            // copy receiver data
            peer->channel = WiFi.channel(i);
            memcpy(peer->mac, WiFi.BSSID(i), sizeof(peer->mac));
            return true;
        }
    }
    // not found
    return false;
}

static void send_topic_text(uint8_t *mac, const char *topic, const char *text)
{
    char buf[250];
    int n = snprintf(buf, sizeof(buf), "%s %s", topic, text);
    WifiEspNow.send(mac, (uint8_t *)buf, n);
}

static bool valid_peer(struct WifiEspNowPeerInfo *peer)
{
    return (peer->channel >= 1) && (peer->channel <= 14); 
}

void loop(void)
{
    WifiEspNowSendStatus status;
    struct WifiEspNowPeerInfo recv;

    switch (mode) {

    case E_SEND:
        // read last known receiver info from EEPROM
        EEPROM.get(0, recv);
        if (valid_peer(&recv)) {
            // send message to last known address
            print("Sending message to %02X:%02X:%02X:%02X:%02X:%02X (chan %d)...",
                  recv.mac[0], recv.mac[1], recv.mac[2], recv.mac[3], recv.mac[4], recv.mac[5], recv.channel);

            WifiEspNow.addPeer(recv.mac, recv.channel, nullptr);
            send_topic_text(recv.mac, "revspace/sensor/ding", "waarde");

            mode = E_ACK;
        } else {
            mode = E_DISCOVER;
        }
        break;

    case E_ACK:
        // wait for tx ack
        status = WifiEspNow.getSendStatus();
        switch (status) {
        case WifiEspNowSendStatus::NONE:
            if (millis() > 3000) {
                print("TX ack timeout\n");
                mode = E_DISCOVER;
            }
            break;
        case WifiEspNowSendStatus::OK:
            print("TX success\n");
            mode = E_SLEEP;
            break;
        case WifiEspNowSendStatus::FAIL:
        default:
            print("TX failed\n");
            mode = E_DISCOVER;
            break;
        }
        break;

    case E_DISCOVER:
        print("Discovering master ...\n");
        if (find_ap(AP_NAME, &recv)) {
            // save it in EEPROM
            print("found '%s' at %02X:%02X:%02X:%02X:%02X:%02X (chan %d), saving to EEPROM", AP_NAME,
                recv.mac[0], recv.mac[1], recv.mac[2], recv.mac[3], recv.mac[4], recv.mac[5], recv.channel);
            EEPROM.put(0, recv);
            EEPROM.end();
        } else {
            print("no master found!\n");
        }
        mode = E_SLEEP;
        break;

    case E_SLEEP:
    default:
        print("Going to sleep...");
        print("was awake for %ld millis\n", millis());
        ESP.deepSleep(0, WAKE_RF_DEFAULT);
        break;
    }
}

