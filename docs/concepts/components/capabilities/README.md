# Capabilities

This directory contains documentation about the various capability types in the
component framework.

Capabilities grant the ability to a component to connect to and access resources
from other components.

- [Event capabilities](event.md): receive lifecycle events about components at
  a certain scope.
- [Directory capabilities](directory.md): connect to directories provided by
  other components.
- [Protocol capabilities](protocol.md): connect to FIDL protocols provided by
  other components or the framework itself.
- [Service capabilities](service.md): connect to FIDL services (groups of
  protocols) provided by other components or the framework itself.
- [Storage capabilities](storage.md): special-cased directories with different
  semantics.
