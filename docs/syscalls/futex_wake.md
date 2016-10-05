# mx_futex_wake

## NAME

futex_wake - Wake some number of threads waiting on a futex.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_futex_wake(mx_futex_t* value_ptr, uint32_t wake_count);
```

## DESCRIPTION

Waking a futex causes `wake_count` threads waiting on the `value_ptr`
futex to be woken up.

## RETURN VALUE

**futex_wake**() returns **NO_ERROR**.

## ERRORS

**futex_wake**() always succeeds. Waking up zero threads is not an
error condition.

## SEE ALSO

[futex_wait](futex_wait.md)
[futex_requeue](futex_requeue.md)
