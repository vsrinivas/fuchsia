#if defined(__x86_64__)
#include "x86_64/io.h"
#elif defined(__aarch64__)
#include "aarch64/io.h"
#else
#error Unsupported architecture!
#endif
