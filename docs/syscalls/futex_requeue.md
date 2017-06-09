# mx_futex_requeue

## NAME

futex_requeue - Wake some number of threads waiting on a futex, and
move more waiters to another wait queue.

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_futex_requeue(mx_futex_t* value_ptr, uint32_t wake_count,
                             int current_value, mx_futex_t* requeue_ptr,
                             uint32_t requeue_count);
```

## DESCRIPTION

Requeuing is a generalization of waking. First, the kernel verifies
that the value in wake_count matches the value of the futex at
`value_ptr`, and if not reports *MX_ERR_ALREADY_BOUND*. After waking `wake_count`
threads, `requeue_count` threads are moved from the original futex's
wait queue to the wait queue corresponding to `requeue_ptr`, another
futex.

This requeueing behavior may be used to avoid thundering herds on wake.

## RETURN VALUE

**futex_requeue**() returns **MX_OK** on success.

## ERRORS

**MX_ERR_INVALID_ARGS**  *value_ptr* isn't a valid userspace pointer, or
*value_ptr* is the same futex as *requeue_ptr*, or
*value_ptr* or *requeue_ptr* is not aligned, or
*requeue_ptr* is NULL but *requeue_count* is positive.

**MX_ERR_BAD_STATE**  *current_value* does not match the value at *value_ptr*.

## SEE ALSO

[futex_wait](futex_wait.md),
[futex_wake](futex_wake.md).
