#include <signal.h>

#include <poll.h>
#include <stddef.h>

int pause(void) {
    // TODO(kulakowski) Signal handling.
    return poll(NULL, 0, 0);
}
