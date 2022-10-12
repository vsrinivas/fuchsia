# Component manifest (`.cml`) reference

A `.cml` file contains a single json5 object literal with the keys below.

Where string values are expected, a list of valid values is generally documented.
The following string value types are reused and must follow specific rules.

## String types

### Names {#names}

Both capabilities and a component's children are named. A name string must consist of one or
more of the following characters: `a-z`, `0-9`, `_`, `.`, `-`.

### References {#references}

A reference string takes the form of `#<name>`, where `<name>` refers to the name of a child:

-   A [static child instance][doc-static-children] whose name is
    `<name>`, or
-   A [collection][doc-collections] whose name is `<name>`.

[doc-static-children]: /docs/concepts/components/v2/realms.md#static-children
[doc-collections]: /docs/concepts/components/v2/realms.md#collections
[doc-protocol]: /docs/concepts/components/v2/capabilities/protocol.md
[doc-directory]: /docs/concepts/components/v2/capabilities/directory.md
[doc-storage]: /docs/concepts/components/v2/capabilities/storage.md
[doc-resolvers]: /docs/concepts/components/v2/capabilities/resolvers.md
[doc-runners]: /docs/concepts/components/v2/capabilities/runners.md
[doc-event]: /docs/concepts/components/v2/capabilities/event.md
[doc-service]: /docs/concepts/components/v2/capabilities/service.md
[doc-directory-rights]: /docs/concepts/components/v2/capabilities/directory#directory-capability-rights

## Top-level keys

### `include` {#include}

_array of `string` (optional)_

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

### `disable` {#disable}

_`object` (optional)_

The `disable` section disables certain features in a particular CML that are
otherwise enforced by cmc.

- `must_offer_protocol`: (_optional array of `string`_) Lists protocols for which the option "experimental_must_offer_protocol" is disabled.
- `must_use_protocol`: (_optional array of `string`_) Lists protocols for which the option "experimental_must_use_protocol" is disabled.

Example:

```json5
disable: {
    must_offer_protocol: [ "fuchsia.logger.LogSink", "fuchsia.component.Binder" ],
    must_use_protocol: [ "fuchsia.logger.LogSink" ],
}
```



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

_array of `object` (optional)_

The `children` section declares child component instances as described in
[Child component instances][doc-children].

[doc-children]: /docs/concepts/components/v2/realms.md#child-component-instances

- `name`: (_`string`_) The name of the child component instance, which is a string of one
  or more of the following characters: `a-z`, `0-9`, `_`, `.`, `-`. The name
  identifies this component when used in a [reference](#references).
- `url`: (_`string`_) The [component URL][component-url] for the child component instance.
- `startup`: (_`string`_) The component instance's startup mode. One of:
  -   `lazy` _(default)_: Start the component instance only if another
      component instance binds to it.
  -   [`eager`][doc-eager]: Start the component instance as soon as its parent
      starts.
- `on_terminate`: (_optional `string`_) Determines the fault recovery policy to apply if this component terminates.
  -   `none` _(default)_: Do nothing.
  -   `reboot`: Gracefully reboot the system if the component terminates for
      any reason. This is a special feature for use only by a narrow set of
      components; see [Termination policies][doc-reboot-on-terminate] for more
      information.
- `environment`: (_optional `string`_) If present, the name of the environment to be assigned to the child component instance, one
  of [`environments`](#environments). If omitted, the child will inherit the same environment
  assigned to this component.

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

[component-url]: /docs/concepts/components/component_urls.md
[doc-eager]: /docs/development/components/connect.md#eager
[doc-reboot-on-terminate]: /docs/development/components/connect.md#reboot-on-terminate



### `collections` {#collections}

_array of `object` (optional)_

The `collections` section declares collections as described in
[Component collections][doc-collections].

- `name`: (_`string`_) The name of the component collection, which is a string of one or
  more of the following characters: `a-z`, `0-9`, `_`, `.`, `-`. The name
  identifies this collection when used in a [reference](#references).
- `durability`: (_`string`_) The duration of child component instances in the collection.
  -   `transient`: The instance exists until its parent is stopped or it is
      explicitly destroyed.
  -   `single_run`: The instance is started when it is created, and destroyed
      when it is stopped.
- `environment`: (_optional `string`_) If present, the environment that will be
  assigned to instances in this collection, one of
  [`environments`](#environments). If omitted, instances in this collection
  will inherit the same environment assigned to this component.
- `allowed_offers`: (_optional `string`_) Constraints on the dynamic offers that target the components in this collection.
  Dynamic offers are specified when calling `fuchsia.component.Realm/CreateChild`.
  -   `static_only`: Only those specified in this `.cml` file. No dynamic offers.
      This is the default.
  -   `static_and_dynamic`: Both static offers and those specified at runtime
      with `CreateChild` are allowed.
- `allow_long_names`: (_optional `bool`_) Allow child names up to 1024 characters long instead of the usual 100 character limit.
  Default is false.
- `persistent_storage`: (_optional `bool`_) If set to `true`, the data in isolated storage used by dynamic child instances and
  their descendants will persist after the instances are destroyed. A new child instance
  created with the same name will share the same storage path as the previous instance.

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

_array of `object` (optional)_

The `environments` section declares environments as described in
[Environments][doc-environments].

[doc-environments]: /docs/concepts/components/v2/environments.md

- `name`: (_`string`_) The name of the environment, which is a string of one or more of the
  following characters: `a-z`, `0-9`, `_`, `.`, `-`. The name identifies this
  environment when used in a [reference](#references).
- `extends`: (_optional `string`_) How the environment should extend this realm's environment.
  -   `realm`: Inherit all properties from this compenent's environment.
  -   `none`: Start with an empty environment, do not inherit anything.
- `runners`: (_optional array of `object`_) The runners registered in the environment. An array of objects
  with the following properties:
  - `runner`: (_`string`_) The [name](#name) of a runner capability, whose source is specified in `from`.
  - `from`: (_`string`_) The source of the runner capability, one of:
    -   `parent`: The component's parent.
    -   `self`: This component.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
  - `as`: (_optional `string`_) An explicit name for the runner as it will be known in
    this environment. If omitted, defaults to `runner`.

- `resolvers`: (_optional array of `object`_) The resolvers registered in the environment. An array of
  objects with the following properties:
  - `resolver`: (_`string`_) The [name](#name) of a resolver capability,
    whose source is specified in `from`.
  - `from`: (_`string`_) The source of the resolver capability, one of:
    -   `parent`: The component's parent.
    -   `self`: This component.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
  - `scheme`: (_`string`_) The URL scheme for which the resolver should handle
    resolution.

- `debug`: (_optional array of `object`_) Debug protocols available to any component in this environment acquired
  through `use from debug`.
  - `protocol`: (_optional `string or array of strings`_) The name(s) of the protocol(s) to make available.
  - `from`: (_`string`_) The source of the capability(s), one of:
    -   `parent`: The component's parent.
    -   `self`: This component.
    -   `#<child-name>`: A [reference](#references) to a child component
        instance.
  - `as`: (_optional `string`_) If specified, the name that the capability in `protocol` should be made
    available as to clients. Disallowed if `protocol` is an array.

- `stop_timeout_ms`: (_optional `number`_) The number of milliseconds to wait, after notifying a component in this environment that it
  should terminate, before forcibly killing it.

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
                resolver: "full-resolver",
                from: "parent",
                scheme: "fuchsia-pkg",
            },
        ],
    },
],
```



### `capabilities` {#capabilities}

_array of `object` (optional)_

The `capabilities` section defines capabilities that are provided by this component.
Capabilities that are [offered](#offer) or [exposed](#expose) from `self` must be declared
here.

One and only one of the capability type keys (`protocol`, `directory`, `service`, ...) is required.

[glossary.outgoing directory]: /docs/glossary/README.md#outgoing-directory

- `service`: (_optional `string or array of strings`_) The [name](#name) for this service capability. Specifying `path` is valid
  only when this value is a string.
- `protocol`: (_optional `string or array of strings`_) The [name](#name) for this protocol capability. Specifying `path` is valid
  only when this value is a string.
- `directory`: (_optional `string`_) The [name](#name) for this directory capability.
- `storage`: (_optional `string`_) The [name](#name) for this storage capability.
- `runner`: (_optional `string`_) The [name](#name) for this runner capability.
- `resolver`: (_optional `string`_) The [name](#name) for this resolver capability.
- `event`: (_optional `string`_) The [name](#name) for this event capability.
- `event_stream`: (_optional `string or array of strings`_) The [name](#name) for this event_stream capability.
- `path`: (_optional `string`_) The path within the [outgoing directory][glossary.outgoing directory] of the component's
  program to source the capability.

  For `protocol` and `service`, defaults to `/svc/${protocol}`, otherwise required.

  For `protocol`, the target of the path MUST be a channel, which tends to speak
  the protocol matching the name of this capability.

  For `service`, `directory`, the target of the path MUST be a directory.

  For `runner`, the target of the path MUST be a channel and MUST speak
  the protocol `fuchsia.component.runner.ComponentRunner`.

  For `resolver`, the target of the path MUST be a channel and MUST speak
  the protocol `fuchsia.component.resolution.Resolver`.
- `rights`: (_optional `array of string`_) (`directory` only) The maximum [directory rights][doc-directory-rights] that may be set
  when using this directory.
- `from`: (_optional `string`_) (`storage` only) The source component of an existing directory capability backing this
  storage capability, one of:
  -   `parent`: The component's parent.
  -   `self`: This component.
  -   `#<child-name>`: A [reference](#references) to a child component
      instance.
- `backing_dir`: (_optional `string`_) (`storage` only) The [name](#name) of the directory capability backing the storage. The
  capability must be available from the component referenced in `from`.
- `subdir`: (_optional `string`_) (`storage` only) A subdirectory within `backing_dir` where per-component isolated storage
  directories are created
- `storage_id`: (_optional `string`_) (`storage only`) The identifier used to isolated storage for a component, one of:
  -   `static_instance_id`: The instance ID in the component ID index is used
      as the key for a component's storage. Components which are not listed in
      the component ID index will not be able to use this storage capability.
  -   `static_instance_id_or_moniker`: If the component is listed in the
      component ID index, the instance ID is used as the key for a component's
      storage. Otherwise, the component's relative moniker from the storage
      capability is used.


### `use` {#use}

_array of `object` (optional)_

For executable components, declares capabilities that this
component requires in its [namespace][glossary.namespace] at runtime.
Capabilities are routed from the `parent` unless otherwise specified,
and each capability must have a valid route through all components between
this component and the capability's source.

[fidl-environment-decl]: /reference/fidl/fuchsia.component.decl#Environment
[glossary.namespace]: /docs/glossary/README.md#namespace

- `service`: (_optional `string or array of strings`_) When using a service capability, the [name](#name) of a [service capability][doc-service].
- `protocol`: (_optional `string or array of strings`_) When using a protocol capability, the [name](#name) of a [protocol capability][doc-protocol].
- `directory`: (_optional `string`_) When using a directory capability, the [name](#name) of a [directory capability][doc-directory].
- `storage`: (_optional `string`_) When using a storage capability, the [name](#name) of a [storage capability][doc-storage].
- `event`: (_optional `string or array of strings`_) When using an event capability, the [name](#name) of an [event capability][doc-event].
- `event_stream_deprecated`: (_optional `string`_) Deprecated.
- `event_stream`: (_optional `string or array of strings`_) When using an event stream capability, the [name](#name) of an [event stream capability][doc-event].
- `from`: (_optional `string`_) The source of the capability. Defaults to `parent`.  One of:
  -   `parent`: The component's parent.
  -   `debug`: One of [`debug_capabilities`][fidl-environment-decl] in the
      environment assigned to this component.
  -   `framework`: The Component Framework runtime.
  -   `self`: This component.
  -   `#<capability-name>`: The name of another capability from which the
      requested capability is derived.
  -   `#<child-name>`: A [reference](#references) to a child component
      instance.
- `path`: (_optional `string`_) The path at which to install the capability in the component's namespace. For protocols,
  defaults to `/svc/${protocol}`.  Required for `directory` and `storage`. This property is
  disallowed for declarations with arrays of capability names.
- `rights`: (_optional `string`_) (`directory` only) the maximum [directory rights][doc-directory-rights] to apply to
  the directory in the component's namespace.
- `subdir`: (_optional `string`_) (`directory` only) A subdirectory within the directory capability to provide in the
  component's namespace.
- `as`: (_optional `string`_) TODO(fxb/96705): Document events features.
- `scope`: (_optional `string or array of strings`_) TODO(fxb/96705): Document events features.
- `filter`: (_optional `object`_) TODO(fxb/96705): Document events features.
- `subscriptions`: (_optional `string`_) TODO(fxb/96705): Document events features.
- `dependency`: (_optional `string`_) `dependency` _(optional)_: The type of dependency between the source and
  this component, one of:
  -   `strong`: a strong dependency, which is used to determine shutdown
      ordering. Component manager is guaranteed to stop the target before the
      source. This is the default.
  -   `weak_for_migration`: a weak dependency, which is ignored during
      shutdown. When component manager stops the parent realm, the source may
      stop before the clients. Clients of weak dependencies must be able to
      handle these dependencies becoming unavailable. This type exists to keep
      track of weak dependencies that resulted from migrations into v2
      components.
- `availability`: (_optional `string`_) `availability` _(optional)_: The expectations around this capability's availability. One
  of:
  -   `required` (default): a required dependency, the component is unable to perform its
      work without this capability.
  -   `optional`: an optional dependency, the component will be able to function without this
      capability (although if the capability is unavailable some functionality may be
      disabled).

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



### `expose` {#expose}

_array of `object` (optional)_

Declares the capabilities that are made available to the parent component or to the
framework. It is valid to `expose` from `self` or from a child component.

One and only one of the capability type keys (`protocol`, `directory`, `service`, ...) is required.

- `service`: (_optional `string or array of strings`_) When routing a service, the [name](#name) of a [service capability][doc-service].
- `protocol`: (_optional `string or array of strings`_) When routing a protocol, the [name](#name) of a [protocol capability][doc-protocol].
- `directory`: (_optional `string or array of strings`_) When routing a directory, the [name](#name) of a [directory capability][doc-directory].
- `runner`: (_optional `string or array of strings`_) When routing a runner, the [name](#name) of a [runner capability][doc-runners].
- `resolver`: (_optional `string or array of strings`_) When routing a resolver, the [name](#name) of a [resolver capability][doc-resolvers].
- `from`: (_`string or array of strings`_) `from`: The source of the capability, one of:
  -   `self`: This component. Requires a corresponding
      [`capability`](#capabilities) declaration.
  -   `framework`: The Component Framework runtime.
  -   `#<child-name>`: A [reference](#references) to a child component
      instance.
- `as`: (_optional `string`_) The [name](#name) for the capability as it will be known by the target. If omitted,
  defaults to the original name. `as` cannot be used when an array of multiple capability
  names is provided.
- `to`: (_optional `string`_) The capability target. Either `parent` or `framework`. Defaults to `parent`.
- `rights`: (_optional `string`_) (`directory` only) the maximum [directory rights][doc-directory-rights] to apply to
  the exposed directory capability.
- `subdir`: (_optional `string`_) (`directory` only) the relative path of a subdirectory within the source directory
  capability to route.
- `event_stream`: (_optional `string or array of strings`_) TODO(fxb/96705): Complete.
- `scope`: (_optional `string or array of strings`_) TODO(fxb/96705): Complete.

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
        resolver: "full-resolver",
        from: "#full-resolver",
    },
],
```



### `offer` {#offer}

_array of `object` (optional)_

Declares the capabilities that are made available to a [child component][doc-children]
instance or a [child collection][doc-collections].

- `service`: (_optional `string or array of strings`_) When routing a service, the [name](#name) of a [service capability][doc-service].
- `protocol`: (_optional `string or array of strings`_) When routing a protocol, the [name](#name) of a [protocol capability][doc-protocol].
- `directory`: (_optional `string or array of strings`_) When routing a directory, the [name](#name) of a [directory capability][doc-directory].
- `runner`: (_optional `string or array of strings`_) When routing a runner, the [name](#name) of a [runner capability][doc-runners].
- `resolver`: (_optional `string or array of strings`_) When routing a resolver, the [name](#name) of a [resolver capability][doc-resolvers].
- `storage`: (_optional `string or array of strings`_) When routing a storage capability, the [name](#name) of a [storage capability][doc-storage].
- `event`: (_optional `string or array of strings`_) When routing an event, the [name](#name) of the [event][doc-event].
- `from`: (_`string or array of strings`_) `from`: The source of the capability, one of:
  -   `parent`: The component's parent. This source can be used for all
      capability types.
  -   `self`: This component. Requires a corresponding
      [`capability`](#capabilities) declaration.
  -   `framework`: The Component Framework runtime.
  -   `#<child-name>`: A [reference](#references) to a child component
      instance. This source can only be used when offering protocol,
      directory, or runner capabilities.
  -   `void`: The source is intentionally omitted. Only valid when `availability` is not
      `required`.
- `to`: (_`string or array of strings`_) A capability target or array of targets, each of which is a [reference](#references) to the
  child or collection to which the capability is being offered, of the form `#<target-name>`.
- `as`: (_optional `string`_) An explicit [name](#name) for the capability as it will be known by the target. If omitted,
  defaults to the original name. `as` cannot be used when an array of multiple names is
  provided.
- `dependency`: (_optional `string`_) The type of dependency between the source and
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
- `rights`: (_optional `string`_) (`directory` only) the maximum [directory rights][doc-directory-rights] to apply to
  the offered directory capability.
- `subdir`: (_optional `string`_) (`directory` only) the relative path of a subdirectory within the source directory
  capability to route.
- `filter`: (_optional `object`_) TODO(fxb/96705): Complete.
- `event_stream`: (_optional `string or array of strings`_) TODO(fxb/96705): Complete.
- `scope`: (_optional `string or array of strings`_) TODO(fxb/96705): Complete.
- `availability`: (_optional `string`_) `availability` _(optional)_: The expectations around this capability's availability. One
  of:
  -   `required` (default): a required dependency, the target of this offer must receive this
      capability.
  -   `optional`: an optional dependency, the target of this offer may or may not receive
      this capability, and the target must consume this capability as `optional`.
  -   `same_as_target`: the availability expectations of this capability will match whatever
      the target's. If the target requires the capability, then this field is set to
      `required`. If the target has an optional dependency on the capability, then the field
      is set to `optional`.
- `source_availability`: (_optional `string`_) Whether or not the source of this offer must exist. If set to `unknown`, the source of this
  offer will be rewritten to `void` if the source does not exist (i.e. is not defined in this
  manifest). Defaults to `required`.

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
        resolver: "full-resolver",
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

The configuration schema as defined by a component. Each key represents a single field
in the schema.

NOTE: This feature is currently experimental and access is controlled through an allowlist
in fuchsia.git at `//tools/cmc/build/restricted_features/BUILD.gn`.

Configuration fields are JSON objects and must define a `type` which can be one of the
following strings:
`bool`, `uint8`, `int8`, `uint16`, `int16`, `uint32`, `int32`, `uint64`, `int64`,
`string`, `vector`

Example:

```json5
config: {
    debug_mode: {
        type: "bool"
    },
}
```

Strings must define the `max_size` property as a non-zero integer.

Example:

```json5
config: {
    verbosity: {
        type: "string",
        max_size: 20,
    }
}
```

Vectors must set the `max_count` property as a non-zero integer. Vectors must also set the
`element` property as a JSON object which describes the element being contained in the
vector. Vectors can contain booleans, integers, and strings but cannot contain other
vectors.

Example:

```json5
config: {
    tags: {
        type: "vector",
        max_count: 20,
        element: {
            type: "string",
            max_size: 50,
        }
    }
}
```

