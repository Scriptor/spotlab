#include <libspotify/api.h>
#include "appkey.c"

typedef enum {
    SEARCH,
    PLAY
} cmd_kind;

typedef struct command{
    cmd_kind kind;
    char args[3][256];
} command_t;

typedef struct tok{
    char val[256];
} tok_t;

void finish_cmd();
