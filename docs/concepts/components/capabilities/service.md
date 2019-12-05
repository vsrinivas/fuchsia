# Service capabilities

[Service capabilities][glossary-service] allow components to connect to
[FIDL][glossary-fidl] service protocols provided either by other components or the
component framework itself.

## Creating service capabilities

When a component has a service protocol that is made available to other components, the
service's path in the component's [outgoing directory][glossary-outgoing] is
[exposed][expose] to the component's parent...

```
{
    "expose": [{
        "service_protocol": "/svc/fuchsia.example.ExampleService",
        "from": "self",
    }],
}
```

... or [offered][offer] to some of the component's children.

```
{
    "offer": [{
        "service_protocol": "/svc/fuchsia.example.ExampleService",
        "from": "self",
        "to": [{
            { "dest": "#child-a" },
            { "dest": "#child-b" },
        }],
    }],
}
```

## Consuming service capabilities

When a service capability is offered to a component from its containing realm it
can be [used][use] to make the service protocol accessible through the component's
[namespace][glossary-namespace].

This example shows a directory named `/svc` that is included in the component's
namespace. When the component attempts to open the
`fuchsia.example.ExampleService` item in this directory, the component framework
performs [capability routing][capability-routing] to find the component that
provides this service protocol. Then, the framework connects the newly opened
channel to this provider.

```
{
    "use": [{
        "service": "/svc/fuchsia.example.ExampleService",
    }],
}
```

See [life of a service open][life-of-a-service-open] for a detailed walkthrough
of what happens during this open request.

See [`//examples/components/routing`][routing-example] for a working example of
routing a service capability from one component to another.

## Consuming service capabilities provided by the framework

Some service capabilities are provided by the component framework, and thus can
be [used][use] by components without their parents [offering][offer] them.

For a list of these service protocols and what they can be used for, see the
[framework services][framework-services] section of the component manifests
documentation.

```
{
    "use": [{
        "service_protocol": "/svc/fuchsia.sys2.Realm",
        "from": "framework",
    }],
}
```

## Service paths

The path used to refer to a given service protocol provides a hint to clients which
protocol the server expects clients to use, but this is entirely a convention.
The paths can even be renamed when being [offered][offer], [exposed][expose], or
[used][use].

In the following example, there are three components, `A`, `B`, and `C`, with
the following layout:

```
 A  <- offers service "/svc/fidl.example.X" from "self" to B as "/intermediary"
 |
 B  <- offers service "/intermediary" from "realm" to B as "/intermediary2"
 |
 C  <- uses service "/intermediary2" as "/service/example"
```

Each component in this example changes the path used to reference the service
protocol when passing it along in this chain, and so long as components `A`
and `C` know which FIDL protocol to use over the channel, this will work just
fine.

```
A.cml:
{
    "offer": [{
        "service_protocol": "/svc/fidl.example.X",
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
        "from": "realm",
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
will see an open request for `/svc/fidl.example.X`. If any of the names didn't
match in this chain, `C` would see the opened `example` node be closed.

[capability-routing]: /docs/concepts/components/component_manifests.md#capability-routing
[expose]: ../component_manifests.md#expose
[framework-services]: /docs/concepts/components/component_manifests.md#framework-services
[glossary-fidl]: /docs/glossary.md#fidl
[glossary-namespace]: /docs/glossary.md#namespace
[glossary-outgoing]: /docs/glossary.md#outgoing-directory
[glossary-service]: /docs/glossary.md#service-capability
[life-of-a-service-open]: /docs/concepts/components/life_of_a_service_open.md
[offer]: /docs/concepts/components/component_manifests.md#offer
[routing-example]: /examples/components/routing
[use]: /docs/concepts/components/component_manifests.md#use
