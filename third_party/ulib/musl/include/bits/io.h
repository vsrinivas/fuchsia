#if defined(__x86_64__)
#include "x86_64/io.h"
#elif defined(__aarch64__)
#include "generic/io.h"
#else
#error Unsupported architecture!
#endif
