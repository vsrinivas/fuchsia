#if defined(__x86_64__)
#include "../../arch/x86_64/bits/io.h"
#elif defined(__aarch64__)
#include "../../arch/generic/bits/io.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/io.h"
#else
#error Unsupported architecture!
#endif
