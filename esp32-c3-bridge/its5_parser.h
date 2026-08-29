#include <stdbool.h>
#include <stdint.h>

#define ITS5_HEADER_LEN 14
#define ITS5_MAX_PAYLOAD 1500

typedef struct {
    uint32_t sec;
    uint32_t usec;
    uint16_t len;
    uint8_t payload[ITS5_MAX_PAYLOAD];
} its5_frame_t;

// parse a single byte of ITS5 data, returns true if a complete frame was parsed
bool its5_parse(uint8_t c, its5_frame_t *frame);
