# Scenic Input Injection Tool

This directory contains input, a simple tool to inject input events into
Scenic.

## usage

    input [<options>] text|keyevent|tap|swipe <args>
      input text <text>
      input keyevent <hid_usage (int)>
      input tap <x> <y>
      input swipe <x0> <y0> <x1> <y1>

## global options

### `--duration=<ms>`

the duration of the event, in milliseconds (default: 0)

## commands

### `text`

Text is injected by translating a string into keystrokes using a QWERTY keymap.
This facility is intended for end-to-end and input testing purposes only.

Only printable ASCII characters are mapped. Tab, newline, and other control
characters are not supported, and `keyevent` should be used instead.

The events simulated consist of a series of keyboard reports, ending in a
report with no keys. The number of reports is near the lower bound of reports
needed to produce the requested string, minimizing pauses and shift-state
transitions.

The `--duration` is divided between the reports. Care should be taken not to
provide so long a duration that key repeat kicks in.

Note: when using through `fx shell` with quotes, you may need to surround the
invocation in strong quotes, e.g.:

    fx shell 'input text "Hello, world!"'

#### future work

This facility will eventually use IME for general text input, removing the
above limitations. Keystroke mapping will remain as a specialized utility. At
that point, the Dvorak keymap will also be supported.

### `keyevent`

This command simulates a single key down + up sequence. The argument is a
decimal HID usage code, prior to any remapping the IME may do.

Common usage codes:

key       | code
----------|-----
enter     | 40
escape    | 41
backspace | 42
tab       | 43

### `tap`/`swipe`

These commands simulate touch events.

By default, the x and y coordinates are in the range 0 to 1000 and will be
proportionally transformed to the current display, but you can specify a
virtual range for the input with the `--width` and `--height` options.

The events simulated consist of a series of touch reports starting with the
initial touch, followed by updated touch coordinates for `swipe`, ending in a
report with no touches. The time between the first and last report is
`--duration`.

`swipe` coordinates are interpreted linearly, but they are all sent at the same
time, after `--duration`, immediately preceding the last report.

#### options:

##### `--width=<w>`

the width of the display (default: 1000)

##### `--height=<h>`

the height of the display (default: 1000)

#### swipe options:

##### `--move_event_count=<count>`

the number of move events to send in between the up and down events of the
swipe (default: 100)
