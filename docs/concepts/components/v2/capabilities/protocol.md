# Protocol capabilities

[Protocol capabilities][glossary-protocol] allow components to
connect to [FIDL protocols][glossary-fidl-protocol] provided either by other
components or the component framework itself.

Note: _Protocol_ and _service_ capabilities are distinct types of
capabilities. A protocol represents a single instance of a
[FIDL protocol][glossary-fidl-protocol], while a service represents zero or
more instances of a [FIDL service][glossary-fidl-service].
See the documentation on [service capabilities][service-capability]
for more details.

## Providing protocol capabilities

Components provide protocol capabilities by either:

- [exposing](#providing-protocol-capability-expose) them,
- or [offering](#providing-protocol-capability-offer) them.

Components host protocol capabilities in their
[outgoing directory][glossary-outgoing].

### Exposing {#providing-protocol-capability-expose}

Exposing a protocol capability gives the component's parent access to that
capability. This is done through an [`expose`][expose] declaration.

```
{
    "expose": [{
        "protocol": "/svc/fuchsia.example.ExampleProtocol",
        "from": "self",
    }],
}
```

The `"from": "self"` directive means that the protocol capability was created
by this component.

Note: The protocol path `"/svc/fuchsia.example.ExampleProtocol"` follows a
convention and is explained in the [protocol paths section](#protocol-paths).

### Offering {#providing-protocol-capability-offer}

Offering a protocol capability gives a child component access to that
capability. This is done through an [`offer`][offer] declaration.

```
{
    "offer": [{
        "protocol": "/svc/fuchsia.example.ExampleProtocol",
        "from": "self",
        "to": [{
            { "dest": "#child-a" },
            { "dest": "#child-b" },
        }],
    }],
}
```

## Consuming protocol capabilities

When a component [uses][use] a protocol capability that has been
[offered][offer] to it, that protocol is made available through the component's
[namespace][glossary-namespace].

Consider a component with the following manifest declaration:

```
{
    "use": [{
        "protocol": "/svc/fuchsia.example.ExampleProtocol",
    }],
}
```

When the component attempts to open the path
`/svc/fuchsia.example.ExampleProtocol`, the component framework performs
[capability routing][capability-routing] to find the component that provides
this protocol. Then, the framework connects the newly opened channel to this
provider.

For more information about the open request, see
[life of a protocol open][life-of-a-protocol-open].

For a working example of routing a protocol capability from one component to
another, see [`//examples/components/routing`][routing-example].

## Consuming protocol capabilities provided by the framework

Some protocol capabilities are provided by the component framework, and thus
can be [used][use] by components without their parents [offering][offer] them.

For a list of these protocols and what they can be used for, see
[framework protocols][framework-protocols].

```
{
    "use": [{
        "protocol": "/svc/fuchsia.sys2.Realm",
        "from": "framework",
    }],
}
```

## Protocol paths {#protocol-paths}

When a protocol capability is `use`d by a component, its path refers to the
path in the component's [namespace][glossary-namespace].

When a protocol capability is `offer`ed or `expose`d from itself, its path
refers to the path in the component's [outgoing directory][glossary-outgoing].

The path also hints to clients which FIDL protocol the server expects clients
to use, but this is entirely a convention. Protocol capability paths can be
renamed when being [offered][offer], [exposed][expose], or [used][use].

In the following example, there are three components, `A`, `B`, and `C`, with
the following layout:

```
 A  <- offers protocol "/svc/fidl.example.X" from self to B as "/intermediary"
 |
 B  <- offers protocol "/intermediary" from realm to C as "/intermediary2"
 |
 C  <- uses protocol "/intermediary2" as "/protocol/example"
```

Each component in this example changes the path used to reference the protocol
when passing it along in this chain, and so long as components `A` and `C`
know which FIDL protocol to use over the channel, this will work just fine.

```
A.cml:
{
    "offer": [{
        "protocol": "/svc/fidl.example.X",
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
        "protocol": "/intermediary",
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
        "protocol": "/intermediary2",
        "as": "/svc/example",
    }],
}
```

When `C` attempts to open the `example` node in its `/protocol` directory, `A`
sees an open request for `/svc/fidl.example.X`. If any of the names don't
match in this chain, `C` will see its open attempt fail.

[capability-routing]: /docs/concepts/components/v2/component_manifests.md#capability-routing
[expose]: /docs/concepts/components/v2/component_manifests.md#expose
[framework-protocols]: /docs/concepts/components/v2/component_manifests.md#framework-protocols
[glossary-fidl]: /docs/glossary.md#fidl
[glossary-fidl-protocol]: /docs/glossary.md#protocol
[glossary-fidl-service]: /docs/glossary.md#service
[glossary-namespace]: /docs/glossary.md#namespace
[glossary-outgoing]: /docs/glossary.md#outgoing-directory
[glossary-protocol]: /docs/glossary.md#protocol-capability
[life-of-a-protocol-open]: /docs/concepts/components/v2/life_of_a_protocol_open.md
[offer]: /docs/concepts/components/v2/component_manifests.md#offer
[routing-example]: /examples/components/routing
[service-capability]: /docs/concepts/components/v2/capabilities/service.md
[use]: /docs/concepts/components/v2/component_manifests.md#use
