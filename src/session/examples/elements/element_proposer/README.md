# `element_proposer`

Reviewed on: 2020-02-04

The `element_proposer` is a component that uses the [`ElementManager`](//sdk/fidl/fuchsia.session/element_manager.fidl) service to request that `Element`s get added to the session. A session can contain any number of `ElementProposer` components. Notably, a component runner may act as an `ElementProposer`.

`ElementProposer`s commonly act as gateways to make software ecosystems available to a product. For example, a component acting as a voice assistant would propose elements in response to voice commands.

In this example, the `fuchsia-pkg://fuchsia.com/element_proposer#meta/element_proposer.cm` component is configured to use the `ElementManager` service. This is done by using the `ElementManager` protocol in the [`element_proposer.cml`](./meta/element_proposer.cml) file.

Once the element proposer is started, it connects to the `ElementManager` service and attempts to add an element to the session. The element proposer can propose both v1 and v2 components the session.
