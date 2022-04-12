#include <signal.h>

int __libc_current_sigrtmax(void) { return _NSIG - 1; }
