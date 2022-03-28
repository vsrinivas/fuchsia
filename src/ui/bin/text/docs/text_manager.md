# `text_manager` internals

The `text_manager` is a program that serves the text editing needs of Fuchsia
clients.

## Consumer-facing APIs

`text_manager` serves the following discoverable FIDL APIs, which are offered
to the consumers of the text entry APIs.

* [`fuchsia.ui.input/ImeService`][1]: this is the first point of contact for
  a text consumer that requires text editing capabilities.  Clients call this
  API to establish two-way communication between the consumer (text field owner)
  and `text_manager`.

* [`fuchsia.ui.input/ImeVisibilityService`][2]: used to observe the input method
  editor visibility.

  There is some duplication of functionality that should be removed in the
  steady state.  See [`fuchsia.input.virtualkeyboard/Manager`][10].

* [`fuchsia.ui.input3/Keyboard`][7]: used to accept keyboard event listener
  registrations.

* [`fuchsia.ui.input3/KeyEventInjector`][8]: used to receive keyboard events
  from the input pipeline.

* [`fuchsia.ui.keyboard.focus/Controller`][9]: used to accept focus change data,
  primarily intended for getting messages from the input pipeline.

When `ImeService.GetInputMethodEditor` is called, two more connections are
established:

* [`fuchsia.ui.input/InputMethodEditorClient`][3]: served by the *consumer*,
  used by the text manager to communicate the change in the text edit box state
  to the consumer.

* [`fuchsia.ui.input/InputMethodEditor`][4]: served by text manager, used to
  communicate the text input, and client commands.

## A brief tour of the internals

The text manager entry point is [main.rs][5].

All the services that text manager provides are declared in [`main` itself][6].
This is useful to know for CFv2 migration, since the CFv1 `.cmx` files do not
specify the outgoing APIs, and so the CFv2 "capabilities" and "offer" sections
must be reconstructed from the code.

[1]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input/ime_service.fidl;l=8;drc=e3b39f2b57e720770773b857feca4f770ee0619e
[2]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input/ime_service.fidl;l=23;drc=e3b39f2b57e720770773b857feca4f770ee0619e
[3]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input/text_input.fidl?q=%22protocol%20InputMethodEditorClient%20%7B%22&ss=fuchsia%2Ffuchsia
[4]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input/text_input.fidl?q=%22protocol%20InputMethodEditor%20%7B%22&ss=fuchsia%2Ffuchsia
[5]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/bin/text/src/main.rs;l=19;drc=d5c7f0ad7d26fac62c9495cb7024203a8e85d93d
[6]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/bin/text/src/main.rs;l=27;drc=d5c7f0ad7d26fac62c9495cb7024203a8e85d93d
[7]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input3/keyboard.fidl;l=11;drc=e3b39f2b57e720770773b857feca4f770ee0619e
[8]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.input3/keyboard.fidl?q=%22protocol%20KeyEventInjector%20%7B%22
[9]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.keyboard.focus/focus.fidl;l=21;drc=28aebcdb857d528c73f78b2c1b3ed731fd13bc1d
[10]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.input.virtualkeyboard/virtual_keyboard.fidl;l=184;drc=dbbfb34baa9a0f1d713d945de2c9978b26eaa312
