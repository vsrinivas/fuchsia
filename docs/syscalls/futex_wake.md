# zx_futex_wake

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

futex_wake - Wake some number of threads waiting on a futex, optionally transferring ownership to the thread which was woken in the process.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_futex_wake(const zx_futex_t* value_ptr, uint32_t wake_count);
```

## DESCRIPTION

Waking a futex causes `wake_count` threads waiting on the `value_ptr`
futex to be woken up.

Waking up zero threads is not an error condition.  Passing in an unallocated
address for `value_ptr` is not an error condition.

## OWNERSHIP

A successful call to **futex_wake**() results in the owner of the futex being
set to nothing, regardless of the wake count.  In order to transfer ownership of
a futex, use the **futex_wake_single_owner**() variant instead.
**futex_wake_single_owner**() will attempt to wake exactly one thread from the
futex wait queue.  If there is at least one thread to wake, the owner of the
futex will be set to the thread which was woken.  Otherwise, the futex will have
no owner.

See *Ownership and Priority Inheritance* in [futex](../objects/futex.md) for
details.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

None.

## RETURN VALUE

**futex_wake**() returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_INVALID_ARGS**  `value_ptr` is not aligned.

## SEE ALSO

[futex objects](../objects/futex.md),
[futex_requeue](futex_requeue.md),
[futex_wait](futex_wait.md).
[futex_wake_single_owner](futex_wake_single_owner.md).
