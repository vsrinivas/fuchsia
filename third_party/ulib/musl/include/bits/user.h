#if defined(__x86_64__)
#include "x86_64/user.h"
#elif defined(__aarch64__)
#include "aarch64/user.h"
#else
#error Unsupported architecture!
#endif
