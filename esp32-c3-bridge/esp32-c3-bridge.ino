#include <Arduino.h>
#include <WiFi.h>

#include "MiniShell.h"

static MiniShell shell(&Serial);

static int do_wifi(int argc, char *argv[])
{
    if (argc > 1) {
        char *ssid = argv[1];
        const char *pass = (argc > 2) ? argv[2] : "";
        WiFi.begin(ssid, pass);
    }
    printf("SSID:     %s\n", WiFi.SSID().c_str());
    printf("GateWay:  %s\n", WiFi.gatewayIP().toString().c_str());
    printf("IP addr:  %s\n", WiFi.localIP().toString().c_str());
    printf("Web page: http://%s\n", WiFi.localIP().toString().c_str());
    return (WiFi.status() == WL_CONNECTED) ? 0 : -2;
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

static const cmd_t commands[] = {
    { "wifi", do_wifi, "<ssid> <password> Set WiFi credentials" },
    { "reboot", do_reboot, "Reboot" },
    { "datetime", do_datetime, "Display date and time" },
    { NULL, NULL, NULL }
};

void setup(void)
{
    Serial.begin(115200);
    Serial.println("Hello from ESP32-C3 bridge!");

    configTzTime("CET-1CEST,M3.5.0/02,M10.5.0/03", "pool.ntp.org");
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin();
}

void loop(void)
{
    // command line processing
    shell.process(">", commands);
}
