## Overview

The ElementProposer is a component that uses the `ElementManager` service to
request Elements be added to the session. A session can contain any number of
ElementProposer components. Notably, a component runner may act as an
ElementProposer.

ElementProposers commonly act as gateways to make software ecosystems available
to a product. For example, a component acting as a voice assistant would propose
Elements in response to voice commands.

## ElementProposer

In this example, the
`fuchsia-pkg://fuchsia.com/element_proposer#meta/element_proposer.cm` component
is configured to:

  1. Use the `ElementManager` service.

This is done by using the `ElementManager` protocol in the
[`element_proposer.cml`]() file.

Once the element proposer is started it connects to the `ElementManager` service
and attempts to add an element to the session. The element proposer can propose
both v1 and v2 components the session.