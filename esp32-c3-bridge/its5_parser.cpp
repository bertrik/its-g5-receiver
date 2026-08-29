#include <stdio.h>  // for printf
#include <string.h> // for memcpy

#include "its5_parser.h"

static uint8_t buffer[ITS5_HEADER_LEN + ITS5_MAX_PAYLOAD];
static uint16_t frame_len = 0;
static int position = 0;

bool its5_parse(uint8_t b, its5_frame_t *frame)
{
    switch (position) {
    case 0:
        if (b != 'I') {
            return false;
        }
        break;
    case 1:
        if (b != 'T') {
            position = 0;
            return its5_parse(b, frame);
        }
        break;
    case 2:
        if (b != 'S') {
            position = 0;
            return its5_parse(b, frame);
        }
        break;
    case 3:
        if (b != '5') {
            position = 0;
            return its5_parse(b, frame);
        }
        printf("ITS5 header detected\n");
        break;
    case 13:
        // second byte of length field
        frame_len = buffer[12] + (b << 8);
        printf("frame_len=%d\n", frame_len);
        if ((ITS5_HEADER_LEN + frame_len) > sizeof(buffer)) {
            // won't fit, reset and try again
            position = 0;
            return its5_parse(b, frame);
        }
        break;
    default:
        break;
    }

    // store bytes while there is room in the buffer
    if (position < sizeof(buffer)) {
        buffer[position++] = b;
    }

    // full frame?
    if (position == (ITS5_HEADER_LEN + frame_len)) {
        // end of packet, decode into frame
        frame->sec = buffer[4] + (buffer[5] << 8) + (buffer[6] << 16) + (buffer[7] << 24);
        frame->usec = buffer[8] + (buffer[9] << 8) + (buffer[10] << 16) + (buffer[11] << 24);
        frame->len = frame_len;
        memcpy(frame->payload, &buffer[ITS5_HEADER_LEN], frame_len);
        // reset index and indicate success
        position = 0;
        return true;
    }
    return false;
}
