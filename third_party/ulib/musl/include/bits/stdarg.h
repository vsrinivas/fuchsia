#if defined(__x86_64__)
#include "generic/stdarg.h"
#elif defined(__aarch64__)
#include "generic/stdarg.h"
#else
#error Unsupported architecture!
#endif
