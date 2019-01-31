#include <signal.h>

int sigtimedwait(const sigset_t* restrict mask, siginfo_t* restrict si,
                 const struct timespec* restrict timeout) {
    return 0;
}
