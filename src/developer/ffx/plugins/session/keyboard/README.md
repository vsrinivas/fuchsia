## Keyboard plugin for `ffx`

The keyboard plugin for `ffx` is an experimental plugin used in the development of
Fuchsia's keyboard subsystem.

## Prerequisites

Since the plugin is experimental, it must first be enabled like so:

```
ffx config set experimental_keyboard true
```

## Use

For the time being, the plugin has only one thing you can set: the keymap
identifier.

Set it like this:

```
ffx session keyboard --keymap US_QWERTY
```

See `ffx help keyboard` for more details.



