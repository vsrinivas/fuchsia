#if defined(__x86_64__)
#include "x86_64/socket.h"
#elif defined(__aarch64__)
#include "aarch64/socket.h"
#elif defined(__arm__)
#include "generic/socket.h"
#else
#error Unsupported architecture!
#endif
