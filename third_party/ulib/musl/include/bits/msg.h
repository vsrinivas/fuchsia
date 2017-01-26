#if defined(__x86_64__)
#include "x86_64/msg.h"
#elif defined(__aarch64__)
#include "aarch64/msg.h"
#else
#error Unsupported architecture!
#endif
