#include <signal.h>

#include <poll.h>
#include <stddef.h>

int pause(void) {
    return poll(NULL, 0, 0);
}
