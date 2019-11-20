# zx_futex_wake_single_owner

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Wake some number of threads waiting on a futex, optionally transferring ownership to the thread which was woken in the process.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_futex_wake_single_owner(const zx_futex_t* value_ptr);
```

## DESCRIPTION

See [`zx_futex_wake()`] for a full description.

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

None.

## RETURN VALUE

`zx_futex_wake_single_owner()` returns **ZX_OK** on success.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *value_ptr* is not aligned.

## SEE ALSO

 - [futex objects](/docs/concepts/objects/futex.md)
 - [`zx_futex_requeue()`]
 - [`zx_futex_wait()`]
 - [`zx_futex_wake()`]

<!-- References updated by update-docs-from-fidl, do not edit. -->

[`zx_futex_requeue()`]: futex_requeue.md
[`zx_futex_wait()`]: futex_wait.md
[`zx_futex_wake()`]: futex_wake.md
