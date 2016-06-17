#if defined(__x86_64__)
#include "x86_64/ioctl.h"
#elif defined(__aarch64__)
#include "aarch64/ioctl.h"
#elif defined(__arm__)
#include "generic/ioctl.h"
#else
#error Unsupported architecture!
#endif
