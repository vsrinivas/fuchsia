#if defined(__x86_64__)
#include "x86_64/signal.h"
#elif defined(__aarch64__)
#include "aarch64/signal.h"
#else
#error Unsupported architecture!
#endif
