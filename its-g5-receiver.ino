#include <Arduino.h>

#include <atomic>
#include <cstring>

#include "esp_wifi.h"
#include "esp_wifi_secret.h"

#include "Adafruit_NeoPixel.h"
#include "MiniShell.h"

#define RGB_LED_PIN 27

static Adafruit_NeoPixel led(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// when configured with USB-CDC port, "Serial" goes to ACM0 (USB-CDC), "Serial0"/printf goes to USB0
static MiniShell shell(&Serial);

static uint32_t rgb = 0;
static const int CHANNEL = 5900;
static const size_t QUEUE_SIZE = 8;
static const int MAX_PACKET_LEN = 2304;

static const uint8_t CAM_MSG[] = {
    0x88, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xD2, 0x6E, 0xBC, 0xB3, 0x2D, 0x10,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x0E, 0x25, 0x00, 0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00,
    0x89, 0x47, 0x12, 0x00, 0x05, 0x01, 0x03, 0x81, 0x00, 0x40, 0x03, 0x80, 0x56, 0x20, 0x50, 0x02,
    0x80, 0x00, 0x32, 0x01, 0x00, 0x14, 0x00, 0xD2, 0x6E, 0xBC, 0xB3, 0x2D, 0x10, 0x11, 0x58, 0x4D,
    0x11, 0x1F, 0x02, 0x07, 0x5B, 0x02, 0xCC, 0x2A, 0x0A, 0x85, 0x50, 0x09, 0x07, 0x00, 0x00, 0xA0,
    0x00, 0x07, 0xD1, 0x00, 0x00, 0x02, 0x02, 0xBC, 0xB3, 0x2D, 0x10, 0x4E, 0x2F, 0x00, 0x5A, 0x94,
    0xDD, 0xF1, 0x4D, 0xC2, 0xBF, 0x4D, 0x4C, 0xD8, 0xCC, 0xE9, 0xBE, 0x32, 0xE7, 0x54, 0x58, 0x90,
    0x61, 0x22, 0xA3, 0x83, 0x02, 0xD6, 0x8A, 0x7F, 0x33, 0xF8, 0x81, 0xFF, 0x9A, 0x10, 0x3F, 0xE0,
    0x14, 0x59, 0x80, 0x40, 0x01, 0x24, 0x00, 0x02, 0x84, 0xCB, 0xC0, 0xF1, 0xDF, 0x67, 0x80, 0xAA,
    0x33, 0xD1, 0x6E, 0xBC, 0xB3, 0x2D, 0x10, 0x80, 0x83, 0x52, 0xE0, 0x7E, 0x40, 0x16, 0x26, 0xD5,
    0x18, 0xA4, 0xF2, 0x56, 0x5A, 0xC4, 0x75, 0x31, 0xD6, 0x36, 0x84, 0x0C, 0x74, 0xCF, 0xE1, 0x06,
    0x86, 0x87, 0xD1, 0x01, 0xC9, 0xAD, 0x3F, 0xEA, 0x35, 0xD2, 0xC9, 0x20, 0x41, 0x4C, 0x09, 0xEF,
    0x85, 0x6A, 0x02, 0x6D, 0x10, 0x83, 0x23, 0xED, 0xC8, 0xD2, 0xEB, 0xAC, 0x37, 0x46, 0x4F, 0x9C,
    0x33, 0xCF, 0x1E, 0xDC, 0xC8, 0x42, 0x5B, 0x62,
    0x74
};

typedef struct {
    unsigned long millis;
    size_t len;
    uint8_t data[MAX_PACKET_LEN];
} packet_t;

static packet_t queue[QUEUE_SIZE];
static std::atomic < int >writeIndex = 0;
static std::atomic < int >readIndex = 0;
static std::atomic < uint32_t > droppedPackets = 0;

static void printhex(const char *title, const uint8_t *buf, size_t len, int rowsize = 16)
{
    printf("%s", title);
    for (size_t i = 0; i < len; i++) {
        if ((rowsize > 0) && (i % rowsize) == 0) {
            printf("\n%04X:", i);
        }
        printf(" %02X", buf[i]);
    }
    printf("\n");
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

    // toggle the led
    rgb ^= 0x001100;

    // enqueue, indicate error on failure
    if (!enqueue(millis(), len, pkt->payload)) {
        droppedPackets.fetch_add(1, std::memory_order_relaxed);
        rgb ^= 0x110000;
    }
}

static int do_reboot(int argc, char *argv[])
{
    ESP.restart();
    return 0;
}

static int do_fake(int argc, char *argv[])
{
    return enqueue(millis(), sizeof(CAM_MSG), CAM_MSG) ? 0 : -1;
}

static int do_send(int argc, char *argv[])
{
    const char *text = (argc > 1) ? argv[1] : "Hello world!";
    return enqueue(millis(), strlen(text), (const uint8_t *) text) ? 0 : -1;
}

static const cmd_t commands[] = {
    { "reboot", do_reboot, "Reboot" },
    { "fake", do_fake, "Send a fake packet" },
    { "send", do_send, "<message> Send a message over serial" },
    { NULL, NULL, NULL }
};

void setup(void)
{
    pinMode(LED_BUILTIN, OUTPUT);

    // the hardware usb serial needs .begin()
    Serial.begin(115200);
    Serial.println("Starting ITS G-5 sniffer...");

    Serial0.begin(115200);
    Serial0.println("Hello from the alternate serial port!");

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

    rgb = 0;
    led.setPixelColor(0, rgb);
    led.show();
}

void loop(void)
{
    packet_t pkt;
    while (dequeue(&pkt)) {
        printhex("Packet", (uint8_t *) pkt.data, pkt.len);
    }

    static uint32_t last = -1;
    uint32_t sec = millis() / 1000;
    if (sec != last) {
        last = sec;
        rgb ^= 0x000011;
    }

    static uint32_t last_rgb = 0;
    if (last_rgb != rgb) {
        last_rgb = rgb;
        led.setPixelColor(0, rgb);
        led.show();
    }
    // command line processing
    shell.process(">", commands);
}
