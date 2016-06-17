#if defined(__x86_64__)
#include "x86_64/endian.h"
#elif defined(__aarch64__)
#include "aarch64/endian.h"
#elif defined(__arm__)
#include "arm/endian.h"
#else
#error Unsupported architecture!
#endif
