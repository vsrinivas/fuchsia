#if defined(__x86_64__)
#include "../../arch/generic/bits/resource.h"
#elif defined(__aarch64__)
#include "../../arch/generic/bits/resource.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/resource.h"
#else
#error Unsupported architecture!
#endif
