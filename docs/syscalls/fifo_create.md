# mx_fifo_create

## NAME

fifo_create - create a fifo state object

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_fifo_create(uint64_t count, mx_handle_t* out);
```

## DESCRIPTION

**fifo_create**() creates a fifo object that tracks a pair of head/tail
pointers. Initially, these pointers will be zero, indicating an empty fifo.
**fifo_op**() may be called to advance either pointer, subject to the following
conditions:

* tail cannot advance past head
* head - tail cannot exceed the size of the fifo

The values of head and tail wrap at UINT64_MAX, and may need to be masked by the
size of the fifo if the fifo represents an index into another data structure.

The newly-created handle will have the *MX_RIGHT_TRANSFER*,
*MX_RIGHT_DUPLICATE*, *MX_RIGHT_READ*, *MX_RIGHT_FIFO_PRODUCER*, and
*MX_RIGHT_FIFO_CONSUMER* rights. To drop rights, use *MX_FIFO_PRODUCER_RIGHTS*
or *MX_FIFO_CONSUMER_RIGHTS* to pass default producer/consumer rights to
**handle_duplicate**() or **handle_replace**().

The *MX_FIFO_EMPTY*, *MX_FIFO_NOT_EMPTY*, *MX_FIFO_FULL*, and
*MX_FIFO_NOT_FULL* signals are set by the fifo as appropriate when the
head and tail are advanced.

The size of the fifo must be a positive power of 2.

## RETURN VALUE

**fifo_create**() returns NO_ERROR and a valid event handle (via *out*) on
success. On failure, an error value is returned.

## ERRORS

**ERR_INVALID_ARGS** The size of the fifo is not a positive power of 2.

## EXAMPLES

```
mx_handle_t fifo;
if (mx_fifo_create(1 << 5, &fifo) < 0) {
  // error!
}

// send the handle to another thread/process

mx_fifo_state_t state;
mx_fifo_op(fifo, MX_FIFO_ADVANCE_HEAD, 1, &state);
// state.head is now 1, state.tail is 0

// do some more work

// check the fifo to see if the other thread/process has finished
mx_fifo_op(fifo, MX_FIFO_READ_STATE, 0, &state);

```

## SEE ALSO

[fifo_op](fifo_op.md)
