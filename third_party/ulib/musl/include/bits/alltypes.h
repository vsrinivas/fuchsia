#if defined(__x86_64__)
#include "x86_64/alltypes.h"
#elif defined(__aarch64__)
#include "aarch64/alltypes.h"
#elif defined(__arm__)
#include "arm/alltypes.h"
#else
#error Unsupported architecture!
#endif
