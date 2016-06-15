#if defined(__x86_64__)
#include "../../arch/generic/bits/errno.h"
#elif defined(__aarch64__)
#include "../../arch/generic/bits/errno.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/errno.h"
#else
#error Unsupported architecture!
#endif
