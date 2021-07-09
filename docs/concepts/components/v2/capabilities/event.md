# Event capabilities {#event-capabilities}

<<../../_v2_banner.md>>

Event capabilities allow components to receive events about a set of components,
called the *scope* of the event. Because event capabilities represent information about
components provided by component manager, they always originate from the
framework; they are never declared by components themselves. The scope of an event
is the component which originally offered or exposed the capability from the
framework and includes the subtree ([realm][realms]) rooted at the component that initially
routed the capability.

Components that wish to listen for events must have the
[`fuchsia.sys2.EventSource`][event-source] protocol routed to them.

Refer to [`fuchsia.sys2.EventType`][event-type] for the complete list of supported
events and their explanations.

## Event routing

Consider the following example of a component topology:

![A visual tree representation of the declarations explained below][example-img]

In this example:

- `core/archivist`: Can get `Started` events about the whole toplogy since
  it was offered `Started` from root through core. It also has access to the
  `fuchsia.sys2.EventSource` protocol that was explicitly routed to it from
  the root. In this case, this archivist will receive `Started` events for:
  `root`, `core`, `core/archivist`, `core/test_manager`, `core/test_manager/archivist`,
  `core/test_manager/tests:test-12345`, `core/test_manager/tests:test-12345/foo`
  and `core/test_manager/tests:test-12345/bar`.

- `core/test_manager/tests:test-12345`: Can get `Started` events about all
  components under the `test-12345` (`foo` and `bar`) because it uses the
  `Started` event from "framework" and has `fuchsia.sys2.EventSource` which
  test_manager offered to all components under the tests collection.

- `core/test_manager/archivist`: Can get `Started` events about all components
  under test_manager given that it was offered the `Started` event by
  test_manager from the framework as well as the `fuchsia.sys2.EventSource` protocol.

## Using events {#using-events}

Events may be [used][routing-terminology] by a component. A component that wants to
receive events declares in its `use` declarations the events it is interested in and the
`fuchsia.sys2.EventSource` protocol. Both the protocol and the events must have
been offered to the component for the component to be able to listen for that event.

Events can be used from two sources:

- `framework`: Events used from the framework are scoped to the realm of the component
  using them, this means that the events will be dispatched for the component and all its
  descendants.

- `parent`: Events used from the parent have been offered by the parent and
  are scoped to the realm of the component which originally routed the capability from
  the framework.

In the example above, the following `use` declarations exist:

```
// archivist.cml
{
  use: [
    { protocol: "fuchsia.sys2.EventSource" },
    { event: "started" }
  ]
}

// archivist.cml (the one at "core/test_manager/archivist")
{
  use: [
    { protocol: "fuchsia.sys2.EventSource" },
    { event: "started" }
  ]
}

// test-12345.cml
{
  use: [
    { protocol: "fuchsia.sys2.EventSource" },
    {
      event: "started",
      from: "framework"
    }
  ]
}
```

### Offering events {#offering-events}

Events may be [offered][routing-terminology] to children. In the example above,
the following `offer` declarations exist:

```json5
// root.cml
{
    offer: [
        {
            protocol: "fuchsia.sys2.EventSource",
            from: "parent",
            to: "#core",
        },
        {
            event: "started",
            from: "parent",
            to: "#core",
        },
    ]
}

// core.cml
{
    offer: [
        {
            protocol: "fuchsia.sys2.EventSource",
            from: "parent",
            to: [ "#archivist", "#test_manager" ],
        },
        {
            event: "started",
            from: "parent",
            to: [ "#archivist", "#test_manager" ],
        },
    ]
}

// test_manager.cml
{
    offer: [
        {
            protocol: "fuchsia.sys2.EventSource",
            from: "parent",
            to: [ "#tests", "#archivist" ],
        },
        {
            event: "started",
            from: "framework",
            to: "#archivist",
        },
    ]
}
```

Events can be offered from two sources:

-   `parent`: A component that was offered an event (`Started` for example) can
    offer this same event from its parent. The scope of the offered event will
    be the same scope of the `Started` event that the component was offered.
    In the example above, `archivist` gets `Started` from `core` which got it
    from `root`, therefore it's able to see `Started` for all components under
    `root`.

-   `framework`: A component can also offer an event that its parent didn't
    offer to it. The scope of this event will be the component's realm itself
    and all its descendants.

## Static event streams {#event-streams}

Event subscriptions can be set up statically in CML files through static event streams.
Static event streams are similar to event streams created through the
`fuchsia.sys2.EventSource/Subscribe` FIDL method but are set up by the framework during
the resolution of a component's manifest. The following is an example of the syntax of
a static event stream.

```json5
use: [
    {
        event: "started",
        from: "parent",
    },
    {
        event_stream: "MyEventStream",
        subscriptions: [
            {
                event: "started",
            }
        ],
    },
]
```

A component connects to a static event stream by calling
`fuchsia.sys2.EventSource/TakeStaticEventStream` and providing the name of the
event stream defined in its `use` declaration (`MyEventStream` in the example
above). A component can only refer to events explicitly `use`d in the
`subscriptions` section of its manifest. For example, if the manifest above had
`event: "stopped"` in the `subscriptions` section, a validation error would be
triggered since the event is not used.


[event-source]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#EventSource
[event-type]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#EventType
[example-img]: ../images/event-example.png
[realms]: /docs/concepts/components/v2/realms.md
[routing-terminology]: ../component_manifests.md#routing-terminology
