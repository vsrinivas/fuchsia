#include <stdlib.h>
#include <unistd.h>

_Noreturn void _exit(int status) {
    _Exit(status);
}
