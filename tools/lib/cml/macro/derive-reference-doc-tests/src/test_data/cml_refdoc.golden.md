# Component manifest (`.cml`) reference

A `.cml` file contains a single json5 object literal with the keys below.

Where string values are expected, a list of valid values is generally documented.
The following string value types are reused and must follow specific rules.

# String types

## Names {#names}

Both capabilities and a component's children are named. A name string must consist of one or
more of the following characters: `a-z`, `0-9`, `_`, `.`, `-`.

## References {#references}

A reference string takes the form of `#<name>`, where `<name>` refers to the name of a child:

-   A [static child instance][doc-static-children] whose name is
    `<name>`, or
-   A [collection][doc-collections] whose name is `<name>`.

[doc-static-children]: /docs/concepts/components/v2/realms.md#static-children
[doc-collections]: /docs/concepts/components/v2/realms.md#collections

## Top-level keys

### `include` {#include}

_array of `strings` (optional)_

The optional `include` property describes zero or more other component manifest
files to be merged into this component manifest. For example:

```json5
include: [ "syslog/client.shard.cml" ]
```

In the example given above, the component manifest is including contents from a
manifest shard provided by the `syslog` library, thus ensuring that the
component functions correctly at runtime if it attempts to write to `syslog`. By
convention such files are called "manifest shards" and end with `.shard.cml`.

Include paths prepended with `//` are relative to the source root of the Fuchsia
checkout. However, include paths not prepended with `//`, as in the example
above, are resolved from Fuchsia SDK libraries (`//sdk/lib`) that export
component manifest shards.

For reference, inside the Fuchsia checkout these two include paths are
equivalent:

* `syslog/client.shard.cml`
* `//sdk/lib/syslog/client.shard.cml`

You can review the outcome of merging any and all includes into a component
manifest file by invoking the following command:

Note: The `fx` command below is for developers working in a Fuchsia source
checkout environment.

```sh
fx cmc include {{ "<var>" }}cmx_file{{ "</var>" }} --includeroot $FUCHSIA_DIR --includepath $FUCHSIA_DIR/sdk/lib
```

Includes are transitive, meaning that shards can have their own includes.

Include paths can have diamond dependencies. For instance this is valid:
A includes B, A includes C, B includes D, C includes D.
In this case A will transitively include B, C, D.

Include paths cannot have cycles. For instance this is invalid:
A includes B, B includes A.
A cycle such as the above will result in a compile-time error.

### `program` {#program}

_`object` (optional)_

Components that are executable include a `program` section. The `program`
section must set the `runner` property to select a [runner][doc-runners] to run
the component. The format of the rest of the `program` section is determined by
that particular runner.

#### ELF runners {#elf-runners}

If the component uses the ELF runner, `program` must include the following
properties, at a minimum:

-   `runner`: must be set to `"elf"`
-   `binary`: Package-relative path to the executable binary
-   `args` _(optional)_: List of arguments

Example:

```json5
program: {
    runner: "elf",
    binary: "bin/hippo",
    args: [ "Hello", "hippos!" ],
},
```

For a complete list of properties, see: [ELF Runner](/docs/concepts/components/v2/elf_runner.md)

#### Other runners {#other-runners}

If a component uses a custom runner, values inside the `program` stanza other
than `runner` are specific to the runner. The runner receives the arguments as a
dictionary of key and value pairs. Refer to the specific runner being used to
determine what keys it expects to receive, and how it interprets them.

[doc-runners]: /docs/concepts/components/v2/capabilities/runners.md

### `children` {#children}

_array of `objects` (optional)_

The `children` section declares child component instances as described in
[Child component instances][doc-children].

Keys:

-   `name`: The name of the child component instance, which is a string of one
    or more of the following characters: `a-z`, `0-9`, `_`, `.`, `-`. The name
    identifies this component when used in a [reference](#references).
-   `url`: The [component URL][component-url] for the child component instance.
-   `startup` _(optional)_: The component instance's startup mode.
    -   `lazy` _(default)_: Start the component instance only if another
        component instance binds to it.
    -   [`eager`][doc-eager]: Start the component instance as soon as its parent
        starts.
-   `environment` _(optional)_: If present, the name of the environment to be
    assigned to the child component instance, one of
    [`environments`](#environments). If omitted, the child will inherit the same
    environment assigned to this component.
-   `on_terminate` _(optional)_: Determines the fault recovery policy to apply
    if this component terminates.
    -   `none` _(default)_: Do nothing.
    -   `reboot`: Gracefully reboot the system if the component terminates for
        any reason. This is a special feature for use only by a narrow set of
        components; see [Termination policies][doc-reboot-on-terminate] for more
        information.

Example:

```json5
children: [
    {
        name: "logger",
        url: "fuchsia-pkg://fuchsia.com/logger#logger.cm",
    },
    {
        name: "pkg_cache",
        url: "fuchsia-pkg://fuchsia.com/pkg_cache#meta/pkg_cache.cm",
        startup: "eager",
    },
    {
        name: "child",
        url: "#meta/child.cm",
    }
],
```

[doc-children]: /docs/concepts/components/v2/realms.md#child-component-instances
[component-url]: /docs/concepts/components/component_urls.md
[doc-eager]: /docs/concepts/components/v2/lifecycle.md#eager_binding
[doc-reboot-on-terminate]: /docs/concepts/components/v2/termination_policies.md#reboot-on-terminate

### `collections` {#collections}

_array of `objects` (optional)_

The `collections` section declares collections as described in
[Component collections][doc-collections].

Keys:

-   `name`: The name of the component collection, which is a string of one or
    more of the following characters: `a-z`, `0-9`, `_`, `.`, `-`. The name
    identifies this collection when used in a [reference](#references).
-   `durability`: The duration of child component instances in the collection.
    -   `transient`: The instance exists until its parent is stopped or it is
        explicitly destroyed.
    -   `single_run`: The instance is started when it is created, and destroyed
        when it is stopped.
-   `environment` _(optional)_: If present, the environment that will be
    assigned to instances in this collection, one of
    [`environments`](#environments). If omitted, instances in this collection
    will inherit the same environment assigned to this component.

Example:

```json5
collections: [
    {
        name: "tests",
        durability: "transient",
    },
],
```

### `environments` {#environments}

_array of `objects` (optional)_

The `environments` section declares environments as described in
[Environments][doc-environments].

Keys:

-   `name`: The name of the environment, which is a string of one or more of the
    following characters: `a-z`, `0-9`, `_`, `.`, `-`. The name identifies this
    environment when used in a [reference](#references).
-   `extend`: How the environment should extend this realm's environment.
    -   `realm`: Inherit all properties from this compenent's environment.
    -   `none`: Start with an empty environment, do not inherit anything.
-   `runners`: The runners registered in the environment. An array of objects
    with the following properties:
    -   `runner`: The [name](#name) of a runner capability, whose
        source is specified in `from`.
    -   `from`: The source of the runner capability, one of:
        -   `parent`: The component's parent.
        -   `self`: This component.
        -   `#<child-name>`: A [reference](#references) to a child component
            instance.
    -   `as` _(option)_: An explicit name for the runner as it will be known in
        this environment. If omitted, defaults to `runner`.
-   `resolvers`: The resolvers registered in the environment. An array of
    objects with the following properties:
    -   `resolver`: The [name](#name) of a resolver capability,
        whose source is specified in `from`.
    -   `from`: The source of the resolver capability, one of:
        -   `parent`: The component's parent.
        -   `self`: This component.
        -   `#<child-name>`: A [reference](#references) to a child component
            instance.
    -   `scheme`: The URL scheme for which the resolver should handle
        resolution.

Example:

```json5
environments: [
    {
        name: "test-env",
        extend: "realm",
        runners: [
            {
                runner: "gtest-runner",
                from: "#gtest",
            },
        ],
        resolvers: [
            {
                resolver: "universe-resolver",
                from: "parent",
                scheme: "fuchsia-pkg",
            },
        ],
    },
],
```

[doc-environments]: /docs/concepts/components/v2/environments.md

### `capabilities` {#capabilities}

_array of `objects` (optional)_

The `capabilities` section defines capabilities that are provided by this component.
Capabilities that are [offered](#offer) or [exposed](#expose) from `self` must be declared
here.

These capabilities often map to a node in the
[outgoing directory][glossary.outgoing directory].

Each entry in `capabilities` must conform to one of the following schemas:

-   [`protocol`](#capability-protocol)
-   [`directory`](#capability-directory)
-   [`storage`](#capability-storage)
-   [`runner`](#capability-runner)
-   [`resolver`](#capability-resolver)

#### protocol {#capability-protocol}

A definition of a [protocol capability][doc-protocol].

-   `protocol`: The [name](#name) for this protocol capability, or
    an array of names to define multiple protocols.
-   `path` _(optional)_: The path in the component's outgoing directory from
    which this protocol is served. Only supported when `protocol` is a single
    name. Defaults to `/svc/${protocol}`.

#### directory {#capability-directory}

A definition of a [directory capability][doc-directory].

-   `directory`: The [name](#name) for this directory capability.
-   `subdir` _(optional)_: A subdirectory within the source component's
    directory capability. This will expose only the given subdirectory as the
    root of the target directory capability. If absent, the source directory's
    root will be exposed.
-   `path`: The path in the component's outgoing directory from which this
    directory is served.
-   `rights`: The maximum [directory rights][doc-directory-rights] that may be set
    when using this directory.

#### storage {#capability-storage}

A definition of a [storage capability][doc-storage].

-   `storage`: The [name](#name) for this storage capability.
-   `from`: The source of the existing directory capability backing this storage
    capability, one of:
    -   `parent`: The component's parent.
    -   `self`: This component.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
-   `backing_dir`: The [name](#name) of the directory backing the
    storage.
-   `subdir`: The subdirectory within `backing_dir` where per-component isolated
    storage directories are created.
-   `storage_id`: The identifier used to isolated storage for a component, one
    of:
    -   `static_instance_id`: The instance ID in the component ID index is used
        as the key for a component's storage. Components which are not listed in
        the component ID index will not be able to use this storage capability.
    -   `static_instance_id_or_moniker`: If the component is listed in the
        component ID index, the instance ID is used as the key for a component's
        storage. Otherwise, the component's relative moniker from the storage
        capability is used.

#### runner {#capability-runner}

A definition of a [runner capability][doc-runners].

-   `runner`: The [name](#name) for this runner capability.
-   `path`: The path in the component's outgoing directory from which the
    `fuchsia.component.runner.ComponentRunner` protocol is served.

#### resolver {#capability-resolver}

A definition of a [resolver capability][doc-resolvers].

-   `resolver`: The [name](#name) for this resolver capability.
-   `path`: The path in the component's outgoing directory from which the
    `fuchsia.sys2.ComponentResolver` protocol is served.

[doc-protocol]: /docs/concepts/components/v2/capabilities/protocol.md
[doc-directory]: /docs/concepts/components/v2/capabilities/directory.md
[doc-storage]: /docs/concepts/components/v2/capabilities/storage.md
[doc-resolvers]: /docs/concepts/components/v2/capabilities/resolvers.md
[doc-runners]: /docs/concepts/components/v2/capabilities/runners.md
[doc-event]: /docs/concepts/components/v2/capabilities/event.md
[doc-directory-rights]: /docs/concepts/components/v2/capabilities/directory#directory-capability-rights
[glossary.outgoing directory]: /docs/glossary/README.md#outgoing-directory

### `use` {#use}

_array of `objects` (optional)_

For executable components, declares capabilities that this
component requires in its [namespace][glossary.namespace] at runtime.
Capabilities are routed from the `parent` unless otherwise specified,
and each capability must have a valid route through all components between
this component and the capability's source.

Keys:

-   A capability declaration, one of:
    -   `protocol`: The [name](#name) of a [protocol capability][doc-protocol],
        or an array of names.
    -   `directory`: The [name](#name) of a [directory capability][doc-directory].
    -   `storage`: The [name](#name) of a [storage capability][doc-storage].
    -   `event`: The [name](#name) of an [event capability][doc-event],
        or an array of names.
-   `from` _(optional)_: The source of the capability. Defaults to `parent`.
    One of:
    -   `parent`: The component's parent.
    -   `debug`: One of [`debug_capabilities`][fidl-environment-decl] in the
        environment assigned to this component.
    -   `framework`: The Component Framework runtime.
    -   `self`: This component.
    -   `#<capability-name>`: The name of another capability from which the
        requested capability is derived.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
-   `path` _(optional)_: The path at which to install the capability in the
    component's namespace. For protocols, defaults to `/svc/${protocol}`.
    Required for `directory` and `storage`. This property is disallowed for
    declarations with capability arrays.

Example:

```json5
use: [
    {
        protocol: [
            "fuchsia.ui.scenic.Scenic",
            "fuchsia.accessibility.Manager",
        ]
    },
    {
        directory: "themes",
        path: "/data/themes",
        rights: [ "r*" ],
    },
    {
        storage: "persistent",
        path: "/data",
    },
    {
        event: [
            "started",
            "stopped",
        ],
        from: "framework",
    },
],
```

[fidl-environment-decl]: /reference/fidl/fuchsia.component.decl#Environment
[glossary.namespace]: /docs/glossary/README.md#namespace

### `expose` {#expose}

_array of `objects` (optional)_

Declares the capabilities that are made available to the parent component or to the
framework. It is valid to `expose` from `self` or from a child component.

Keys:

-   A capability declaration, one of:
    -   `protocol`: The [name](#name) of a [protocol capability][doc-protocol],
        or an array of names.
    -   `directory`: The [name](#name) of a [directory capability][doc-directory],
        or an array of names.
    -   `runner`: The [name](#name) of a [runner capability][doc-runners],
        or an array of names.
    -   `resolver`: The [name](#name) of a [resolver capability][doc-resolvers],
        or an array of names.
-   `from`: The source of the capability, one of:
    -   `self`: This component. Requires a corresponding
        [`capability`](#capabilities) declaration.
    -   `framework`: The Component Framework runtime.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
-   `to` _(optional)_: The capability target. Either `parent` or `framework`.
    Defaults to `parent`.
-   `as` _(optional)_: The [name](#name) for the capability as it
    will be known by the target. If omitted, defaults to the original name. This
    property cannot be used when `protocol` is an array of multiple items. `as`
    cannot be used when an array of multiple names is provided.

Example:

```json5
expose: [
    {
        directory: "themes",
        from: "self",
    },
    {
        protocol: "pkg.Cache",
        from: "#pkg_cache",
        as: "fuchsia.pkg.PackageCache",
    },
    {
        protocol: [
            "fuchsia.ui.app.ViewProvider",
            "fuchsia.fonts.Provider",
        ],
        from: "self",
    },
    {
        runner: "web-chromium",
        from: "#web_runner",
        as: "web",
    },
    {
        resolver: "universe-resolver",
        from: "#universe_resolver",
    },
],
```

### `offer` {#offer}

_array of `objects` (optional)_

Declares the capabilities that are made available to a [child component][doc-children]
instance or a [child collection][doc-collections].

Keys:

-   A capability declaration, one of:
    -   `protocol`: The [name](#name) of a [protocol capability][doc-protocol],
        or an array of names.
    -   `directory`: The [name](#name) of a [directory capability][doc-directory],
        or an array of names.
    -   `storage`: The [name](#name) of a [storage capability][doc-storage],
        or an array of names.
    -   `runner`: The [name](#name) of a [runner capability][doc-runners],
        or an array of names.
    -   `resolver`: The [name](#name) of a [resolver capability][doc-resolvers],
        or an array of names.
    -   `event`: The [name](#name) of an [event capability][doc-event],
        or an array of names.
-   `from`: The source of the capability, one of:
    -   `parent`: The component's parent. This source can be used for all
        capability types.
    -   `self`: This component. Requires a corresponding
        [`capability`](#capabilities) declaration.
    -   `framework`: The Component Framework runtime.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance. This source can only be used when offering protocol,
        directory, or runner capabilities.
-   `to`: A capability target or array of targets, each of which is a
    [reference](#references) to the child or collection to which the capability
    is being offered, of the form `#<target-name>`.
-   `as` _(optional)_: An explicit [name](#name) for the capability
    as it will be known by the target. If omitted, defaults to the original
    name. `as` cannot be used when an array of multiple names is provided.
-   `dependency` _(optional)_: The type of dependency between the source and
    targets, one of:
    -   `strong`: a strong dependency, which is used to determine shutdown
        ordering. Component manager is guaranteed to stop the target before the
        source. This is the default.
    -   `weak_for_migration`: a weak dependency, which is ignored during
        shutdown. When component manager stops the parent realm, the source may
        stop before the clients. Clients of weak dependencies must be able to
        handle these dependencies becoming unavailable. This type exists to keep
        track of weak dependencies that resulted from migrations into v2
        components.

Example:

```json5
offer: [
    {
        protocol: "fuchsia.logger.LogSink",
        from: "#logger",
        to: [ "#fshost", "#pkg_cache" ],
        dependency: "weak_for_migration",
    },
    {
        protocol: [
            "fuchsia.ui.app.ViewProvider",
            "fuchsia.fonts.Provider",
        ],
        from: "#session",
        to: [ "#ui_shell" ],
        dependency: "strong",
    },
    {
        directory: "blobfs",
        from: "self",
        to: [ "#pkg_cache" ],
    },
    {
        directory: "fshost-config",
        from: "parent",
        to: [ "#fshost" ],
        as: "config",
    },
    {
        storage: "cache",
        from: "parent",
        to: [ "#logger" ],
    },
    {
        runner: "web",
        from: "parent",
        to: [ "#user-shell" ],
    },
    {
        resolver: "universe-resolver",
        from: "parent",
        to: [ "#user-shell" ],
    },
    {
        event: "stopped",
        from: "framework",
        to: [ "#logger" ],
    },
],
```

### `facets` {#facets}

_`object` (optional)_

Contains metadata that components may interpret for their own purposes. The component
framework enforces no schema for this section, but third parties may expect their facets to
adhere to a particular schema.

### `config` {#config}

_`object` (optional)_

A JSON object containing all configuration fields for this component. Each field must have
a key and a value type.

NOTE: This feature is currently experimental and access is controlled through an allowlist
in fuchsia.git at `//tools/cmc/build/restricted_features/BUILD.gn`.

Example:

```json5
config: {
    debug_mode: {
        type: "bool"
    },
    tags: {
        type: "vector",
        max_count: 10,
        element: {
            type: "string",
            max_size: 20
        }
    }
}
```
TODO(fxbug.dev/87560): Write detailed syntax for the `config` section

