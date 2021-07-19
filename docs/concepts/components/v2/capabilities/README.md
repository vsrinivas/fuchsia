# Capabilities

[Capabilities][glossary.capability] can be created, routed, and used in a
[component manifest][doc-component-manifest] to control which parts of Fuchsia
have ability to connect to and access which resources.

## Routing terminology {#routing-terminology}

Component manifests declare how capabilities are routed between
[components][glossary.component]. The language of capability routing consists of
the following three keywords:

-   `use`: When a component `uses` a capability, the capability is installed in
    the component's namespace. A component may `use` any capability that has
    been `offered` or `exposed` to it.
-   `offer`: A component may `offer` a capability to a *target*, which is either
    a [child][glossary.child] or
    [collection][doc-collections]. When a capability is offered to a child, the
    child instance may `use` the capability or `offer` it to one of its own
    targets. Likewise, when a capability is offered to a collection, any
    instance in the collection may `use` the capability or `offer` it.
-   `expose`: When a component `exposes` a capability to its
    [parent][glossary.parent], the parent may `offer` the
    capability to one of its other children. A component may `expose` any
    capability that it provides, or that one of its children exposes.

When you use these keywords together, they express how a capability is routed
from a component instance's [outgoing directory][doc-outgoing-directory] to
another component instance's namespace:

-   `use` describes the capabilities that populate a component instance's
    namespace.
-   `expose` and `offer` describe how capabilities are passed between component
    instances. Aside from their directionality, there is another significant
    difference between `offer` and `expose`. A component may freely `use` a
    capability that was `offered` to it, however, in order to prevent dependency
    cycles between parents and children there are restrictions on `using` a
    capability that is `exposed` to a component by its child. If the parent
    offers no capabilities to the child which the parent *itself* provides, then
    it can `use` from its child. If the parent does offer capabilities that it 
    provides, then the `use` from the child must be marked as `weak`.

## Capability types

- [Directory capabilities](directory.md) connect to directories provided by
  other components.
- [Event capabilities](event.md) receive lifecycle events about components at
  a certain scope.
- [Protocol capabilities](protocol.md) are service nodes that can be used to
  open a channel to a FIDL protocol.
- [Resolver capabilities](resolvers.md) when registered in an
  [environment](../environments.md), cause a component with a particular URL
  scheme to be resolved with that [resolver][doc-resolvers].
- [Runner capabilities](runners.md) determines which runner is responsible
  for instantiating the component and assisting with its lifecycle.
- [Service capabilities](service.md) connect to FIDL services (groups of
  protocols) provided by other components or the framework itself.
- [Storage capabilities](storage.md) are special-cased directories with different
  semantics that are isolated to the components using them.

`directory`, `event`, `protocol`, `service` and `storage` capabilities are routed to
components that `use` them. `resolver` and `runner` capabilities are routed to
[environments](#environments) that include them.

For more information on what happens when connecting to a capability, see
[Life of a protocol open][doc-protocol-open].

[doc-collections]: /docs/concepts/components/v2/realms.md#collections
[doc-component-manifest]: /docs/concepts/components/v2/component_manifests.md
[doc-outgoing-directory]: /docs/concepts/system/abi/system.md#outgoing_directory
[doc-protocol-open]: /docs/concepts/components/v2/capabilities/life_of_a_protocol_open.md
[doc-resolvers]: /docs/concepts/components/v2/capabilities/resolvers.md
[glossary.capability]: /docs/glossary#capability
[glossary.child]: /docs/glossary#child-component-instance
[glossary.component]: /docs/glossary#component
[glossary.parent]: /docs/glossary#parent-component-instance