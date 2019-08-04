#include <poll.h>
#include <signal.h>
#include <stddef.h>

int pause(void) { return poll(NULL, 0, 0); }
