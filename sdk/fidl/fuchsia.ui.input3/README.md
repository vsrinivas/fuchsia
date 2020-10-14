# Physical keyboard events

Fuchsia's Keyboard service provides a mechanism for delivering physical keyboard
events to all interested clients.

## Rendered Docs

* [FIDL](https://fuchsia.dev/reference/fidl/fuchsia.ui.input3)
* [Rust](https://fuchsia-docs.firebaseapp.com/rust/fidl_fuchsia_ui_input3/index.html)

## Overview

Key events are delivered only to the components subscribed via `Keyboard` FIDL
protocol. Components interested in the service, should list FIDL service in the
components manifest, and it will be injected when the component is started.

Typical use-cases:

* TAB navigation between form fields.
* Arrow keys navigation in menus, lists.
* Pressing “f” to open a **F**ile from a menu.
* Closing popups on ESC.
* WASD navigation in games.

Media buttons, shortcuts, and text entry (IME) related events are delivered
separately via specialized interfaces.

Key events are only delivered to components in the Scenic focus chain.
Only focused components receive key events.

Key events are delivered in root to leaf order - i.e. parent components first.

Parent components have the ability to block further event propagation via
`KeyboardListener.OnKeyEvent` by returning `KeyEventStatus.Handled` to prevent
event’s propagation.

Clients are notified of keys being pressed or released via `Pressed` and
`Released` event types.

Clients are notified of keys pressed or released while client wasn't available
(e.g. not focused, or not started) via `Sync` and `Cancel` event types. Those
events will delivered for relevant events only, e.g. keys pressed and held while
client was not available.

## Example

```rust
use fidl_fuchsia_ui_input3 as ui_input;

let keyboard = connect_to_service::<ui_input::KeyboardMarker>()
    .context("Failed to connect to Keyboard service")?;

let (listener_client_end, mut listener_stream) =
    create_request_stream::<ui_input::KeyboardListenerMarker>()?;

keyboard.add_listener(view_ref, listener_client_end).await.expect("add_listener");

match listener_stream.next().await {
    Some(Ok(ui_input::KeyboardListenerRequest::OnKeyEvent { event, responder, .. })) => {
        assert_eq!(event.key, Some(fuchsia_input::Key::A));
        responder.send(ui_input::Status::Handled).expect("response from key listener")
    },
}
```
