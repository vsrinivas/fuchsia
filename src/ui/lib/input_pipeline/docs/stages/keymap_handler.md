# input_pipeline > Keymap handler

The handler source code is in [keymap_handler.rs][kh].

The task of the keymap handler is to use the [keymap settings][ks] supplied by
the [text settings handler](text_settings_handler.md) to apply the appropriate
[keymap]. This allows rudimentary international text entry based on key events,
which is require by some consumers, such as those that implement web text APIs
(e.g. Chromium).

The handler [selects a keymap][sk] to apply, and [decorates][dec] the
[`KeyboardEvent`][ke] with [`key_meaning`][km], corresponding to the code point
based on the currently active keymap.

The `key_meaning` is distributed to consumers by way of
[`fuchsia.ui.input3/KeyEvent.key_meaning`][i3km].  The consumers are expected
to:

1. Apply the key effect based on `KeyEvent.key_meaning` if one is set.
2. Else, apply the US QWERTY keymap to [`KeyEvent.key`] if
   `KeyEvent.key_meaning` is not set.

While in theory only (1) is ever needed, (2) must be specified and supported
because of the modularity of the input pipeline. It is unlikely, but
conceivable, that a product would configure the input pipeline to not have any
keymap switching support.

[kh]: /src/ui/lib/input_pipeline/src/keymap_handler.rs
[keymap]: https://en.wikipedia.org/wiki/Keyboard_layout
[sk]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/ui/keymaps/src/lib.rs;l=31;drc=d5e41f93794ae5548c4e0f65a2e52bc6490849be
[ke]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/keyboard_binding.rs;l=36;drc=4a14bb79f879a47b46e15bf67c83fb3a5638835d
[km]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/keyboard_binding.rs;l=57;drc=4a14bb79f879a47b46e15bf67c83fb3a5638835d
[dec]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/keyboard_binding.rs;l=92;drc=4a14bb79f879a47b46e15bf67c83fb3a5638835d
[i3km]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input3/events.fidl;l=132;drc=f4c943310df266ca4fde85b9db374817e67c59a7
[i3k]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input3/events.fidl;l=113;drc=f4c943310df266ca4fde85b9db374817e67c59a7
