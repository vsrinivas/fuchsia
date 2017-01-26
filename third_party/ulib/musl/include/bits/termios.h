#if defined(__x86_64__)
#include "generic/termios.h"
#elif defined(__aarch64__)
#include "generic/termios.h"
#else
#error Unsupported architecture!
#endif
