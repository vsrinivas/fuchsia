#if defined(__x86_64__)
#include "../../arch/generic/bits/statfs.h"
#elif defined(__aarch64__)
#include "../../arch/generic/bits/statfs.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/statfs.h"
#else
#error Unsupported architecture!
#endif
