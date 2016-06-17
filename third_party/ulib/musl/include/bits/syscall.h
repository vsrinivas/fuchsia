#if defined(__x86_64__)
#include "x86_64/syscall.h"
#elif defined(__aarch64__)
#include "aarch64/syscall.h"
#elif defined(__arm__)
#include "arm/syscall.h"
#else
#error Unsupported architecture!
#endif
