#if defined(__x86_64__)
#include "../../arch/generic/bits/sem.h"
#elif defined(__aarch64__)
#include "../../arch/aarch64/bits/sem.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/sem.h"
#else
#error Unsupported architecture!
#endif
