# zx_process_exit

## NAME

process_exit - Exits the currently running process.

## SYNOPSIS

```
#include <zircon/syscalls.h>

void zx_process_exit(int ret_code);

```

## DESCRIPTION

The **zx_process_exit** call ends the calling process with the given
return code. The return code of a process can be queried via the
**ZX_INFO_PROCESS** request to **zx_object_get_info**.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_process_exit** does not return.

## ERRORS

**zx_process_exit** cannot fail.

## SEE ALSO

[object_get_info](object_get_info.md),
[process_create](process_create.md).
