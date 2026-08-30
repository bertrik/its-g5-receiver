#include <stdlib.h>
#include <atomic>
#include <cstring>

#include <Arduino.h>

#include "esp_wifi.h"
#include "esp_wifi_secret.h"

#include <Adafruit_NeoPixel.h>
#include <MiniShell.h>

#define RGB_LED_PIN 27

static Adafruit_NeoPixel led(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// when configured with USB-CDC port, "Serial" goes to ACM0 (USB-CDC), "Serial0"/printf goes to UART/USB0
static MiniShell shell(&Serial);

static uint32_t rgb = 0;
static const int CHANNEL = 5900;
static const size_t QUEUE_SIZE = 8;
static const int MAX_PACKET_LEN = 2400;

typedef struct {
    unsigned long millis;
    size_t len;
    uint8_t data[MAX_PACKET_LEN];
} packet_t;

static packet_t queue[QUEUE_SIZE];
static std::atomic < int >writeIndex = 0;
static std::atomic < int >readIndex = 0;
static std::atomic < uint32_t > droppedPackets = 0;

static void set_led(uint32_t rgb)
{
    static uint32_t last_rgb = -1;
    if (last_rgb != rgb) {
        last_rgb = rgb;
        led.setPixelColor(0, rgb);
        led.show();
    }
}

static void printhex(const char *title, const uint8_t *buf, size_t len, int rowsize = 16)
{
    Serial.printf("%s", title);
    for (size_t i = 0; i < len; i++) {
        if ((rowsize > 0) && (i % rowsize) == 0) {
            Serial.printf("\n%04X:", i);
        }
        Serial.printf(" %02X", buf[i]);
    }
    Serial.printf("\n");
}

static bool enqueue(unsigned long ms, size_t length, const uint8_t *data)
{
    if (length > MAX_PACKET_LEN) {
        // Packet too big
        return false;
    }

    int w = writeIndex.load(std::memory_order_relaxed);
    int r = readIndex.load(std::memory_order_acquire);

    int next = (w + 1) % QUEUE_SIZE;
    if (next == r) {
        // Queue full
        return false;
    }
    packet_t & pkt = queue[w];

    pkt.millis = ms;
    pkt.len = length;
    memcpy(pkt.data, data, length);

    writeIndex.store(next, std::memory_order_release);
    return true;
}

static bool dequeue(packet_t *pkt)
{
    int r = readIndex.load(std::memory_order_relaxed);
    int w = writeIndex.load(std::memory_order_acquire);
    if (r == w) {
        // Queue empty
        return false;
    }
    *pkt = queue[r];

    readIndex.store((r + 1) % QUEUE_SIZE, std::memory_order_release);
    return true;
}

static void wifi_sniffer_cb(void *rawpkt, wifi_promiscuous_pkt_type_t type)
{
    // this is always the case, even if not fully obvious from the esp-idf docs
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *) rawpkt;

    // we build the PCAP packet header
    if (pkt->rx_ctrl.sig_len <= 4)
        return;
    size_t len = pkt->rx_ctrl.sig_len - 4;

    // we do not log MISC packets
    if (type == WIFI_PKT_MISC)
        return;
    // we do not log packets with errors
    if (pkt->rx_ctrl.rx_state)
        return;
    // we only log broadcast packets
    if (len < (4 + 6) || memcmp(&pkt->payload[4], "\xFF\xFF\xFF\xFF\xFF\xFF", 6))
        return;

    // enqueue, indicate error on failure
    if (!enqueue(millis(), len, pkt->payload)) {
        droppedPackets.fetch_add(1, std::memory_order_relaxed);
    }
}

static void send_its5(unsigned long micros, size_t len, const uint8_t *data)
{
    uint32_t sec = micros / 1000000;
    uint32_t usec = micros % 1000000;

    uint8_t header[14];
    uint8_t *p = header;
    *p++ = 'I';
    *p++ = 'T';
    *p++ = 'S';
    *p++ = '5';
    *p++ = sec & 0xFF;
    *p++ = (sec >> 8) & 0xFF;
    *p++ = (sec >> 16) & 0xFF;
    *p++ = (sec >> 24) & 0xFF;
    *p++ = usec & 0xFF;
    *p++ = (usec >> 8) & 0xFF;
    *p++ = (usec >> 16) & 0xFF;
    *p++ = (usec >> 24) & 0xFF;
    *p++ = len & 0xFF;
    *p++ = (len >> 8) & 0xFF;

    Serial0.write(header, sizeof(header));
    Serial0.write(data, len);
}

static int do_reboot(int argc, char *argv[])
{
    ESP.restart();
    return 0;
}

static int do_send(int argc, char *argv[])
{
    const char *text = (argc > 1) ? argv[1] : "Hello world!";
    return enqueue(millis(), strlen(text), (const uint8_t *) text) ? 0 : -1;
}

static const cmd_t commands[] = {
    { "reboot", do_reboot, "Reboot" },
    { "send", do_send, "<message> Send a message over serial" },
    { NULL, NULL, NULL }
};

void setup(void)
{
    pinMode(LED_BUILTIN, OUTPUT);

    Serial0.begin(115200);
    Serial.begin(115200);
    Serial.println("Starting ITS G-5 sniffer...");

    // needed by esp wifi driver
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // initialize esp wifi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));

    // initialize everything so that we can sniff ITS G-5
    wifi_promiscuous_filter_t wifi_filter;
    wifi_filter.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL & ~WIFI_PROMIS_FILTER_MASK_FCSFAIL;
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&wifi_filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    phy_11p_set(1, 0);
    ESP_ERROR_CHECK(esp_wifi_set_channel(140, WIFI_SECOND_CHAN_NONE));
    phy_change_channel(CHANNEL, 1, 0, 0);

    led.begin();
    led.setBrightness(32);
    set_led(0);
}

void loop(void)
{
    // determine status led color
    div_t divided = div(millis(), 1000);
    long int sec = divided.quot;
    long int msec = divided.rem;
    set_led((msec < 50) ? 0x000044 : 0);

    // send packets over SLIP
    packet_t pkt;
    while (dequeue(&pkt)) {
        set_led(0x00FF00);
        send_its5(micros(), pkt.len, pkt.data);
        set_led(0);
    }

    // command line processing
    shell.process(">", commands);
}
