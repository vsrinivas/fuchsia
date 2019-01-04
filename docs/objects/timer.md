# Timer

## NAME

timer - An object that may be signaled at some point in the future

## SYNOPSIS

A timer is used to wait until a specified point in time has occurred
or the timer has been canceled.

## DESCRIPTION

Like other waitable objects, timers can be waited on via
[`zx_object_wait_one()`](../syscalls/object_wait_one.md),
[`zx_object_wait_many()`](../syscalls/object_wait_many.md), or
[`zx_object_wait_async()`](../syscalls/object_wait_async.md).

A given timer can be used over and over.

Once **ZX_TIMER_SIGNALED** is asserted, it will remain asserted until
the timer is canceled ([timer_cancel]) or reset ([timer_set]).

The typical lifecycle is:

1. `zx_timer_create()`
2. `zx_timer_set()`
3. wait for the timer to be signaled
4. optinally reset and reuse the timer (i.e. goto #2)
5. `zx_handle_close()`

## SYSCALLS

+ [timer_create] - create a timer
+ [timer_set] - set a timer
+ [timer_cancel] - cancel a timer

## SEE ALSO

+ [timer slack](../timer_slack.md)

[timer_create]: ../syscalls/timer_create.md
[timer_set]: ../syscalls/timer_set.md
[timer_cancel]: ../syscalls/timer_cancel.md
