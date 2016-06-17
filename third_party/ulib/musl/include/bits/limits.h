#if defined(__x86_64__)
#include "x86_64/limits.h"
#elif defined(__aarch64__)
#include "aarch64/limits.h"
#elif defined(__arm__)
#include "arm/limits.h"
#else
#error Unsupported architecture!
#endif
