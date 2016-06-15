#if defined(__x86_64__)
#include "../../arch/generic/bits/poll.h"
#elif defined(__aarch64__)
#include "../../arch/generic/bits/poll.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/poll.h"
#else
#error Unsupported architecture!
#endif
