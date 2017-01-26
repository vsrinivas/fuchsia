#if defined(__x86_64__)
#include "generic/resource.h"
#elif defined(__aarch64__)
#include "generic/resource.h"
#else
#error Unsupported architecture!
#endif
