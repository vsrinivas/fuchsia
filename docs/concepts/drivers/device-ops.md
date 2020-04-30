
# The Device Protocol

Please refer to the [header comments][device] for descriptions of the methods.

## Hook ordering guarantees

![Hook ordering guarantees](/docs/images/zircon/ddk/driver-hook-ordering.png)

The hooks that a driver implements will be invoked by other drivers and by the
runtime.  These invocations in some occasions may occur in parallel with
invocations of other or even the same hook.  This section will describe the
ordering properties that you may rely on.

### Terminology

This section uses the terms *unsequenced*, *indeterminately sequenced*, and
*sequenced before* as they are used in the C++ execution model.

### Driver Initialization

The [zx_driver_ops_t][driver] *init* hook will execute completely before any other
hooks for that driver.

### Driver Teardown

The [zx_driver_ops_t][driver] *release* hook will begin execution only after all
devices created by this driver have been released.

### Driver Bind

If tests are enabled, the [zx_driver_ops_t][driver] *bind* hook will begin execution only after the
run_unit_tests hook.

### Device Lifecycle

The device lifecycle begins when some driver successfully invokes **device_add()**.  This may
occur on any thread.  No [zx_device_ops_t][device] hooks will run before the
device's lifecycle has begun or after it has ended.

The device lifecycle ends when the device's *release* hook has begun executing.

The [zx_device_ops_t][device] hooks are unsequenced with respect to each other
unless otherwise specified.

**Note**: This means that any code that occurs after a call to **device_add()**, even in *bind* hooks,
is unsequenced with respect to the end of the created device's lifecycle.

### Device Connection Lifecycle

A device connection lifecycle begins when the [zx_device_ops_t][device] *open* hook begins
executing.  None of the [zx_device_ops_t][device] *read*/*write*/*message*/*close* hooks
will be invoked if the number of alive device connections is 0.

A device connection lifecycle ends when the [zx_device_ops_t][device] *close* hook
begins executing.  Any execution of *read*/*write*/*message* hooks is sequenced before
this.

Since the *read*/*write*/*message* hooks only execute on the devhost's main thread,
they will never be executed concurrently but the processing of outstanding requests from
different connections will be indeterminately sequenced.

### Device Power Management

The [zx_device_ops_t][device] *suspend* hook is sequenced before itself (e.g.
if a request to suspend to D1 happens, and while that is being executed a
request to suspend to D2 happens, the first will finish before the latter
begins).  It is also sequenced before the *resume* hook.

The `set_performance_state` hook is sequenced before itself.
It has no particular ordering with suspend/resume hooks.
After the driver returns from the set_performance_state hook with success,
it is assumed by power manager that the device is operating at the requested
performance state whenever the device is in working state. Since the hook only
executes on the devhost's main thread, multiple requests are not executed
concurrently.
On success, the out_state and the requested_state is same. If the device is in a
working state, the performance state will be changed to requested_state immediately.
If the device is in non-working state, the performance state will be the requested_state
whenever the device transitions to working state.
On failure, the out_state will have the state that the device can go into.

The `configure_autosuspend` hook is sequenced before itself and is used to configure whether
devices can suspend or resume themselves depending on their idleness. The hook is called with
the deepest sleep state the device is expected to be in which is when the device is suspended.
If the entire system is being suspended to a sleep state, the driver should expect `suspend`
hook to be called, even if the auto suspend is configured. It is not supported to selectively
suspend a device when auto suspend is configured.

### Misc Device APIs

The [zx_device_ops_t][device] *get_size* and *get_protocol* hooks are
unsequenced with respect to all hooks (including concurrent invocations of themselves).
The one exception to this is that they are sequenced before the *release* hook.

[device]: /zircon/system/ulib/ddk/include/ddk/device.h
[driver]: /zircon/system/ulib/ddk/include/ddk/driver.h
