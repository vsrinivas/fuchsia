# Directory capabilities

<<../../_v2_banner.md>>

[Directory capabilities][glossary.directory capability] allows components
to connect to directories provided by other components.

## Providing directory capabilities

To provide a directory capability, a component must define the capability and
[route](#routing-directory-capabilities) it from `self`. The component hosts the
directory capability in its [outgoing directory][glossary.outgoing directory].

To define the capability, add a `capabilities` declaration for it:

```json5
{
    capabilities: [
        {
            directory: "data",
            rights: ["r*"],
            path: "/published-data",
        },
    ],
}
```

This defines a capability hosted by this component whose outgoing directory path
is `/published-data`, and whose maximum usable
[rights](#directory-capability-rights) are `r*`.

## Routing directory capabilities

Components route directory capabilities by either
[exposing](#routing-directory-capability-expose) them or
[offering](#routing-directory-capability-offer) them.

When a component wants to make one of its directories available to other
components, it specifies the path of that directory in its
[outgoing directory][glossary.outgoing directory] in one of the
following ways:

### Exposing {#routing-directory-capability-expose}

To [expose][expose] the directory to a parent:

```json5
{
    expose: [
        {
            directory: "data",
            from: "#child-a",
        },
    ],
}
```

You may optionally specify:

* [`as`](#renaming)
* [`rights`](#directory-capability-rights)
* [`subdir`](#subdirectories)

### Offering {#routing-directory-capability-offer}

To [offer][offer] a directory to a child:

```json5
{
    offer: [
        {
            directory: "data",
            from: "parent",
            to: [ "#child-a", "#child-b" ],
        },
    ],
}
```

You may optionally specify:

* [`as`](#renaming)
* [`rights`](#directory-capability-rights)
* [`subdir`](#subdirectories)

## Consuming directory capabilities

When a component wants to make use of a directory from its parent, it does so by
[using][use] the directory. This will make the directory accessible from the
component's [namespace][glossary.namespace].

This example shows a directory named `data` that is included in the component's
namespace. If the component instance accesses this directory during its
execution, the component framework performs
[capability routing][capability-routing] to find the component that provides it.
Then, the framework connects the directory from the component's namespace to
this provider.

```json5
{
    use: [
        {
            directory: "data",
            rights: ["r*"],
            path: "/data",
        },
    ],
}
```

You must specify [`rights`](#directory-capability-rights).
You may optionally specify [`subdir`](#subdirectories).

## Directory capability rights {#directory-capability-rights}

Directory rights enable components to control access to directories as they are
routed throughout the system. Directory rights are applied as follows:

-   [`capability`][capability]: *Required*.
    Provides the base set of rights available for the directory. Any rights
    specified in a `use`, `offer`, or `expose` must be a subset of what is
    declared here.
-   [`use`][use]: *Required*.
    Describes the access rights requested by the consuming component.
-   [`offer`][offer]: *Optional*.
    Modified rights available to the destination component. Rights are inherited
    from the `offer` source if not present. 
-   [`expose`][expose]: *Optional*.
    Modified rights available to the destination component. Rights are inherited
    from the `expose` source if not present.

The `rights` field can contain any combination of the following
[`fuchsia.io2.Rights`][fidl-io2-rights] tokens:

```json5
rights: [
  "connect",
  "enumerate",
  "traverse",
  "read_bytes",
  "write_bytes",
  "execute_bytes",
  "update_attributes",
  "get_attributes",
  "modify_directory",
]
```

The framework provides a simplified form for declaring `rights` using *aliases*.
Each alias represents the combination of FIDL rights tokens to provide common
read, write, or execute access:

| Alias | FIDL rights                                                |
| :---: | ---------------------------------------------------------- |
| `r*`  | `connect, enumerate, traverse, read_bytes,`                |
:       : `get_attributes`                                           :
| `w*`  | `connect, enumerate, traverse, write_bytes,`               |
:       : `update_attributes, modify_directory`                      :
| `x*`  | `connect, enumerate, traverse, execute_bytes`              |
| `rw*` | `connect, enumerate, traverse, read_bytes, write_bytes,`   |
:       : `get_attributes, update_attributes, modify_directory`      :
| `rx*` | `connect, enumerate, traverse, read_bytes, execute_bytes,` |
:       : `get_attributes`                                           :

The `rights` field may only contain one alias. Additional FIDL rights may be
appended as long as they do not duplicate rights expressed by the alias.

### Example

This example shows component `A` requesting access to `data` with read-write
rights:

```json5
// A.cml
{
    use: [
        {
            directory: "data",
            rights: ["rw*"],
            path: "/data",
        },
    ],
}
```

Furthermore, parent component `B` offers the directory `data` to component A but
with only read-only rights. In this case the routing fails and `data` wouldn't
be present in A's namespace.

```json5
// B.cml
{
    capabilities: [
        {
            directory: "data",
            rights: ["r*"],
            path: "/published-data",
        },
    ],
    offer: [
        {
            directory: "data",
            from: "self",
            to: [ "#A" ],
        },
    ],
}
```

## Subdirectories {#subdirectories}

You may `expose`, `offer`, or `use` a subdirectory of a directory capability:

```json5
{
    offer: [
        {
            directory: "data",
            from: "parent",
            to: [ "#child-a", "#child-b" ],
            subdir: "children",
        },
    ],
}
```

## Renaming directories {#renaming}

You may `expose` or `offer` a directory capability by a different name:

```json5
{
    offer: [
        {
            directory: "data",
            from: "#child-a",
            to: [ "#child-b" ],
            as: "a-data",
        },
    ],
}
```

## Framework directories {#framework}

A *framework directory* is a directory provided by the component framework.
Any component may `use` these capabilities by setting `framework` as the source
without an accompanying `offer` from its parent.
Fuchsia supports the following framework directories:

-   [hub][doc-hub]: Allows a component to perform runtime introspection of
    itself and its children.

```
{
    use: [
        {
            directory: "hub",
            from: "framework",
            rights: ["r*"],
            path: "/hub",
        },
    ],
}
```

[glossary.directory capability]: /docs/glossary/README.md#directorty-capability
[glossary.outgoing directory]: /docs/glossary/README.md#outgoing-directory
[capability]: ../component_manifests.md#capability
[capability-routing]: ../component_manifests.md#capability-routing
[doc-hub]: /docs/concepts/components/v2/hub.md
[fidl-io2-rights]: /sdk/fidl/fuchsia.io2/rights-abilities.fidl
[expose]: ../component_manifests.md#expose
[offer]: ../component_manifests.md#offer
[routing-example]: /examples/components/routing
[use]: ../component_manifests.md#use
