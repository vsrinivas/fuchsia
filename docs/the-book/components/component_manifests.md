# Component manifests

A [component manifest](#component-manifest) is a file that defines a component
by encoding a [component declaration](#component-declaration). This document
gives an overview of the concepts used by component declarations, and presents
the syntax for writing [component manifest source](#component-manifest-source).
Component declarations contain:

- Information about [how to run the component](#runtime).
- The realm's [child component instances](#child-component-instances) and
  [component collections](#component-collections).
- [Routing rules](#capability-routing) that describe how capabilities are used,
  exposed, and offered between components.
- [Freeform data ("facets")](#facet-metadata) which is ignored by the component
  framework but can be interpreted by third parties.

## Manifests and declarations

This section explains the distinction between component manifests, component
manifest sources, and component declarations.

### Component manifest

A *component manifest* is a file that encodes a [component
declaration](#component-declaration), usually distributed as part of a
[package](/sdk/docs/packages.md). The binary format is a JSON file mapping
one-to-one onto the component declaration, by convention ending in a `.cm`
extension.

A [fuchsia-pkg URL](/docs/the-book/package_url.md) with a component manifest
fragment identifies a component in a package.

### Component manifest source

A *component manifest source* is a file that encodes part of a component
manifest. Component manifest sources are written in *CML* (*component manifest
language*), which is the developer-facing source format for component manifests.
CML files are JSON5 files that end with a `.cml` extension. Descriptions and
examples of the CML syntax are contained in this document: see
[Syntax](#syntax).

Component manifest sources are compiled to [component
manifests](#component-manifest) by the [`cmc`](/src/sys/cmc) tool.

### Component declaration

The [`ComponentDecl`](/sdk/fidl/fuchsia.sys2/decls/component_decl.fidl) FIDL
table is a *component declaration*. Component declarations are used by the
component framework APIs to represent components and may be provided to
components at runtime.

## Concepts

### Runtime

The [`program`](#program) section of a component manifest declares how the
component is run. For a component containing an ELF binary, this section
consists of a path to a binary in the package and, optionally, a list of
arguments. For a component that does not contain an executable, this section is
omitted.

See also: [ELF Runner](elf_runner.md)

### Child component instances

A *child component instance* is a component instance that is owned by another
component (the "parent"). Child instances may be *static* or *dynamic*. Static
children are declared by the [`children`](#children) section of a component
manifest. Dynamic children are created in a [collection](#component-collections)
at runtime.

The [`offer`](#offer) declarations determine which capabilities child instances
have access to (see [Routing terminology](#routing-terminology)).

### Component collections

A *collection* is a container for component instances which may be created and
destroyed at runtime using the
[`fuchsia.sys2.Realm`](/sdk/fidl/fuchsia.sys2/realm.fidl) [framework
service](#framework-services). Collections are declared in the
[`collections`](#collections) section of a component manifest. When an
[`offer`](#offer) declaration targets a collection, the offered capability is
made available to every instance in the collection (see [Routing
terminology](#routing-terminology)).

### Capability routing

Component manifests provide a syntax for routing capabilities between
components. For a detailed walkthrough about what happens during capability
routing, see [_Life of a service open_](life_of_a_service_open.md)

#### Capability types

The following capabilities can be routed:

- `service`: A filesystem service node that can be used to open a channel to a
  service provider.
- `directory`: A filesystem directory.
- `storage`: A filesystem directory that is isolated to the component using it.

#### Routing terminology

Component manifests declare how capabilities are routed between components. The
language of capability routing consists of the following three keywords:

- `use`: When a component `uses` a capability, the capability is installed in
  the component's namespace. A component may `use` any capability that has been
  `offered` to it.
- `offer`: A component may `offer` a capability to a *target*, which is either a
  [child](#child-component-instances) or [collection](#collections). When a
  capability is offered to a child, the child instance may `use` the capability
  or `offer` it to one of its own targets. Likewise, when a capability is
  offered to a collection, any instance in the collection may `use` the
  capability or `offer` it.
- `expose`: When a component `exposes` a capability to its containing realm
  (i.e., its parent), the parent may `offer` the capability to one of its other
  children. A component may `expose` any capability that it provides, or that
  one of its children exposes.

When you use these keywords together, they express how a capability is routed
from a component instance's [outgoing
directory](/docs/development/abi/system.md#outgoing-directory) to another
component instance's namespace:

- `use` describes the capabilities that populate a component instance's
  namespace.
- `expose` and `offer` describe how capabilities are passed between component
  instances. Aside from their directionality, there is one significant
  difference between `offer` and `expose`. While a component may `use` a
  capability that was `offered` to it, a component is not allowed to `use` a
  capability that was `exposed` to it by its child. This restriction exists to
  prevent dependency cycles between parent and child.

#### Framework services

A *framework service* is a service provided by the component framework. Because
the component framework itself is the provider of the service, any component may
`use` it without an explicit `offer`.  Fuchsia supports the following framework
services:

- [`fuchsia.sys2.Realm`](/sdk/fidl/fuchsia.sys2/realm.fidl): Allows a component
  to manage and bind to its children. Scoped to the component's realm.

#### Capability paths

Service and directory capabilities are identified by paths.  A path consists of
a sequence of path components, starting with and separated by `/`, where each
path component consists one or more non-`/` characters.

A path may either be a *source path* or *target path*, whose meaning depends on
context:

- A *source path* is either a path in the component's outgoing directory (for
  `offer` or `expose` from `self`), or the path by which the capability was
  offered or exposed to this component.
- A *target path* is either a path in the component's namespace (for `use`), or
  the path by which the capability is being `offered` or `exposed` to another
  component.

#### Storage capabilities

Storage capabilities are not directly provided from a component instance's
[outgoing directory](/docs/development/abi/system.md#outgoing-directory), but
are created from preexisting directory capabilities that are declared in
[`storage`](#storage) in a component manifest. This declaration describes the
source for a directory capability and can then be listed as a source for
offering storage capabilities.

Storage capabilities cannot be [exposed](#expose).

#### Storage types

Storage capabilities are identified by types. Valid storage types are `data`,
`cache`, and `meta`, each having different semantics:

- `data`: A mutable directory the component may store its state in. This
  directory is guaranteed to be unique and non-overlapping with
  directories provided to other components.
- `cache`: Identical to the `data` storage type, but the framework may delete
  items from this directory to reclaim space.
- `meta`: A directory where the framework can store metadata for the component
  instance. Features such as persistent collections must use this capability as
  they require component manager to store data on the component's behalf. The
  component cannot directly access this directory.

#### Examples

For an example of how these keywords interact, consider the following component
instance tree:

![Capability routing example](capability_routing_example.png)

In this example, the *echo* component instance provides an `/svc/echo` service
in its outgoing directory. This service is routed to the `echo_tool` component
instance, which uses it. It is necessary for each component instance in the
routing path to propagate `/svc/echo` to the next component instance.

The routing sequence is:

- *echo* hosts the `/svc/echo` service in its outgoing directory. Also, it
  exposes `/svc/echo` from `self` so the service is visible to its parent,
  *system*.
- *system* exposes `/svc/echo` from its child `echo` to its parent, *root*.
- *root* offers `/svc/echo` from its child `system` to its other child *shell*.
- *shell* offers `/svc/echo` from `realm` (i.e., its parent) to its child
  *echo_tool*.
- *echo_tool* uses `/svc/echo`. When `echo_tool` runs, it will find `/svc/echo`
  in its namespace.

A working example of capability routing can be found at
[//examples/components/routing](/examples/components/routing).

### Facet metadata

*Facets* are metadata that is ignored by the component framework itself, but may
be interpreted by interested components. For example, a module component might
contain [module facets](/docs/the-book/modular/module_facet.md) declaring intents
the module subscribes to.

## Syntax

This section explains the syntax for each section of the component manifest, in
CML format. For the full schema, see
[cml_schema.json](/garnet/lib/rust/cm_json/cml_schema.json).

### program

`program` varies depending on how the component is run. If the component
contains no executable, `program` is to omitted. If the component contains an
ELF binary, `program` will contain information on how to run the binary.

When `program` contains information on how to run a binary, it is an object with
the following properties:

- `binary`: Package-relative path to the executable binary
- `args` *(optional)*: List of arguments

Example:

```
"program": {
    "binary": "bin/hippo",
    "args": [ "Hello", "hippos!" ],
},
```

See also: [ELF Runner](elf_runner.md)

### children

The `children` section declares child component instances as described in [Child
component instances](#child-component-instances).

`children` is an array of objects with the following properties:

- `name`: The name of the child component instance, which is a string of one or
  more of the following characters: `a-z`, `0-9`, `_`, `.`, `-`.
- `url`: The component URL for the child component instance.
- `startup` *(optional)*: The component instance's startup mode.
    - `lazy` *(default)*: Start the component instance only if another component
      instance binds to it.
    - `eager`: Start the component instance as soon as its parent starts.

Example:

```
"children": [
    {
        "name": "logger",
        "url": "fuchsia-pkg://fuchsia.com/logger#logger.cm",
    },
    {
        "name": "pkg_cache",
        "url": "fuchsia-pkg://fuchsia.com/pkg_cache#meta/pkg_cache.cm",
        "startup": "eager",
    },
],
```

### collections

The `collections` section declares collections as described in [Component
collections](#component-collections).

`collections` is an array of objects with the following properties:

- `name`: The name of the component collection, which is a string of one or more
  of the following characters: `a-z`, `0-9`, `_`, `.`, `-`.
- `durability`: The duration of child component instances in the collection.
    - `transient`: The instance exists until its containing realm is stopped or
      it is explicitly destroyed.
    - `persistent`: The instance exists until it is explicitly destroyed. This
      mode is not yet supported.

Example:

```
"collections": [
    {
        "name": "tests",
        "durability": "transient",
    },
],
```

### use

The `use` section contains `use` declarations of child component instances as
explained in [Routing terminology](#routing-terminology).

`use` is an array of objects with the following properties:

- A capability declaration, one of:
    - `service`: The [source path](#capability-paths) of a service capability.
    - `directory`: The [source path](#capability-paths) of a directory
      capability.
    - `storage`: The [type](#storage-types) of a storage capability. A manifest
      can only declare one `use` for each storage type.
- `as` *(optional)*: The explicit [target path](#capability-paths) for the
  capability. If omitted, defaults to the source path for service and directory
  capabilities, and one of `/data` or `/cache` for storage capabilities. This
  property cannot be used for meta storage capabilities.

Example:

```
"use": [
    {
        "service": "/svc/fuchsia.log.Log",
    },
    {
        "directory": "/data/themes",
        "as": "/themes",
    },
    {
        "storage": "data",
        "as": "/my_data",
    },
],
```

### expose

The `expose` section declares capabilities exposed by this component, as
explained in [Routing terminology](#routing-terminology).

`expose` is an array of objects with the following properties:

- A capability declaration, one of:
    - `service`: The [source path](#capability-paths) of a service capability.
    - `directory`: The [source path](#capability-paths) of a directory
      capability.
- `from`: The source of the capability, one of:
    - `self`: This component.
    - `#<child-name>`: A [reference](#references) to a child component instance.
- `as` *(optional)*: The explicit [target path](#capability-paths) for the
  capability. If omitted, defaults to the source path.

Example:

```
"expose: [
    {
        "directory": "/data/themes",
        "from": "self",
    },
    {
        "service": "/svc/pkg_cache",
        "from": "#pkg_cache",
        "as": "/svc/fuchsia.pkg.PackageCache",
    },
],
```

### offer

The `offer` section declares capabilities offered by this component, as
explained in [Routing terminology](#routing-terminology).

`offer` is an array of objects with the following properties:

- A capability declaration, one of:
    - `service`: The [source path](#capability-paths) of a service capability.
    - `directory`: The [source path](#capability-paths) of a directory
      capability.
    - `storage`: The [type](#storage-types) of a storage capability.
- `from`: The source of the capability, one of:
    - `realm`: The component's containing realm (parent). This source can be
      used for all capability types.
    - `self`: This component. This source can only be used when offering service
      or directory capabilities.
    - `#<child-name>`: A [reference](#references) to a child component instance.
      This source can only be used when offering service or directory
      capabilities.
    - `#<storage-name>` A [reference](#references) to a storage declaration.
      This source can only be used when offering storage capabilities.
- `to`: An array of target declarations, each of which is an object with the
  following properties:
    - `dest`: A [reference](#references) to the target (child or collection) to
      which the capability is being offered, `#<target-name>`.
    - `as` *(optional)*: The explicit [target path](#capability-paths) for the
      capability. If omitted, defaults to the source path. This path cannot be
      used for storage capabilities.

Example:

```
"offer": [
    {
        "service": "/svc/fuchsia.log.Log",
        "from": "#logger",
        "to": [
            { "dest": "#fshost" },
            { "dest": "#pkg_cache" },
        ],
    },
    {
        "directory": "/data/blobfs",
        "from": "self",
        "to": [
            { "dest": "#pkg_cache", "as": "/blobfs" },
        ],
    },
    {
        "directory": "/data",
        "from": "realm",
        "to": [
            { "dest": "#fshost" },
        ],
    },
    {
        "storage": "meta",
        "from": "realm",
        "to": [
            { "dest": "#logger" },
        ],
    },
],
```

### storage

A `storage` declaration creates three storage capabilities, for "data", "cache",
and "meta" storage. These storage capabilities are backed by a preexisting
directory capability, as explained in [Storage
capabilities](#storage-capabilities).

`storage` is an array of objects with the following properties:

- `name`: A name for this storage section which can be used by an `offer`.
- `from`: The source of the directory capability backing the new storage
  capabilities, one of:
    - `realm`: The component's containing realm (parent).
    - `self`: This component.
    - `#<child-name>`: A [reference](#references) to a child component instance.
- `path`: The [source path](#capability-paths) of a directory capability.

### facets

The `facets` section is a JSON object containing [facets](#facet-metadata),
chunks of metadata which components may interpret for their own purposes. The
component framework enforces no schema for this section, but third parties may
expect their facets to adhere to a particular schema.

This section may be omitted.

### References

A *reference* is a string of the form `#<reference-name>`, where
`<reference-name>` is a string of one or more of the following characters:
`a-z`, `0-9`, `_`, `.`, `-`.

A reference may refer to:

- A [static child instance](#child-component-instances) whose name is
  `<reference-name>`.
- A [collection](#component-collections) whose name is `<reference-name>`.
- A [storage declaration](#storage) whose name is `<reference-name>`.
