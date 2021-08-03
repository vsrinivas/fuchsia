# Service capabilities

<<../../_v2_banner.md>>

Caution: service capabilities are experimental and in development. Their
behavior and APIs could change at any time.

[Service capabilities][glossary.service-capability] allow components
to connect to [FIDL services][glossary.service]
provided either by other components or the component framework itself.

Note: _Protocol_ and _service_ capabilities are distinct types of capabilities.
A protocol represents a single instance of a
[FIDL protocol][glossary.protocol], while a service represents zero or more
instances of a [FIDL service][glossary.service]. See the documentation on
[protocol capabilities][protocol-capability] for more details.

## Providing service capabilities {#provide}

To provide a service capability, a component must define the capability and
[route](#route) it from `self`. The component hosts the
service capability in its [outgoing directory][glossary.outgoing-directory].

To define the capability, add a `capabilities` declaration for it:

```json5
{
    capabilities: [
        {
            service: "fuchsia.example.ExampleService",
        },
    ],
}
```

This defines a capability hosted by this component whose outgoing directory path
is `/svc/fuchsia.example.ExampleService`. You can also customize the path:

```json5
{
    capabilities: [
        {
            service: "fuchsia.example.ExampleService",
            path: "/my_svc/fuchsia.example.MyExampleService",
        },
    ],
}
```

## Routing service capabilities {#route}

Components route service capabilities by [exposing](#expose) them to their
parent and [offering](#offer) them to their children.

### Exposing {#expose}

Exposing a service capability gives the component's parent access to that
capability. This is done through an [`expose`][expose] declaration.

```json5
{
    expose: [
        {
            service: "fuchsia.example.ExampleService",
            from: "self",
        },
    ],
}
```

The `from: "self"` directive means that the service capability is provided by
this component. In this case the service must have a corresponding
[definition](#provide).

#### Services routed from collections

A service capability can be exposed from a [dynamic collection][collection].

```json5
{
    collections: [
        {
            name: "coll",
            durability: "transient",
        },
    ],
    expose: [
        {
            service: "fuchsia.example.ExampleService",
            from: "#coll",
        },
    ],
}
```

When routing services exposed from the components in a collection, each
[service instance][service-instances] entry in the client component's
[namespace][glossary.namespace] is prefixed with the component name to allow
multiple components in the collection to expose the same instance.

The instances are named with the scheme `"$component_name,$instance_name"`.
The `$component_name` is the name given to the
[`fuchsia.sys2/Realm.CreateChild`][realm.fidl] API when the component is
created. The `$instance_name` is defined by the component itself.

For example, the namespace path for the `default` instance of
`fuchsia.example.ExampleService` exposed from a component named `foo` within the
above collection is `/svc/fuchsia.example.ExampleService/foo,default/protocol`.

### Offering {#offer}

Offering a service capability gives a child component access to that capability.
This is done through an [`offer`][offer] declaration.

```json5
{
    offer: [
        {
            service: "fuchsia.example.ExampleService",
            from: "self",
            to: [ "#child-a", "#child_b" ],
        },
    ],
}
```

## Consuming service capabilities {#consume}

When a component [uses][use] a service capability that has been [offered][offer]
to it, that service is made available through the component's
[namespace][glossary.namespace].

Consider a component with the following manifest declaration:

```
{
    use: [
        {
            service: "fuchsia.example.ExampleService",
        },
    ],
}
```

When the component attempts to open the path
`/svc/fuchsia.example.ExampleService`, the component framework performs
[capability routing][capability-routing] to find the component that provides
this service. Then, the framework connects the newly opened channel to this
provider.

You can also customize the namespace path:

```json5
{
    use: [
        {
            service: "fuchsia.example.ExampleService",
            path: "/my_svc/fuchsia.example.MyExampleService",
        },
    ],
}
```

For more information about the open request, see
[life of a protocol open][life-of-a-protocol-open].

Note: For a working example of routing a service capability between components,
see [`//examples/components/services`][routing-example].

[collection]: /docs/concepts/components/v2/realms.md#collections
[glossary.namespace]: /docs/glossary/README.md#namespace
[glossary.outgoing-directory]: /docs/glossary/README.md#outgoing-directory
[glossary.protocol]: /docs/glossary/README.md#protocol
[glossary.service]: /docs/glossary/README.md#service
[glossary.service-capability]: /docs/glossary/README.md#service-capability
[capability-routing]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[expose]: /docs/concepts/components/v2/component_manifests.md#expose
[fidl-service]: /docs/concepts/components/v2/services.md
[framework-services]: /docs/concepts/components/v2/component_manifests.md#framework-services
[life-of-a-protocol-open]: /docs/concepts/components/v2/capabilities/life_of_a_protocol_open.md
[offer]: /docs/concepts/components/v2/component_manifests.md#offer
[protocol-capability]: /docs/concepts/components/v2/capabilities/protocol.md
[realm.fidl]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#Realm
[routing-example]: /examples/components/services
[service-instances]: /docs/concepts/fidl/services.md#instances
[use]: /docs/concepts/components/v2/component_manifests.md#use
