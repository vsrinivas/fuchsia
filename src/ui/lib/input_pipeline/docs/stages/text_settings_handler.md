# input_pipeline > Text settings handler

The handler source code is in [text_settings_handler.rs][tsh].

The task of the text settings handler is to [monitor for keyboard settings
change][kset] per [`fuchsia.settings.Keyboard/Watch`][kw] and to [decorate the
`KeyboardEvent`][kmapdec] with the keymap.

This decoration allows subsequent pipeline handlers to make decisions based on
the keymap.

The currently defined keymaps are listed in
[`fuchsia.input.keymap/KeymapId`][kmapid].

[tsh]: /src/ui/lib/input_pipeline/src/text_settings_handler.rs
[kw]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.settings/keyboard.fidl;l=38;drc=6c09c8d9f154305dbc637072850d4a3310aa161b
[kset]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/text_settings_handler.rs;l=94;drc=d5e41f93794ae5548c4e0f65a2e52bc6490849be
[kmapdec]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/text_settings_handler.rs;l=52;drc=d5e41f93794ae5548c4e0f65a2e52bc6490849be
[kmapid]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.input/keymap.fidl;l=10;drc=80841c0ab133b3b3896aee74511b4d39a2a0d828
