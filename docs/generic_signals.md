# Magenta Generic Signals

A signal is a single bit information that magenta kernel objects expose to applications. Each
object can expose one or more signals; some are generic and some are specific to the object.

Only waitable objects have signals. Below are the signals that apply to every waitable object.

To know if an object is waitable, call [object_get_info](syscalls/object_get_info.md) with
**MX_INFO_HANDLE_BASIC** topic and test for **MX_OBJ_PROP_WAITABLE**.

## MX_SIGNAL_HANDLE_CLOSED

The handle associated with a pending [object_wait_one](syscalls/object_wait_one.md) or a
[object_wait_many](syscalls/object_wait_many.md) call has been closed causing the wait operation
to be aborted.

This signal can only be obtained as a result of the above two wait calls when the wait itself
returns with **ERR_CANCELED**.

## MX_SIGNAL_LAST_HANDLE

The object handle count reached 1. The process observing this event was, at the moment of
observation, the last handle holder. This signal can be used to reap local resources associated
with an object whose handle was duplicated and sent over a channel.

A newly created object first handle has this signal asserted. If this handle is duplicated
then the signal de-asserts.

## MX_USER_SIGNAL_0 to MX_USER_SIGNAL_7

All waitable objects have 8 user-avaliable signals which can be asserted and de-asserted via
[object_signal](syscalls/object_signal.md) and for objects that have a peer such as channels,
sockets and event pairs, the peer object user signals can be mutated via
[object_signal_peer](syscalls/object_signal.md).

## SEE ALSO

[handle_close](syscalls/handle_close.md), [handle_duplicate](syscalls/handle_duplicate.md),
[handle_replace](syscalls/handle_replace.md)
