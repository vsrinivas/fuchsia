# element_manager

Reviewed on: 2021-01-21

`element_manager` is a component that implements the
[`fuchsia.element.Manager`](//sdk/fidl/fuchsia.element/element_manager.fidl)
protocol. `element_manager` is not intended to be used on its own, but instead
composed into a session as a child.

`element_manager` launches all elements proposed using `Manager` in
a collection.

## Building

To add this project to your build, append
`--with //src/session/bin/element_manager` to the `fx set` invocation.

## Running

To include `element_manager` in a session, add the following to the session's
component manifest.

Once available, session manifests should instead include
`element_manager.shard.cml`. Related monorail issue:
[fxbug.dev/68107](fxbug.dev/68107)

```
{
    children: [
        {
            name: "element_manager",
            url: "fuchsia-pkg://fuchsia.com/element_manager#meta/element_manager.cm",
            startup: "eager",
        },
    ],
    capabilities: [
        { protocol: "fuchsia.element.Manager" },
    ],
    offer: [
        {
            protocol: [ "fuchsia.logger.LogSink" ],
            from: "parent",
            to: [ "#element_manager" ],
        },
    ],
    expose: [
        {
            protocol: "fuchsia.element.Manager",
            from: "#element_manager",
        },
    ],
}
```

## Testing

Unit tests for `element_manager` are available in the `element_manager_tests`
package.

```
$ fx test element_manager_tests
```

## Source layout

The entrypoint is located in `src/main.rs`. Unit tests are co-located with the code.

## Element annotations

`element_manager` [writes annotations] in the namespace "element_manager".

| Namespace         | Key    | Value [type]                 | Description                                                                                                                                                                                              |
|-------------------|--------|------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `element_manager` | `url`  | Element component URL [text] | Set from the value from `component_url` in the proposed element spec. The `GraphicalPresenter` implementation can use this annotation to determine which component is associated with an element's view. |

[writes annotations]: https://fuchsia.dev/reference/fidl/fuchsia.element#Annotation
