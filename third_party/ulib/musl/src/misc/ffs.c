#include "atomic.h"
#include <strings.h>

int ffs(int i) {
    return i ? a_ctz_l(i) + 1 : 0;
}
