#if defined(__x86_64__)
#include "generic/errno.h"
#elif defined(__aarch64__)
#include "generic/errno.h"
#else
#error Unsupported architecture!
#endif
