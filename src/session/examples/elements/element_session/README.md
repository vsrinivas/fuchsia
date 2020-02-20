# `element_session`

Reviewed on: 2020-02-04

`element_session` creates a sample session that exposes the [`ElementManager`](//sdk/fidl/fuchsia.session/element_manager.fidl) and `ElementPing` services. It then listens for messages on these two services.

## `ElementManager` Service

`element_session` listens for a `ProposeElement` message from the `ElementManager` service. Once received, the `element_session` generates a name for the proposed element and tells the `ElementManager` to add the element to the component tree.

## `ElementPing` Service

`element_session` listens for a `Ping` message from the ElementPing Service and logs that it was received.
