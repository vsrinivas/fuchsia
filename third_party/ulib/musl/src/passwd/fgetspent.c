#include "pwf.h"

struct spwd* fgetspent(FILE* f) {
    static char* line;
    static struct spwd sp;
    size_t size = 0;
    struct spwd* res = 0;
    if (getline(&line, &size, f) >= 0 && __parsespent(line, &sp) >= 0)
        res = &sp;
    return res;
}
