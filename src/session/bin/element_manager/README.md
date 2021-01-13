# `element_manager`

Reviewed on: 2020-12-11

`element_manager` is a component that exposes the
[`Manager`](//sdk/fidl/fuchsia.element/element_manager.fidl) service,
intended to be composed into a session as a child.

## `Manager` Service

`element_manager` launches all elements proposed to `Manager` in a collection.
