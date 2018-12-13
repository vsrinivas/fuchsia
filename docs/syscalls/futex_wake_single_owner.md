# zx_futex_wake_single_owner

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

futex_wake_single_owner - Wake some number of threads waiting on a futex, optionally transferring ownership to the thread which was woken in the process.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_futex_wake_single_owner(const zx_futex_t* value_ptr);
```

## DESCRIPTION

See [futex_wake](futex_wake.md) for a full description.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

None.

## RETURN VALUE

`zx_futex_wake_single_owner()` returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_INVALID_ARGS**  `value_ptr` is not aligned.

## SEE ALSO

[futex objects](../objects/futex.md),
[futex_requeue](futex_requeue.md),
[futex_wait](futex_wait.md),
[futex_wake](futex_wake.md).
