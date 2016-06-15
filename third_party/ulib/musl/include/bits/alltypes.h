#if defined(__x86_64__)
#include "alltypes-x86_64.h"
#elif defined(__aarch64__)
#include "alltypes-aarch64.h"
#elif defined(__arm__)
#include "alltypes-arm.h"
#else
#error Unsupported architecture!
#endif
