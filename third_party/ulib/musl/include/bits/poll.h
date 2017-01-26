#if defined(__x86_64__)
#include "generic/poll.h"
#elif defined(__aarch64__)
#include "generic/poll.h"
#else
#error Unsupported architecture!
#endif
