#include <strings.h>

int ffs(int i) {
    return __builtin_ctzl(i);
}
