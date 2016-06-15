#if defined(__x86_64__)
#include "../../arch/x86_64/bits/mman.h"
#elif defined(__aarch64__)
#include "../../arch/generic/bits/mman.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/mman.h"
#else
#error Unsupported architecture!
#endif
