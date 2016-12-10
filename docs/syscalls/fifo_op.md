# mx_fifo_op

## NAME

fifo_op - perform an operation on a fifo

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_fifo_op(mx_handle_t handle, uint32_t op, uint64_t val,
                       mx_fifo_state_t* out);
```

## DESCRIPTION

**fifo_op**() performs the specified operation on a fifo. The *op* parameter
indicates the operation to perform.

*val* is an optional value, interpreted by the operation.

*out* is an optional pointer to return the state of the fifo after performing
the operation.

After any operation that advances a pointer, the fifo signals will be updated to
reflect the state of the fifo.

## OPERATIONS

**MX_FIFO_READ_STATE** Reads the fifo state and returns it in *out* (which must
not be NULL for this operation).

**MX_FIFO_ADVANCE_HEAD** Advances the head pointer of the fifo by *val*.
Requires the *MX_RIGHT_FIFO_PRODUCER* right. If *out* is not NULL, the state of
the fifo after advancing the head is returned.

**MX_FIFO_ADVANCE_TAIL** Advances the tail pointer of the fifo by *val*.
Requires the *MX_RIGHT_FIFO_CONSUMER* right. If *out* is not NULL, the state of
the fifo after advancing the tail is returned.

## RETURN VALUE

**fifo_op**() returns NO_ERROR on success. On failure, an error value is
returned.

## ERRORS

**ERR_INVALID_ARGS** *op* is not recognized.

**ERR_INVALID_ARGS** *op* is **MX_FIFO_READ_STATE**, and *out* is NULL.

**ERR_OUT_OF_RANGE** *op* is **MX_FIFO_ADVANCE_HEAD** or
**MX_FIFO_ADVANCE_TAIL**, and advancing the pointer would exceed the size of the
fifo or move tail past head.

## SEE ALSO

[fifo_create](fifo_create.md)
