# mx_object_signal, mx_object_signal_peer

## NAME

object_signal - signal an object

object_signal_peer - signal an object's peer


## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_object_signal(mx_handle_t handle, uint32_t clear_mask, uint32_t set_mask);
mx_status_t mx_object_signal_peer(mx_handle_t handle, uint32_t clear_mask, uint32_t set_mask);

```

## DESCRIPTION

**mx_object_signal**() and **mx_object_signal_peer**() set and clear the userspace-accessible
signal bits on an object or on the object's peer, respectively.  A object peer is the opposite
endpoint of a *channel*, *socket*, *fifo*, or *eventpair*.

Most of the 32 signals are reserved for system use and are assigned to per-object functions, like
*MX_CHANNEL_READABLE* or *MX_TASK_TERMINATED*.  8 signals are available for userspace processes
to use as they see fit: *MX_USER_SIGNAL_0* through *MX_USER_SIGNAL_7*.

*Event* objects also allow control over the *MX_EVENT_SIGNALED* bit.

*Eventpair* objects also allow control over the *MX_EPAIR_SIGNALED* bit.

The *clear_mask* is first used to clear any bits indicated, and then the *set_mask*
is used to set any bits indicated.


## RETURN VALUE

**mx_object_signal**() and **mx_object_signal_peer**() return **MX_OK** on success.
In the event of failure, a negative error value is returned.


## ERRORS

**MX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**MX_ERR_ACCESS_DENIED**  *handle* lacks the right **MX_RIGHT_SIGNAL** (for **mx_object_signal**()) or
**MX_RIGHT_SIGNAL_PEER** (for **mx_object_signal_peer**()).

**MX_ERR_INVALID_ARGS**  *clear_mask* or *set_mask* contain bits that are not allowed.

**MX_ERR_NOT_SUPPORTED**  **mx_object_signal_peer**() used on an object lacking a peer.

**MX_ERR_PEER_CLOSED**  **mx_object_signal_peer**() called on an object with a closed peer.

## NOTE

*MX_RIGHT_WRITE* is used to gate access to signal bits.  This will likely change.


## SEE ALSO

[event_create](event_create.md),
[eventpair_create](eventpair_create.md).
