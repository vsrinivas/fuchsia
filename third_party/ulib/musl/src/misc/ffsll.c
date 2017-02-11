#include <strings.h>

int ffsll(long long i) {
    return __builtin_ctzll(i);
}
