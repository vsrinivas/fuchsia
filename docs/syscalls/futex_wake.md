# mx_futex_wake

## NAME

futex_wake - Wake some number of threads waiting on a futex.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_futex_wake(const mx_futex_t* value_ptr, uint32_t wake_count);
```

## DESCRIPTION

Waking a futex causes `wake_count` threads waiting on the `value_ptr`
futex to be woken up.

Waking up zero threads is not an error condition.  Passing in an unallocated
address for `value_ptr` is not an error condition.

## RETURN VALUE

**futex_wake**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_INVALID_ARGS**  *value_ptr* is not aligned.

## SEE ALSO

[futex_requeue](futex_requeue.md),
[futex_wait](futex_wait.md).
