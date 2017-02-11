#include <strings.h>

int ffsl(long i) {
    return __builtin_ctzl(i);
}
