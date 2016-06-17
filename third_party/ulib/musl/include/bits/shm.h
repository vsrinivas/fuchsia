#if defined(__x86_64__)
#include "x86_64/shm.h"
#elif defined(__aarch64__)
#include "aarch64/shm.h"
#elif defined(__arm__)
#include "generic/shm.h"
#else
#error Unsupported architecture!
#endif
