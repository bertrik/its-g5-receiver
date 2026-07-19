#include <ESPAsyncWebServer.h>

typedef struct {
    int latest;
    int counts[60];
} stats_t;

void stats_begin(void);
void stats_serve(AsyncWebServer &server, const char *path);
void stats_count(int num);
void stats_update(void);
void stats_get(stats_t *statistics);
