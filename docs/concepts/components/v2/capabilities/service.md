# Service capabilities

[Service capabilities][glossary-service] allow components to connect to
[FIDL services][fidl-service] provided either by other components or the
component framework itself.

Note: _Protocol_ and _service_ capabilities are distinct types of
capabilities. A protocol represents a single instance of a
[FIDL protocol][glossary-fidl-protocol], while a service represents zero or
more instances of a [FIDL service][glossary-fidl-service].
See the documentation on [protocol capabilities][protocol-capability] for more
details.

## Providing service capabilities

Components provide service capabilities by either:

- [exposing](#providing-service-capability-expose) them,
- or [offering](#providing-service-capability-offer) them.

Components host service capabilities in their
[outgoing directory][glossary-outgoing].

### Exposing {#providing-service-capability-expose}

Exposing a service capability gives the component's parent access to that
capability. This is done through an [`expose`][expose] declaration.

```
{
    "expose": [{
        "service": "/svc/fuchsia.example.ExampleService",
        "from": "self",
    }],
}
```

The `"from": "self"` directive means that the service capability was created
by this component.

Note: The service path `"/svc/fuchsia.example.ExampleService"` follows a
convention and is explained in the [service paths section](#service-paths).

### Offering {#providing-service-capability-offer}

Offering a service capability gives a child component access to that
capability. This is done through an [`offer`][offer] declaration.

```
{
    "offer": [{
        "service": "/svc/fuchsia.example.ExampleService",
        "from": "self",
        "to": [{
            { "dest": "#child-a" },
            { "dest": "#child-b" },
        }],
    }],
}
```

## Consuming service capabilities

When a component [uses][use] a service capability that has been
[offered][offer] to it, that service is made available through the component's
[namespace][glossary-namespace].

Consider a component with the following manifest declaration:

```
{
    "use": [{
        "service": "/svc/fuchsia.example.ExampleService",
    }],
}
```

When the component attempts to open the path
`/svc/fuchsia.example.ExampleService`, the component framework performs
[capability routing][capability-routing] to find the component that provides
this service. Then, the framework connects the newly opened channel to this
provider.

For more information about the open request, see
[life of a protocol open][life-of-a-protocol-open].

For a working example of routing a service capability from one component to
another, see [`//examples/components/routing`][routing-example].

## Service paths {#service-paths}

When a service capability is `use`d by a component, its path refers to the
path in the component's [namespace][glossary-namespace].

When a service capability is `offer`ed or `expose`d from itself, its path
refers to the path in the component's [outgoing directory][glossary-outgoing].

The path also hints to clients which FIDL service the server expects clients to
use, but this is entirely a convention. Service capability paths can be renamed
when being [offered][offer], [exposed][expose], or [used][use].

In the following example, there are three components, `A`, `B`, and `C`, with
the following layout:

```
 A  <- offers service "/svc/fidl.example.X" from self to B as "/intermediary"
 |
 B  <- offers service "/intermediary" from realm to C as "/intermediary2"
 |
 C  <- uses service "/intermediary2" as "/service/example"
```

Each component in this example changes the path used to reference the service
when passing it along in this chain, and so long as components `A` and `C` know
which FIDL service to use over the channel, this will work just fine.

```
A.cml:
{
    "offer": [{
        "service": "/svc/fidl.example.X",
        "from": "self",
        "to": [{
            { "dest": "#B", "as": "/intermediary" },
        }],
    }],
    "children": [{
        "name": "B",
        "url": "fuchsia-pkg://fuchsia.com/B#B.cm",
    }],
}
```

```
B.cml:
{
    "offer": [{
        "service": "/intermediary",
        "from": "parent",
        "to": [{
            { "dest": "#C", "as": "/intermediary2" },
        }],
    }],
    "children": [{
        "name": "C",
        "url": "fuchsia-pkg://fuchsia.com/C#C.cm",
    }],
}
```

```
C.cml:
{
    "use": [{
        "service": "/intermediary2",
        "as": "/service/example",
    }],
}
```

When `C` attempts to open the `example` node in its `/service` directory, `A`
sees an open request for `/svc/fidl.example.X`. If any of the names don't
match in this chain, `C` will see its open attempt fail.

[capability-routing]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[expose]: /docs/concepts/components/v2/component_manifests.md#expose
[fidl-service]: /docs/concepts/components/v2/services.md
[framework-services]: /docs/concepts/components/v2/component_manifests.md#framework-services
[glossary-fidl]: /docs/glossary.md#fidl
[glossary-fidl-protocol]: /docs/glossary.md#protocol
[glossary-fidl-service]: /docs/glossary.md#service
[glossary-namespace]: /docs/glossary.md#namespace
[glossary-outgoing]: /docs/glossary.md#outgoing-directory
[glossary-protocol]: /docs/glossary.md#protocol-capability
[glossary-service]: /docs/glossary.md#service-capability
[life-of-a-protocol-open]: /docs/concepts/components/v2/life_of_a_protocol_open.md
[offer]: /docs/concepts/components/v2/component_manifests.md#offer
[protocol-capability]: /docs/concepts/components/v2/capabilities/protocol.md
[routing-example]: /examples/components/routing
[use]: /docs/concepts/components/v2/component_manifests.md#use
