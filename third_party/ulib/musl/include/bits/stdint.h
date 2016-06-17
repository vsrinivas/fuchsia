#if defined(__x86_64__)
#include "x86_64/stdint.h"
#elif defined(__aarch64__)
#include "aarch64/stdint.h"
#elif defined(__arm__)
#include "arm/stdint.h"
#else
#error Unsupported architecture!
#endif
