# mx_process_exit

## NAME

process_exit - Exits the currently running process.

## SYNOPSIS

```
#include <magenta/syscalls.h>

void mx_process_exit(int ret_code);

```

## DESCRIPTION

The **mx_process_exit** call ends the calling process with the given
return code. The return code of a process can be queried via the
**MX_INFO_PROCESS** request to **mx_object_get_info**.

## RETURN VALUE

**mx_process_exit** does not return.

## ERRORS

**mx_process_exit** cannot fail.

## SEE ALSO

[object_get_info](object_get_info.md),
[process_create](process_create.md).
