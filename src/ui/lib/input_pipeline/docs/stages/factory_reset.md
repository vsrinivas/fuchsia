# input_pipeline > Factory reset handler

The factory reset handler reads input events sent from `InputDeviceType::ConsumerControls` devices
containing specific button presses. Because the driver can process special button combinations into
a `FactoryReset` signal, the handler only listens for that specific event type to determine whether
the device should start the factory reset process.

For the purposes of this document, the factory reset signal will be referred to as a "button",
though in practice it can be a specific combination of buttons for a given product.

## Factory reset states

The handler manages five possible factory reset states in accordance to the button press events
received and duration held.

### `Disallowed`

**Factory reset button state:** n/a

Factory reset of the device is not allowed. This can be set via the
`fuchsia recovery.policy.DeviceRequest` FIDL protocol. By default, the handler will allow factory
reset unless told otherwise.

This is used to keep public devices from being reset, such as when being used in kiosk mode.

**Transitions**

- `Disallowed` → `Idle`

### `Idle`

**Factory reset button state:** not pressed

No factory reset is underway. This is the default state of the handler when factory resets are
allowed but the device is not currently being reset.

**Transitions**

- `Idle` → `Disallowed`
- `Idle` → `ButtonCountdown`

### `ButtonCountdown { button_deadline: Time }`

**Factory reset button state:** pressed for a duration where `duration <= button_deadline`

A factory reset signal has been detected. The purpose of the `button_deadline` is to determine
an intention to perform factory reset by the user. If the button is released before
`button_deadline` elapses, then the factory reset does not occur. Otherwise, the factory reset
countdown begins on device.

**Transitions**

- `ButtonCountdown` → `Disallowed`
- `ButtonCountdown` → `Idle`
- `ButtonCountdown` → `ResetCountdown`

### `ResetCountdown { reset_deadline: Time }`

**Factory reset button state:** pressed for duration where
`duration > button_deadline` and `duration <= reset_deadline`

The button countdown has completed indicating that this was a purposeful action so a reset
countdown is started to give the user a chance to cancel the factory reset.

A factory reset countdown begins, at the end of which the actual factory reset will occur to
the device. There may be some accompanying UI to notify the user of the countdown at this stage.

If the factory reset button is released before `reset_deadline` elapses, then the factory reset
is cancelled. Otherwise, the factory reset process advances to an irreversible state.

**Transitions**

- `ResetCountdown` → `Disallowed`
- `ResetCountdown` → `Idle`
- `ResetCountdown` → `Resetting`

### `Resetting`

**Factory reset button state:** pressed for duration where `duration > reset_deadline`, after
which point the button can be in any state

Once the device is in this state a factory reset is imminent and can no longer be cancelled.
