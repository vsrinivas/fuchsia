# Magenta Signals

## Introduction

A signal is a single bit of information that waitable magenta kernel objects expose to
applications.  Each object can expose one or more signals; some are generic and some
are specific to the type of object.

For example, the signal *MX_CHANNEL_READABLE* indicates "this channel endpoint has
messages to read", and **MX_PROCESS_TERMINATED** indicates "this process stopped running."

The signals for an object are stored in a uint32 bitmask, and their values (which are object-specific) are defined in the header
[`magenta/types.h`](../system/public/magenta/types.h).  The typedef `mx_signals_t`
is used to refer to signal bitmasks in syscalls and other APIs.

Most objects are waitable.  Ports are an example of a non-waitable object.
To determine if an object is waitable, call [object_get_info](syscalls/object_get_info.md)
with **MX_INFO_HANDLE_BASIC** topic and test for **MX_OBJ_PROP_WAITABLE**.

## State, State Changes and their Terminology

A signal is said to be **Active** when its bit is 1 and **Inactive** when its bit is 0.

A signal is said to be **Asserted** when it is made **Active** in response to an event
(even if it was already **Active**), and is said to be **Deasserted** when it is made
**Inactive* in response to an event (even if it was already **Inactive**).

For example:  When a message is written into a Channel endpoint, the *MX_CHANNEL_READABLE*
signal of the opposing endpoint is **asserted** (which causes that signal to become **active**,
if it were not already active).  When the last message in a Channel endpoint's
queue is read from that endpoint, the *MX_CHANNEL_READABLE* signal of that endpoint is
**deasserted** (which causes that signal to become **inactive**)

## Observing Signals

The syscalls **mx_object_wait_one**(), **mx_object_wait_many**(), and **mx_object_wait_async**() (in combination with a Port), can be used to wait for specified signals on one or more objects.

## Common Signals

### MX_SIGNAL_LAST_HANDLE

This signal is asserted when there is only a single handle referencing the object.  Thus it
is always active on newly created objects.

This signal is deasserted when there are more than one handles referencing the object, for
example, if the initial handle were duplicated.  The process observing this event is, at
the moment of observation, the last handle holder. This signal can be used to reap local
resources associated with an object whose handle was duplicated and shared with another
process.

### MX_SIGNAL_HANDLE_CLOSED

This synthetic signal only exists in the results of [object_wait_one](syscalls/object_wait_one.md)
or [object_wait_many](syscalls/object_wait_many.md) and indicates that a handle that was
being waited upon has been been closed causing the wait operation to be aborted.

This signal can only be obtained as a result of the above two wait calls when the wait itself
returns with **MX_ERR_CANCELED**.

## User Signals

There are eight User Signals (**MX_USER_SIGNAL_0** through **MX_USER_SIGNAL_7**) which may
asserted or deasserted using the **mx_object_signal**() and **mx_object_signal_peer**() syscalls,
provided the handle has the appropriate rights (**MX_RIGHT_SIGNAL** or **MX_RIGHT_SIGNAL_PEER**,
respectively).  These User Signals are always initially inactive, and are only modified by
the object signal syscalls.

## See Also

[object_signal](syscalls/object_signal.md),
[object_signal_peer](syscalls/object_signal.md),
[object_wait_async](syscalls/object_wait_async.md),
[object_wait_many](syscalls/object_wait_many.md),
[object_wait_one](syscalls/object_wait_one.md).

