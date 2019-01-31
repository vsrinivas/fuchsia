#if defined(__x86_64__)
#include "x86_64/ipc.h"
#elif defined(__aarch64__)
#include "aarch64/ipc.h"
#else
#error Unsupported architecture!
#endif
