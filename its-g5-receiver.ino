#include <Arduino.h>

#include <atomic>
#include <cstring>

#include "esp_wifi.h"
#include "esp_wifi_secret.h"

#include "Adafruit_NeoPixel.h"

#define RGB_LED_PIN 27

static Adafruit_NeoPixel led(1, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t rgb = 0;

static const int CHANNEL = 5900;
static const size_t QUEUE_SIZE = 8;
static const int MAX_PACKET_LEN = 2304;

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

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);

    // the hardware usb serial needs .begin()
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

    rgb = 0;
    led.setPixelColor(0, rgb);
    led.show();
}

void loop()
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
}
