#if defined(__x86_64__)
#include "x86_64/reg.h"
#elif defined(__aarch64__)
#include "aarch64/reg.h"
#else
#error Unsupported architecture!
#endif
