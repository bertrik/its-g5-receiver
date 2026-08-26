#include <Arduino.h>
#include <Esp.h>

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
#include "stats.h"
#include "sysinfo.h"

#include "version.h"

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

static char mqtt_info[256];
static char mqtt_status_topic[128];
static char mqtt_info_topic[128];
static char mqtt_stats_topic[128];
static char mqtt_packet_topic[128];

static void blue_led(int on)
{
    static int last_on = -1;
    if (on != last_on) {
        last_on = on;
        digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
    }
}

// publish MQTT message, non-retained, Qos 0, returns true if successful
static bool mqtt_publish(const char *topic, const uint8_t *payload, size_t length)
{
    if (mqttClient.connected()) {
        return mqttClient.publish(topic, payload, length);
    }
    return false;
}

static bool mqtt_connect(void)
{
    if (mqttClient.connected()) {
        // already connected
        return true;
    }
    char proto[16];
    char host[128];
    char user[64];
    char pass[64];
    strlcpy(proto, config_get_value("mqtt_protocol").c_str(), sizeof(proto));
    strlcpy(host, config_get_value("mqtt_broker_host").c_str(), sizeof(host));
    strlcpy(user, config_get_value("mqtt_user").c_str(), sizeof(user));
    strlcpy(pass, config_get_value("mqtt_pass").c_str(), sizeof(pass));
    int port = config_get_value("mqtt_broker_port").toInt();
    if (strlen(host) == 0) {
        // no broker configured, do not attempt to connect
        return false;
    }
    if (strcmp(proto, "mqtts") == 0) {
        if (strcmp(config_get_value("mqtt_insecure").c_str(), "true") == 0) {
            wifiClientSecure.setInsecure();
        } else {
            wifiClientSecure.setCACert(rootCA.c_str());
        }
        mqttClient.setClient(wifiClientSecure);
    } else {
        mqttClient.setClient(wifiClient);
    }
    mqttClient.setServer(host, port);
    mqttClient.setBufferSize(2500);
    bool result;
    char *userp = NULL;
    char *passp = NULL;;
    if (strlen(user) > 0) {
        userp = user;
        passp = pass;
    }
    printf("Connecting to %s://%s:%d ...", proto, host, port);
    uint32_t t0 = millis();
    result = mqttClient.connect(esp_id, userp, passp, mqtt_status_topic, 0, true, "offline", true);
    uint32_t duration = millis() - t0;
    printf(" %d ms...", duration);
    if (result) {
        printf("connected!\n");
        mqttClient.publish(mqtt_status_topic, "online", true);
        mqttClient.publish(mqtt_info_topic, mqtt_info);
    } else {
        printf("failed to connect, rc=%d\n", mqttClient.state());
    }
    return result;
}

static void handleWifiEvent(WiFiEvent_t event)
{
    wifiEvent = event;
}

static int do_wifi(int argc, char *argv[])
{
    if (argc > 1) {
        char *ssid = argv[1];
        const char *pass = (argc > 2) ? argv[2] : "";
        WiFi.disconnect(true, true);
        delay(1000);
        printf("Starting WiFi %s with password '%s'...", ssid, pass);
        WiFi.begin(ssid, pass);
        printf("done\n");
    }
    printf("SSID:    %s\n", WiFi.SSID().c_str());
    return WiFi.status();
}

static int do_network(int argc, char *argv[])
{
    wl_status_t status = WiFi.status();
    printf("SSID:    %s\n", WiFi.SSID().c_str());
    printf("Status:  %d\n", status);
    printf("RSSI:    %d\n", WiFi.RSSI());
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
    infoDoc["ver"] = "github.com/bertrik/its-g5-receiver@" GIT_VERSION;
    infoDoc["hwv"] = "esp32-c5-devkit-c1";
    return serializeJson(infoDoc, info, size);
}

static int do_mqtt(int argc, char *argv[])
{
    printf("mqtt_connection: %s\n", mqttClient.connected()? "connected" : "not connected");
    printf("node/clientid: %s\n", esp_id);
    printf("mqtt_status_topic: %s\n", mqtt_status_topic);
    printf("mqtt_info_topic: %s\n", mqtt_info_topic);
    printf("mqtt_stats_topic: %s\n", mqtt_stats_topic);
    printf("mqtt_packet_topic: %s\n", mqtt_packet_topic);
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

static int do_sysinfo(int argc, char *argv[])
{
    printf("Chip model: %s (rev 0x%02X)\n", ESP.getChipModel(), ESP.getChipRevision());
    printf("CPU freq: %d MHz\n", ESP.getCpuFreqMHz());
    printf("CPU temp: %.1f C\n", temperatureRead());
    printf("ROM size: %d bytes\n", ESP.getFlashChipSize());
    printf("ROM freq: %d MHz\n", ESP.getFlashFrequencyMHz());
    return 0;
}

static int do_tx(int argc, char *argv[])
{
    if (argc > 1) {
        int tx_power = atoi(argv[1]);
        printf("Setting TX power to %d dBm\n", tx_power);
        WiFi.setTxPower((wifi_power_t) (4 * tx_power));
    }
    printf("Current TX power: %d dBm\n", WiFi.getTxPower() / 4);
    return 0;
}

static int do_stats(int argc, char *argv[])
{
    stats_t stats;
    stats_get(&stats);
    printf("latest: %d\n", stats.latest);
    printf("counts:");
    for (int i = 0; i < 60; i++) {
        printf(" %d", stats.counts[i]);
    }
    printf("\n");
    return 0;
}

int do_cpu(int argc, char *argv[])
{
    if (argc > 1) {
        int mhz = atoi(argv[1]);
        printf("Setting CPU speed to %d MHz\n", mhz);
        setCpuFrequencyMhz(mhz);
    }
    printf("Current CPU speed: %d MHz\n", ESP.getCpuFreqMHz());
    return 0;
}

int do_ls(int argc, char *argv[])
{
    File root = LittleFS.open("/");
    if (!root) {
        printf("Failed to open root directory\n");
        return -1;
    }
    if (!root.isDirectory()) {
        printf("Root is not a directory\n");
        return -1;
    }
    File file = root.openNextFile();
    while (file) {
        printf("%6u %s\n", file.size(), file.name());
        file = root.openNextFile();
    }
    return 0;
}

static const cmd_t commands[] = {
    { "wifi", do_wifi, "[<ssid> [password]] Configure WIFi" },
    { "network", do_network, "Show network status" },
    { "reboot", do_reboot, "Reboot" },
    { "datetime", do_datetime, "Display date and time" },
    { "disconnect", do_disconnect, "Disconnect from MQTT" },
    { "led", do_led, "[state]Toggle LED" },
    { "mqtt", do_mqtt, "Show mqtt information" },
    { "config", do_config, "Show configuration" },
    { "sysinfo", do_sysinfo, "Show system information" },
    { "tx", do_tx, "Set WiFi tx power" },
    { "stats", do_stats, "Show statistic internals" },
    { "cpu", do_cpu, "<MHz> Set CPU speed" },
    { "ls", do_ls, "List files" },
    { NULL, NULL, NULL }
};

void setup(void)
{
    // configure LED and turn off
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    // configure SPI idle
    pinMode(SCK, INPUT_PULLUP);
    pinMode(MOSI, INPUT_PULLUP);
    pinMode(MISO, INPUT_PULLUP);
    pinMode(SS, INPUT_PULLUP);

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
    sprintf(mqtt_stats_topic, "its/%s/stats", esp_id);
    create_info(mqtt_info, sizeof(mqtt_info));

    configTzTime("CET-1CEST,M3.5.0/02,M10.5.0/03", "pool.ntp.org");
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(handleWifiEvent);
    WiFi.begin();
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    // load settings, save defaults if necessary
    LittleFS.begin();
    config_begin(LittleFS, "/config.json");
    if (!config_load()) {
        config_set_value("ntp_server", "pool.ntp.org");
        config_set_value("mqtt_insecure", "true");
        config_set_value("mqtt_protocol", "mqtts");
        config_set_value("mqtt_broker_host", "");
        config_set_value("mqtt_broker_port", "1883");
        config_set_value("mqtt_user", "");
        config_set_value("mqtt_pass", "");
        config_set_value("sys_cpu_speed", "160");
        config_save();
    }
    config_serve(server, "/config", "/config.html");
    stats_begin();
    stats_serve(server, "/stats");
    sysinfo_begin(esp_id);
    sysinfo_serve(server, "/sysinfo");

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();

    printf("Reading root CA certificate...");
    File f = LittleFS.open("/isrgrootx1.pem", "r");
    if (f) {
        rootCA = f.readString();
        f.close();
        printf("OK\n");
    } else {
        printf("Failed\n");
    }

    MDNS.begin("its-g5-bridge");
    MDNS.addService("_http", "_tcp", 80);

    int mhz = config_get_value("sys_cpu_speed").toInt();
    printf("Switching to %d MHz...%s\n", mhz, setCpuFrequencyMhz(mhz) ? "OK" : "FAILED");
}

void loop(void)
{
    static uint32_t last_connect = 0;
    uint32_t t = millis();
    uint32_t now = t / 1000;
    uint32_t ms = t % 1000;

    // network status
    bool online = (WiFi.status() == WL_CONNECTED) && (time(nullptr) > 1700000000L);
    if (lastWifiEvent != wifiEvent) {
        lastWifiEvent = wifiEvent;
        printf("WiFi event: %s\n", NetworkEvents::eventName(wifiEvent));
    }
    blue_led(ms < 500 ? !online : !mqttClient.connected());

    // keep MQTT connected
    if (online && !mqttClient.connected() && ((now - last_connect) > 10)) {
        // show blue while still connecting, off when connected
        bool connected = mqtt_connect();
        last_connect = now;
    }
    mqttClient.loop();

    // watch for incoming packets
    size_t pkt_size;
    while ((pkt_size = slip.parsePacket(packet, sizeof(packet))) > 0) {
        stats_count(1);

        // send over mqtt
        blue_led(true);
        printf("Got packet %d bytes\n", pkt_size);
        if (online && mqttClient.connected()) {
            mqtt_publish(mqtt_packet_topic, packet, pkt_size);
        }
        blue_led(false);

        // log to console
        ieee80211_t ieee;
        if (parse_ieee80211(packet, pkt_size, &ieee) > 0) {
            printf
                ("IEEE 802.11 packet from %02x:%02x:%02x:%02x:%02x:%02x, sequence control: %04x\n",
                 ieee.source_mac[0], ieee.source_mac[1], ieee.source_mac[2], ieee.source_mac[3],
                 ieee.source_mac[4], ieee.source_mac[5], ieee.sequence_ctrl);
        }
    }

    // keep stats up-to-date
    if (stats_update()) {
        StaticJsonDocument < 128 > doc;
        char temp[16];
        snprintf(temp, sizeof(temp), "%.1f", temperatureRead());
        doc["temp"] = temp;
        uint8_t json[128];
        size_t size = serializeJson(doc, json);
        if ((size > 0) && mqtt_publish(mqtt_stats_topic, json, size)) {
            printf("Published %s: %s\n", mqtt_stats_topic, json);
        }
    }
    // command line processing
    shell.process(">", commands);

    // spend some time in low-power mode
    delay(50);
}
