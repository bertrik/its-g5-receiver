#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <NetworkEvents.h>
#include <LittleFS.h>
#include <ESPmDNS.h>

#include <ESPAsyncWebServer.h>
#include "MiniShell.h"
#include <PubSubClient.h>
#include <MicroSlip.h>

#include "config.h"
#include "parse.h"

static AsyncWebServer server(80);
static MiniShell shell(&Serial);
static WiFiClient wifiClient;
static WiFiClientSecure wifiClientSecure;
static WiFiEvent_t lastWifiEvent = ARDUINO_EVENT_NONE;
static WiFiEvent_t wifiEvent = ARDUINO_EVENT_NONE;
static String rootCA;
static PubSubClient mqttClient;
static char esp_mac[24];        // e.g. "aa:bb:cc:dd:ee:ff"
static char esp_id[16];
static MicroSlip slip(Serial0);
static uint8_t packet[2500];
static StaticJsonDocument < 1024 > infoDoc;

static char mqtt_status_topic[256];
static char mqtt_info_topic[256];
static char mqtt_packet_topic[256];
static char mqtt_info[256];

static void blue_led(bool on)
{
    digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
}

static bool mqtt_connect(void)
{
    if (mqttClient.connected()) {
        // already connected
        return true;
    }
    char proto[8];
    char host[128];
    char user[64];
    char pass[64];
    strlcpy(proto, config_get_value("mqtt_protocol").c_str(), sizeof(proto));
    strlcpy(host, config_get_value("mqtt_broker_host").c_str(), sizeof(host));
    strlcpy(user, config_get_value("mqtt_user").c_str(), sizeof(user));
    strlcpy(pass, config_get_value("mqtt_pass").c_str(), sizeof(pass));
    int port = config_get_value("mqtt_broker_port").toInt();
    bool secure = (strcmp(proto, "mqtts") == 0);

    mqttClient.setClient(secure ? wifiClientSecure : wifiClient);
    mqttClient.setServer(host, port);
    bool result;
    if (strlen(user) > 0) {
        printf("Connecting to %s@%s://%s:%d...", user, proto, host, port);
        result = mqttClient.connect(esp_id, user, pass, mqtt_status_topic, 0, true, "", true);
    } else {
        printf("Connecting to %s://%s:%d ...", proto, host, port);
        result = mqttClient.connect(esp_id, NULL, NULL, mqtt_status_topic, 0, true, "", true);
    }
    if (result) {
        printf("connected!\n");
        mqttClient.publish(mqtt_status_topic, "online", true);
        mqttClient.publish(mqtt_info_topic, mqtt_info);
    } else {
        printf("failed to connect, rc=%d\n", mqttClient.state());
    }
    return result;
}

static bool mqtt_send(const uint8_t *packet, size_t length)
{
    if (!mqttClient.connected()) {
        printf("Not connected to MQTT broker\n");
        return false;
    }

    return mqttClient.publish(mqtt_packet_topic, (const uint8_t *) packet, length);
}

static void handleWifiEvent(WiFiEvent_t event)
{
    wifiEvent = event;
}

static int do_network(int argc, char *argv[])
{
    if (argc > 1) {
        char *ssid = argv[1];
        const char *pass = (argc > 2) ? argv[2] : "";
        WiFi.disconnect();
        delay(1000);
        printf("Starting WiFi...");
        WiFi.begin(ssid, pass);
        printf("done\n");
    }
    wl_status_t status = WiFi.status();
    printf("SSID:    %s\n", WiFi.SSID().c_str());
    printf("Status:  %d\n", status);
    printf("Inet:    %s\n", WiFi.localIP().toString().c_str());
    printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    printf("Netmask: %s\n", WiFi.subnetMask().toString().c_str());
    printf("Web url: http://%s\n", WiFi.localIP().toString().c_str());
    return status == WL_CONNECTED ? 0 : status;
}

static int do_reboot(int argc, char *argv[])
{
    ESP.restart();
    return 0;
}

static int do_datetime(int argc, char *argv[])
{
    time_t now = time(NULL);

    struct tm *utc = gmtime(&now);
    printf("UTC  : %4d-%02d-%02d %02d:%02d:%02d\n",
           1900 + utc->tm_year, 1 + utc->tm_mon, utc->tm_mday,
           utc->tm_hour, utc->tm_min, utc->tm_sec);
    struct tm *local = localtime(&now);
    printf("Local: %4d-%02d-%02d %02d:%02d:%02d\n",
           1900 + local->tm_year, 1 + local->tm_mon, local->tm_mday,
           local->tm_hour, local->tm_min, local->tm_sec);
    return 0;
}

static int do_disconnect(int argc, char *argv[])
{
    if (mqttClient.connected()) {
        mqttClient.disconnect();
        printf("Disconnected from MQTT broker\n");
    } else {
        printf("Not connected to MQTT broker\n");
    }
    return 0;
}

static int do_led(int argc, char *argv[])
{
    bool state = (argc > 1) ? atoi(argv[1]) : !digitalRead(LED_BUILTIN);
    digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
    return 0;
}

static size_t create_info(char *info, size_t size)
{
    infoDoc["emac"] = esp_mac;
    infoDoc["ver"] = "github.com/bertrik/its-g5-receiver-v0.0.1";
    infoDoc["hwv"] = "esp32-c5-devkit-c1";
    return serializeJson(infoDoc, info, size);
}

static int do_info(int argc, char *argv[])
{
    printf("node: %s\n", esp_id);
    printf("mqtt_status_topic: %s\n", mqtt_status_topic);
    printf("mqtt_packet_topic: %s\n", mqtt_packet_topic);
    printf("mqtt_info_topic: %s\n", mqtt_info_topic);
    printf("info: %s\n", mqtt_info);
    return 0;
}

static int do_config(int argc, char *argv[])
{
    File f = LittleFS.open("/config.json", "r");
    if (f) {
        Serial.println(f.readString());
        f.close();
    }
    return 0;
}

static const cmd_t commands[] = {
    { "network", do_network, "[<ssid> [password]] Configure WIFi / show network" },
    { "reboot", do_reboot, "Reboot" },
    { "datetime", do_datetime, "Display date and time" },
    { "disconnect", do_disconnect, "Disconnect from MQTT" },
    { "led", do_led, "[state]Toggle LED" },
    { "info", do_info, "Show info string" },
    { "config", do_config, "Show configuration" },
    { NULL, NULL, NULL }
};

void setup(void)
{
    pinMode(LED_BUILTIN, OUTPUT);
    blue_led(true);

    Serial.begin(115200);
    Serial.println("Hello from ESP32-C3 bridge!");

    // initialise the hardware UART on its default pins for SLIP
    Serial0.begin(115200);

    // get unique ESP32-C3 ID
    uint64_t chipid = ESP.getEfuseMac();
    char *pid = esp_id;
    char *pemac = esp_mac;
    for (int i = 0; i < 6; i++) {
        pid += sprintf(pid, "%02x", chipid & 0xFF);
        pemac += sprintf(pemac, (i < 5) ? "%02x:" : "%02x", chipid & 0xFF);
        chipid >>= 8;
    }
    printf("espid = %s\n", esp_id);
    sprintf(mqtt_status_topic, "its/%s/status", esp_id);
    sprintf(mqtt_packet_topic, "its/%s/packet", esp_id);
    sprintf(mqtt_info_topic, "its/%s/info", esp_id);
    create_info(mqtt_info, sizeof(mqtt_info));

    configTzTime("CET-1CEST,M3.5.0/02,M10.5.0/03", "pool.ntp.org");
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(handleWifiEvent);
    WiFi.begin();

    // load settings, save defaults if necessary
    LittleFS.begin();
    config_begin(LittleFS, "/config.json");
    if (!config_load()) {
        config_set_value("mqtt_protocol", "mqtt");
        config_set_value("mqtt_broker_host", "");
        config_set_value("mqtt_broker_port", "1883");
        config_set_value("mqtt_user", "");
        config_set_value("mqtt_pass", "");
        config_save();
    }
    config_serve(server, "/config", "/config.html");
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();

    File f = LittleFS.open("/isrgrootx1.pem", "r");
    if (f) {
        rootCA = f.readString();
        f.close();
        printf("Loading root CA certificate:\n");
        wifiClientSecure.setCACert(rootCA.c_str());
    } else {
        Serial.println("Failed to load CA certificate");
    }

    MDNS.begin("its-g5-bridge");
    MDNS.addService("_http", "_tcp", 80);
}

void loop(void)
{
    static uint32_t last_connect = 0;
    uint32_t now = millis() / 1000;

    // network status
    bool online = (WiFi.status() == WL_CONNECTED) && (time(nullptr) > 1700000000L);
    if (lastWifiEvent != wifiEvent) {
        lastWifiEvent = wifiEvent;
        printf("WiFi event: %s\n", NetworkEvents::eventName(wifiEvent));
    }
    // try to get MQTT connected
    if (online && !mqttClient.connected() && ((now - last_connect) > 10)) {
        // show blue while still connecting, off when connected
        bool connected = mqtt_connect();
        blue_led(!connected);
        last_connect = now;
    }
    mqttClient.loop();

    // watch for incoming packets
    size_t pkt_size = slip.parsePacket(packet, sizeof(packet));
    if (pkt_size > 0) {
        blue_led(true);
        printf("Got packet %d bytes\n", pkt_size);
        if (online && mqttClient.connected()) {
            mqtt_send(packet, pkt_size);
        }
        blue_led(false);

        ieee80211_t ieee;
        if (parse_ieee80211(packet, pkt_size, &ieee) > 0) {
            printf("IEEE 802.11 packet from %02x:%02x:%02x:%02x:%02x:%02x, sequence control: %04x\n",
                   ieee.source_mac[0], ieee.source_mac[1], ieee.source_mac[2],
                   ieee.source_mac[3], ieee.source_mac[4], ieee.source_mac[5], ieee.sequence_ctrl);
        }
    }
    // command line processing
    shell.process(">", commands);
}
