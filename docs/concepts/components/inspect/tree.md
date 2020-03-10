# Inspect discovery and hosting

[Inspect][inspect] is a powerful diagnostics feature for Fuchsia Components.

This document describes the details about how components host their
Inspect data and how various tools discover that data.

## Hosting Inspect data

A component may expose Inspect data in the following ways:

* [`fuchsia.inspect.Tree`](#tree) (C++, Rust)
* [`VmoFile`](#vmofile) (Dart)
* [`fuchsia.inspect.deprecated.Inspect`](#deprecated) (Go)

Components do not need to reach out to other services in
order to expose Inspect data.

Components generally do not need to be concerned with which method is
being used, client libraries abstract out the specific mechanism for
hosting. Nearly all implementations will eventually use `Tree`.

Feature | `fuchsia.inspect.Tree` | `VmoFile` | `fuchsia.inspect.deprecated.Inspect` | Description
----|----|----|----|----
Non-lazy values | Yes | Yes | Yes | Components may record values such as strings and integers.
Lazy values | Yes | No | Yes | Components may generate values dynamically at read time.
Mutable tree | Yes | Yes | Yes | Components may modify the values stored in their output.
Post-mortem inspection | Yes | Yes | No | The values recorded by a component are available after that component exits.
Low-latency snapshots | Yes | Yes | No | A full snapshot of the data can be obtained with low-latency.
Consistent snapshots | Yes\* | Yes | No | The snapshot of the tree is guaranteed to represent its state at one point in time.

(\*: Each tree's snapshot is consistent, but inter-tree consistency is not guaranteed)

### `fuchsia.inspect.Tree` {#tree}

`fuchsia.inspect.Tree` supports all features of the Inspect API, and it
is the recommended way to expose Inspect data from components.

Components host a single service file named `fuchsia.inspect.Tree` under
a subdirectory called "diagnostics". Multiple such files may exist,
but they must be in separate subdirectories under "diagnostics".

#### Implementation

The `fuchsia.inspect.Tree` [FIDL File][tree-fidl] defines the protocol
that is hosted by components to expose their Inspect data.

The `Tree` protocol links together multiple [Inspect VMOs][vmo-format]
in a "tree of trees."

![Figure: Example of trees](tree-example.png)

In the above figure, the tree named "root" handles protocol
`fuchsia.inspect.Tree` for connections on the top-level service hosted
under `diagnostics/fuchsia.inspect.Tree`. Child trees may be enumerated
and opened using methods on the protocol. For example, "child B" may
not exist in memory until opened and read.

The protocol supports this behavior in the following ways:

* `GetContent`

  This method obtains the content of the tree, currently in the form of
  an [Inspect VMO][vmo-format]. By convention, calling this method on
  the root tree should return a VMO that will be continually updated
  with new data. The client should not need to re-read the content of
  the tree to read new values.

* `ListChildNames`

  This method accepts an iterator over which the names of children of
  the tree will be returned. For example, the tree in the figure above
  will return names "child A" and "child B" when run on the root tree.

* `OpenChild`

  This method accepts a request for `fuchsia.inspect.Tree` that will
  be bound to the tree specified by a given name. Using this method a
  client may iterate through all trees exposed over the root iterface.

### `VmoFile` {#vmofile}

Components may expose any number of [Inspect VMOs][inspect-vmo]
in their `out/diagnostics` directory ending in the file extension
`.inspect`. By convention, components expose their "root" inspect tree at
`diagnostics/root.inspect`.

Note: Reading services may not disambiguate the sources of data in
a component.

Components may choose to generate the content of the VMO when the file
is opened if they choose, however, there exists no mechanism to link
multiple trees created this way together. For this reason, lazy values
are not supported in the context of a single tree, either the entire
tree is generated dynamically or none of it is.

### `fuchsia.inspect.deprecated.Inspect` {#deprecated}

This deprecated interface is used by Go to expose Inspect data. While
`fuchsia.inspect.Tree` exposes a "tree of trees," this interface exposes
only a single tree where subtrees may be dynamically instantiated.

This interface is deprecated in favor of the VMO format hosted by Inspect
tree for the following reasons:

* VMO format supports low-latency snapshots without communicating with
the hosting program.
* VMO format snapshots are always consistent for the whole tree.
* VMO format supports postmortem inspection, all Inspect data using the
deprecated interface dies with the component.
* `Tree` protocol supports the same dynamic features as the deprecated
interface.

## Reading Inspect data

The two primary ways to read Inspect data are:

1. [iquery](#iquery)
2. The [Archivist](#archivist)

### iquery {#iquery}

[iquery][iquery] (Inspect Query) is the CLI for interacting with Inspect data.

`iquery`'s primary mode of operation takes a list of locations for Inspect
data and prints out the contains information. A location consists either
of the path to a `.inspect` file, or the path to a directory containing
`fuchsia.inspect.Tree`.

Note: If a directory contains both `Tree` and the deprecated `Inspect`
protocol, `Tree` is preferred by readers, and only its content will
be shown.

iquery's secondary mode of operation (triggered by `--find`) recursively
identifies locations for Inspect data from the given directorry path. The
two modes may be used together as follows:

```
iquery --recursive `iquery --find /hub | grep -v system_objects | grep component_name`
```

In the example above, `iquery` is run to find a list of Inspect
locations that do not contain "system\_objects" and that do contain
"component\_name". Then, `iquery` is run on the result of the first
filter to recursively list data in the matching locations. Internally,
this is how the `fx iquery` tool is implemented. You may instead write:

```
fx iquery component_name
```

### Archivist {#archivist}

The Fuchsia Diagnostics Platform, hosted by the [Archivist][archivist],
is responsible for monitoring and aggregating Inspect data on demand.

#### Collection under appmgr

When a component is running under appmgr, diagnostics
data is collected from its `out/diagnostics` directory.
A connection to this directory is provided to Archivist by the
[`ComponentEventProvider`][fidl-event-provider] protocol.

A separate component, called `observer.cmx`, serves the same purpose as
the Archivist but may be injected into tests. Observer allows tests to
find only their own diagnostics data, helping to make tests hermitic.

#### Collection under component\_manager

When running under component manager, diagnostics data will be made
available to the Archivist through a new event system. This mechanism
is still a work in progress.

TODO(47865): Update details when done.

#### Reading Inspect from the Archivist

The Archivist (and observer) host
[`fuchsia.diagnostics.Archive`][archive], which provides the `ReadInspect`
method to obtain Inspect data from running components.

[archive]: /sdk/fidl/fuchsia.diagnostics/reader.fidl
[archivist]: /src/diagnostics/archivist
[fidl-event-provider]: /sdk/fidl/fuchsia.sys.internal/component_event_provider.fidl
[inspect]: /docs/development/inspect/README.md
[iquery]: /docs/development/inspect/iquery.md
[tree-fidl]: /sdk/fidl/fuchsia.inspect/tree.fidl
[vmo-format]: vmo_format.md
