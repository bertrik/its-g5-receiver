#include <stdio.h>  // for printf
#include <string.h> // for memcpy

#include "its5_parser.h"

static const uint8_t ITS5_HEADER[4] = {'I', 'T', 'S', '5'};
static uint8_t buffer[ITS5_HEADER_LEN + ITS5_MAX_PAYLOAD];
static uint16_t frame_len = 0;
static int position = 0;

bool its5_parse(uint8_t b, its5_frame_t *frame)
{
    switch (position) {
    case 0:
    case 1:
    case 2:
    case 3:
        // header magic
        if (b != ITS5_HEADER[position]) {
            if (position > 0) {
                position = 0;
                return its5_parse(b, frame);
            }
        }
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
