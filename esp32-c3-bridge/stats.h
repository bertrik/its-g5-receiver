#include <ESPAsyncWebServer.h>

typedef struct {
    int uptime;
    int last_received;
    int counts[60];
} stats_t;

void stats_begin(void);
void stats_serve(AsyncWebServer &server, const char *path);
void stats_count(int num);
void stats_update(void);
