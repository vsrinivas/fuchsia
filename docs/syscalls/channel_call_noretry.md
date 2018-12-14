# zx_channel_call_noretry

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

channel_call_noretry - TODO(ZX-3106)

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```
#include <zircon/syscalls.h>

zx_status_t zx_channel_call_noretry(zx_handle_t handle,
                                    uint32_t options,
                                    zx_time_t deadline,
                                    const zx_channel_call_args_t* args,
                                    uint32_t* actual_bytes,
                                    uint32_t* actual_handles);
```

## DESCRIPTION

TODO(ZX-3106)

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_CHANNEL** and have **ZX_RIGHT_READ** and have **ZX_RIGHT_WRITE**.

All wr_handles of *args* must have **ZX_RIGHT_TRANSFER**.

## RETURN VALUE

TODO(ZX-3106)

## ERRORS

TODO(ZX-3106)

## SEE ALSO


TODO(ZX-3106)
