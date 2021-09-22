# Component manifests {#component-manifests}

<<../_v2_banner.md>>

A [component manifest](#component-manifest) is a file that defines a component
by encoding a [component declaration](#component-declaration). This document
gives an overview of the concepts used by component declarations, and presents
the syntax for writing [component manifest source](#component-manifest-source).
Component declarations contain:

-   Information about [how to run the component](#runtime).
-   The component's [child component instances][doc-children] and
    [component collections][doc-collections].
-   [Routing rules](#capability-routing) that describe how capabilities are
    used, exposed, and offered between components.
-   [Freeform data ("facets")](#facet-metadata), which is ignored by the
    component framework but can be interpreted by third parties.

## Manifests and declarations {#manifests-and-declarations}

This section explains the distinction between component manifests, component
manifest sources, and component declarations.

### Component manifest source {#component-manifest-source}

A [component manifest source][glossary.component manifest source] is a file that
encodes part of a component manifest. Component manifest sources are written in
component manifest language (CML), which is the developer-facing source format
for component manifests. CML files are JSON5 files that end with a `.cml`
extension. Descriptions and examples of the CML syntax are contained in this
document: see [Syntax](#syntax).

Component manifest sources are compiled to
[component manifests](#component-manifest) by the [`cmc`][src-cmc] tool.

### Component manifest {#component-manifest}

A [component manifest][glossary.component manifest] is a file that encodes a
[component declaration](#component-declaration), usually distributed as part of
a [package][glossary.package]. The binary format is a persisted FIDL file
mapping one-to-one onto the component declaration, typically ending in a `.cm`
extension.

A [fuchsia-pkg URL][doc-package-url] with a component manifest resource path
identifies a component in a package.

### Component declaration {#component-declaration}

A [component declaration][glossary.component declaration] describes what a
component can do, the capabilities it uses and exposes, its children, and other
information needed to run the component. Component declarations are represented
using the [`ComponentDecl`][fidl-component-decl] FIDL table.

A component [resolver][doc-resolvers] retrieves the component declaration and
provides it to the component framework.

## Concepts {#concepts}

### Runtime {#runtime}

The component framework doesn't dictate a particular format for programs, but
instead requires components to specify which runtime they need by specifying a
[runner][doc-runners]. The component framework provides a built-in
[ELF runner](elf_runner.md), while other runtimes are implemented as components
within the framework. A component can specify any runner available in its
[environment][doc-environments].

The [`program`](#program) section of a component manifest designates a runner
and declares to the runner how the component is run, such as the program
location and any arguments. Components using the ELF runner should specify the
binary name and arguments, see [ELF Runner](elf_runner.md).
[Other runners][doc-runners] may have other runner-specific details, documented
by that runner.

A component may also have no runtime at all by omitting the `program` section.
In this case, the component may still route capabilities and host children, but
no code will be executed for the component.

See also: [ELF Runner](elf_runner.md), [Component Runners][doc-runners]

### Capability routing {#capability-routing}

Component manifests provide a syntax for routing capabilities between
components. For a detailed walkthrough about what happens during capability
routing, see the [Capabilities overview][doc-capabilities] and
[Life of a protocol open][doc-protocol-open].

#### Capability names {#capability-names}

Every capability in a `.cml` has a name so that it can be referred to in routing
declarations. A capability name consists of a string containing the characters
`a` to `z`, `A` to `Z`, `0` to `9`, underscore (`_`), hyphen (`-`), or the full
stop character (`.`).

#### Capability types {#capability-types}

The following capabilities can be routed:

| type        | description                   | routed to                     |
| ----------- | ----------------------------- | ----------------------------- |
| `protocol`  | A filesystem node that is     | components                    |
:             : used to open a channel backed :                               :
:             : by a FIDL protocol.           :                               :
| `service`   | A filesystem directory that   | components                    |
:             : is used to open a channel to  :                               :
:             : one of several                :                               :
:             : [service][doc-service]        :                               :
:             : instances.                    :                               :
| `directory` | A filesystem directory.       | components                    |
| `storage`   | A writable filesystem         | components                    |
:             : directory that is isolated to :                               :
:             : the component using it.       :                               :
| `resolver`  | A capability that, when       | [environments](#environments) |
:             : registered in an              :                               :
:             : [environment](#environments), :                               :
:             : causes a component with a     :                               :
:             : particular URL scheme to be   :                               :
:             : resolved with that            :                               :
:             : [resolver][doc-resolvers].    :                               :
| `runner`    | A capability that, when       | [environments](#environments) |
:             : registered in an              :                               :
:             : [environment](#environments), :                               :
:             : allows the framework to use   :                               :
:             : that [runner][doc-runners]    :                               :
:             : when starting components.     :                               :

#### Routing terminology {#routing-terminology}

Routing terminology divides into the following categories:

1.  Declarations of how capabilities are routed between the component, its
    parent, and its children:
    -   `offer`: Declares that the capability listed is made available to a
        [child component][doc-children] instance or a [child
        collection][doc-collections].
    -   `expose`: Declares that the capabilities listed are made available to
        the parent component or to the framework. It is valid to `expose` from
        `self` or from a child component.
1.  Declarations of capabilities consumed or provided by the component:
    -   `use`: For executable components, declares capabilities that this
        component requires in its [namespace][glossary.namespace] at runtime.
        Capabilities are routed from the `parent` unless otherwise specified,
        and each capability must have a valid route from its source.
    -   `capabilities`: Declares capabilities that this component provides.
        Capabilities that are offered or exposed from `self` must appear here.
        These capabilities often map to a node in the
        [outgoing directory][glossary.outgoing directory].

#### Framework protocols {#framework-protocols}

A *framework protocol* is a protocol provided by the component framework.
Because the component framework itself provides the protocol, any component may
`use` it without an accompanying `offer` from its parent. Fuchsia supports the
following framework protocols:

-   [`fuchsia.sys2.Realm`][fidl-realm]: Allows a component to manage and bind to
    its children. Scoped to the component's realm.
-   [`fuchsia.component.Binder`][fidl-binder]: Allows a component to start
    another component.

#### Framework directories {#framework-directories}

A *framework directory* is a directory provided by the component framework.
Because the component framework itself is the provider of the directory, any
component may `use` it without an explicit `offer`. Fuchsia supports the
following framework directories:

-   [hub][glossary.hub]: Allows a component to perform runtime introspection of
    itself and its children.

#### Directory rights {#directory-rights}

Directory rights define how a directory may be accessed in the component
framework. You must specify directory rights on `use` declarations and on
`expose` and `offer` declarations from `self`. On `expose` and `offer`
declarations not from `self`, they are optional.

A *rights* field can be defined by the combination of any of the following
rights tokens:

```json5
rights: ["connect", "enumerate", "read_bytes", "write_bytes", "execute_bytes",
         "update_attributes", "get_attributes", "traverse", "modify_directory"]
```

See [`fuchsia.io2.Rights`][fidl-io2-rights] for the equivalent FIDL definitions.

However *rights aliases* should be preferred where possible for clarity.

```json5
rights: ["r*", "w*", "x*", "rw*", "rx*"]
```

Except in special circumstances you will almost always want either `["r*"]` or
`["rw*"]`. Only one alias can be provided to a rights field and it must not
conflict with any longform rights.

Right aliases are simply expanded into their longform counterparts:

```
"r*" -> ["connect", "enumerate", "traverse", "read_bytes", "get_attributes"]
"w*" -> ["connect", "enumerate", "traverse", "write_bytes", "update_attributes", "modify_directory"]
"x*" -> ["connect", "enumerate", "traverse", "execute_bytes"]
```

Merged aliases like `rw*` are simply `r*` and `w*` merged without duplicates.

This example shows usage of a directory use declaration annotated with rights:

```json5
use: [
    {
        directory: "test",
        from: "parent",
        rights: ["rw*", "admin"],
        path: "/data/test",
    },
],
```

#### Examples {#examples}

For an example of how these keywords interact, consider the following component
instance tree:

<br>![Capability routing example](images/capability_routing_example.png)<br>

In this example, the `echo` component instance provides an `fuchsia.Echo`
protocol in its outgoing directory. This protocol is routed to the `echo_tool`
component instance, which uses it. It is necessary for each component instance
in the routing path to propagate `fuchsia.Echo` to the next component instance.

The routing sequence is:

-   `echo` hosts the `fuchsia.Echo` protocol in its outgoing directory. Also, it
    exposes `fuchsia.Echo` from `self` so the protocol is visible to its parent,
    `services`.
-   `services` exposes `fuchsia.Echo` from its child `echo` to its parent,
    `shell`.
-   `system` offers `fuchsia.Echo` from its child `services` to its other child
    `tools`.
-   `tools` offers `fuchsia.Echo` from `parent` (i.e., its parent) to its child
    `echo_tool`.
-   `echo_tool` uses `fuchsia.Echo`. When `echo_tool` runs, it will find
    `fuchsia.Echo` in its namespace.

A working example of capability routing can be found at
[//examples/components/routing][examples-routing].

### Facet metadata {#facet-metadata}

*Facets* are metadata that is ignored by the component framework itself, but may
be interpreted by interested components.

## Syntax {#syntax}

This section describes the syntax for each section of the component manifest, in
CML format.

### References {#references}

A *reference* is a string of the form `#<reference-name>`, where
`<reference-name>` is a string of one or more of the following characters:
`a-z`, `0-9`, `_`, `.`, `-`.

A reference may refer to:

-   A [static child instance][doc-static-children] whose name is
    `<reference-name>`.
-   A [collection][doc-collections] whose name is `<reference-name>`.

### include {#include}

The optional `include` property describes zero or more other component manifest
files to be merged into this component manifest. For example:

```json5
include: [ "//src/lib/syslog/client.shard.cml" ]
```

In the example given above, the component manifest is including contents from a
manifest shard provided by the `syslog` library, thus ensuring that the
component functions correctly at runtime if it attempts to write to syslog. By
convention such files are called "manifest shards" and end with `.shard.cml`.

If working in fuchsia.git, include paths are relative to the source root of the
Fuchsia tree.

You can review the outcome of merging any and all includes into a component
manifest file by invoking the following command:

```sh
fx cmc include {{ "<var>" }}cmx_file{{ "</var>" }} --includeroot $FUCHSIA_DIR --includepath $FUCHSIA_DIR/sdk/lib
```

Includes can be recursive, meaning that shards can have their own includes.

### program {#program}

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

For a complete list of properties, see: [ELF Runner](elf_runner.md)

#### Other runners {#other-runners}

If a component uses a custom runner, values inside the `program` stanza other
than `runner` are specific to the runner. The runner receives the arguments as a
dictionary of key and value pairs. Refer to the specific runner being used to
determine what keys it expects to receive, and how it interprets them.

### children {#children}

The `children` section declares child component instances as described in
[Child component instances][doc-children].

`children` is an array of objects where each object has the following
properties:

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

### collections {#collections}

The `collections` section declares collections as described in
[Component collections][doc-collections].

`collections` is an array of objects where each object has the following
properties:

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

### environments {#environments}

The `environments` section declares environments as described in
[Environments][doc-environments].

`environments` is an array of objects where each object has the following
properties:

-   `name`: The name of the environment, which is a string of one or more of the
    following characters: `a-z`, `0-9`, `_`, `.`, `-`. The name identifies this
    environment when used in a [reference](#references).
-   `extend`: How the environment should extend this realm's environment.
    -   `realm`: Inherit all properties from this compenent's environment.
    -   `none`: Start with an empty environment, do not inherit anything.
-   `runners`: The runners registered in the environment. An array of objects
    with the following properties:
    -   `runner`: The [name](#capability-names) of a runner capability, whose
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
    -   `resolver`: The [name](#capability-names) of a resolver capability,
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

### capabilities {#capabilities}

The `capabilities` section defines capabilities that are provided by this
component. Capabilities that are offered or exposed from `self` must be declared
here.

`capabilities` is an array of objects of any of the following types:

-   [`protocol`](#capability-protocol)
-   [`directory`](#capability-directory)
-   [`storage`](#capability-storage)
-   [`runner`](#capability-runner)
-   [`resolver`](#capability-resolver)

#### protocol {#capability-protocol}

A definition of a [protocol capability][doc-protocol].

-   `protocol`: The [name](#capability-names) for this protocol capability, or
    an array of names to define multiple protocols.
-   `path` _(optional)_: The path in the component's outgoing directory from
    which this protocol is served. Only supported when `protocol` is a single
    name. Defaults to `/svc/${protocol}`.

#### directory {#capability-directory}

A definition of a [directory capability][doc-directory].

-   `directory`: The [name](#capability-names) for this directory capability.
-   `subdir` _(optional)_: A subdirectory within the source component's
    directory capability. This will expose only the given subdirectory as the
    root of the target directory capability. If absent, the source directory's
    root will be exposed.
-   `path`: The path in the component's outgoing directory from which this
    directory is served.
-   `rights`: The maximum [directory rights](#directory-rights) that may be set
    when using this directory.

#### storage {#capability-storage}

A definition of a [storage capability][doc-storage].

-   `storage`: The [name](#capability-names) for this storage capability.
-   `from`: The source of the existing directory capability backing this storage
    capability, one of:
    -   `parent`: The component's parent.
    -   `self`: This component.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
-   `backing_dir`: The [name](#capability-names) of the directory backing the
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

-   `runner`: The [name](#capability-names) for this runner capability.
-   `path`: The path in the component's outgoing directory from which the
    `fuchsia.component.runner.ComponentRunner` protocol is served.

#### resolver {#capability-resolver}

A definition of a [resolver capability][doc-resolvers].

-   `resolver`: The [name](#capability-names) for this resolver capability.
-   `path`: The path in the component's outgoing directory from which the
    `fuchsia.sys2.ComponentResolver` protocol is served.

### use {#use}

The `use` section declares the capabilities that the component can use at
runtime, as explained in [Routing terminology](#routing-terminology).

`use` is an array of objects with the following properties:

-   A capability declaration, one of:
    -   `protocol`: The [name](#capability-names) of a protocol capability, or
        an array of names of protocol capabilities.
    -   `directory`: The [name](#capability-names) of a directory capability.
    -   `storage`: The [name](#capability-names) of a storage capability.
-   `from` _(optional)_: The source of the capability. Defaults to `parent`. One
    of:
    -   `parent`: The component's parent.
    -   `debug`: One of [`debug_capabilities`][fidl-environment-decl] in the
        environment assigned to this component.
    -   `framework`: The Component Framework runtime.
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
],
```

### expose {#expose}

The `expose` section declares the capabilities exposed by this component, as
explained in [Routing terminology](#routing-terminology).

`expose` is an array of objects with the following properties:

-   A [capability name](#capability-names) or array of names, keyed to one of
    the following typenames:
    -   `protocol`
    -   `directory`
    -   `runner`
    -   `resolver`
-   `from`: The source of the capability, one of:
    -   `self`: This component. Requires a corresponding
        [`capability`](#capabilities) declaration.
    -   `framework`: The Component Framework runtime.
    -   `#<capability-name>`: The name of another capability from which the
        exposed capability is derived.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
-   `to` _(optional)_: The capability target. Either `parent` or `framework`.
    Defaults to `parent`.
-   `as` _(optional)_: The [name](#capability-names) for the capability as it
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

### offer {#offer}

The `offer` section declares the capabilities offered by this component, as
explained in [Routing terminology](#routing-terminology).

`offer` is an array of objects with the following properties:

-   A [capability name](#capability-names) or array of names, keyed to one of
    the following typenames:
    -   `protocol`
    -   `directory`
    -   `storage`
    -   `runner`
    -   `resolver`
-   `from`: The source of the capability, one of:
    -   `parent`: The component's parent. This source can be used for all
        capability types.
    -   `self`: This component. Requires a corresponding
        [`capability`](#capabilities) declaration.
    -   `framework`: The Component Framework runtime.
    -   `#<capability-name>`: The name of another capability from which the
        offered capability is derived.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance. This source can only be used when offering protocol,
        directory, or runner capabilities.
-   `to`: A capability target or array of targets, each of which is a
    [reference](#references) to the child or collection to which the capability
    is being offered, of the form `#<target-name>`.
-   `as` _(optional)_: An explicit [name](#capability-names) for the capability
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
],
```

### facets {#facets}

The `facets` section is a JSON object containing [facets](#facet-metadata),
chunks of metadata that components may interpret for their own purposes. The
component framework enforces no schema for this section, but third parties may
expect their facets to adhere to a particular schema.

This section may be omitted.

[component-url]: /docs/concepts/components/component_urls.md
[doc-capabilities]: /docs/concepts/components/v2/capabilities/README.md
[doc-children]: realms.md#child-component-instances
[doc-collections]: realms.md#collections
[doc-directory]: /docs/concepts/components/v2/capabilities/directory.md
[doc-eager]: lifecycle.md#eager_binding
[doc-environments]: environments.md
[doc-module-facets]: /docs/concepts/modular/module_facet.md
[doc-package-url]: /docs/concepts/packages/package_url.md
[doc-packages]: /docs/concepts/packages/package.md
[doc-protocol]: /docs/concepts/components/v2/capabilities/protocol.md
[doc-protocol-open]: /docs/concepts/components/v2/capabilities/life_of_a_protocol_open.md
[doc-realm-definitions]: realms.md#definitions
[doc-reboot-on-terminate]: termination_policies.md#reboot-on-terminate
[doc-resolvers]: /docs/concepts/components/v2/capabilities/resolvers.md
[doc-runners]: /docs/concepts/components/v2/capabilities/runners.md
[doc-static-children]: realms.md#static-children
[doc-service]: /docs/concepts/components/v2/capabilities/service.md
[doc-storage]: /docs/concepts/components/v2/capabilities/storage.md
[examples-routing]: /examples/components/routing
[fidl-component-decl]: /sdk/fidl/fuchsia.sys2/decls/component_decl.fidl
[fidl-environment-decl]: /sdk/fidl/fuchsia.sys2/decls/environment_decl.fidl
[fidl-io2-rights]: /sdk/fidl/fuchsia.io2/rights-abilities.fidl
[fidl-binder]: /sdk/fidl/fuchsia.component/binder.fidl
[fidl-realm]: /sdk/fidl/fuchsia.sys2/realm.fidl
[glossary.component declaration]: /docs/glossary/README.md#component-declaration
[glossary.component manifest]: /docs/glossary/README.md#component-manifest
[glossary.component manifest source]: /docs/glossary/README.md#component-manifest-source
[glossary.hub]: /docs/glossary/README.md#hub
[glossary.outgoing directory]: /docs/glossary/README.md#outgoing-directory
[glossary.namespace]: /docs/glossary/README.md#namespace
[glossary.package]: /docs/glossary/README.md#package
[src-cmc]: /tools/cmc
