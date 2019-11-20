# zx_port_queue

## NAME

<!-- Updated by update-docs-from-fidl, do not edit. -->

Queue a packet to a port.

## SYNOPSIS

<!-- Updated by update-docs-from-fidl, do not edit. -->

```c
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

zx_status_t zx_port_queue(zx_handle_t handle, const zx_port_packet_t* packet);
```

## DESCRIPTION

`zx_port_queue()` queues a *packet* to the port specified
by *handle*.

```
typedef struct zx_port_packet {
    uint64_t key;
    uint32_t type;
    zx_status_t status;
    union {
        zx_packet_user_t user;
        zx_packet_signal_t signal;
    };
} zx_port_packet_t;

```

In *packet* *type* should be **ZX_PKT_TYPE_USER** and only the **user**
union element is considered valid:

```
typedef union zx_packet_user {
    uint64_t u64[4];
    uint32_t u32[8];
    uint16_t u16[16];
    uint8_t   c8[32];
} zx_packet_user_t;

```

## RIGHTS

<!-- Updated by update-docs-from-fidl, do not edit. -->

*handle* must be of type **ZX_OBJ_TYPE_PORT** and have **ZX_RIGHT_WRITE**.

## RETURN VALUE

`zx_port_queue()` returns **ZX_OK** on successful queue of a packet.

## ERRORS

**ZX_ERR_BAD_HANDLE** *handle* isn't a valid handle

**ZX_ERR_INVALID_ARGS** *packet* is an invalid pointer.

**ZX_ERR_WRONG_TYPE** *handle* is not a port handle.

**ZX_ERR_ACCESS_DENIED** *handle* does not have **ZX_RIGHT_WRITE**.

**ZX_ERR_SHOULD_WAIT** the port has too many pending packets. Once a thread
has drained some packets a new `zx_port_queue()` call will likely succeed.

## NOTES

The queue is drained by calling [`zx_port_wait()`].


## SEE ALSO

 - [`zx_port_create()`]
 - [`zx_port_wait()`]

<!-- References updated by update-docs-from-fidl, do not edit. -->

[`zx_port_create()`]: port_create.md
[`zx_port_wait()`]: port_wait.md
