# zx_object_signal, zx_object_signal_peer

## NAME

object_signal - signal an object

object_signal_peer - signal an object's peer

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_object_signal(zx_handle_t handle, uint32_t clear_mask, uint32_t set_mask);
zx_status_t zx_object_signal_peer(zx_handle_t handle, uint32_t clear_mask, uint32_t set_mask);

```

## DESCRIPTION

**zx_object_signal**() and **zx_object_signal_peer**() assert and deassert the
userspace-accessible signal bits on an object or on the object's peer,
respectively. A object peer is the opposite endpoint of a *channel*, *socket*,
*fifo*, or *eventpair*.

Most of the 32 signals are reserved for system use and are assigned to
per-object functions, like *ZX_CHANNEL_READABLE* or *ZX_TASK_TERMINATED*. There
are 8 signal bits available for userspace processes to use as they see fit:
*ZX_USER_SIGNAL_0* through *ZX_USER_SIGNAL_7*.

*Event* objects also allow control over the *ZX_EVENT_SIGNALED* bit.

*Eventpair* objects also allow control over the *ZX_EVENTPAIR_SIGNALED* bit.

The *clear_mask* is first used to clear any bits indicated, and then the
*set_mask* is used to set any bits indicated.

## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**zx_object_signal**() and **zx_object_signal_peer**() return **ZX_OK** on success.
In the event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_ACCESS_DENIED**  *handle* lacks the right **ZX_RIGHT_SIGNAL** (for **zx_object_signal**()) or
**ZX_RIGHT_SIGNAL_PEER** (for **zx_object_signal_peer**()).

**ZX_ERR_INVALID_ARGS**  *clear_mask* or *set_mask* contain bits that are not allowed.

**ZX_ERR_NOT_SUPPORTED**  **zx_object_signal_peer**() used on an object lacking a peer.

**ZX_ERR_PEER_CLOSED**  **zx_object_signal_peer**() called on an object with a closed peer.

## NOTE

*ZX_RIGHT_WRITE* is used to gate access to signal bits.  This will likely change.

## SEE ALSO

[event_create](event_create.md),
[eventpair_create](eventpair_create.md).
