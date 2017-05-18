# mx_thread_exit

## NAME

thread_exit - terminate the current running thread

## SYNOPSIS

```
#include <magenta/syscalls.h>

void mx_thread_exit(void);

```

## DESCRIPTION

**thread_exit**() causes the currently running thread to cease
running and exit.

The signal *MX_THREAD_TERMINATED* will be assserted on the thread
object upon exit and may be observed via *object_wait_one*()
or *object_wait_many*() on a handle to the thread.

## RETURN VALUE

**thread_exit**() does not return.

## SEE ALSO

[handle_close](handle_close.md),
[handle_duplicate](handle_duplicate.md),
[object_wait_one](object_wait_one.md),
[object_wait_many](object_wait_many.md),
[thread_create](thread_create.md),
[thread_start](thread_start.md).
