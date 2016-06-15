#if defined(__x86_64__)
#include "../../arch/x86_64/bits/ipc.h"
#elif defined(__aarch64__)
#include "../../arch/aarch64/bits/ipc.h"
#elif defined(__arm__)
#include "../../arch/generic/bits/ipc.h"
#else
#error Unsupported architecture!
#endif
