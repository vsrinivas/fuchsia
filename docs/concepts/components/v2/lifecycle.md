# Component lifecycle

<<../_v2_banner.md>>

This document describes how Component manager interacts with individual component
instances to manage their lifecycle.

## Lifecycle states {#states}

Component instances progress through the following major lifecycle states:

![Component lifecycle states](images/component-lifecycle.png){: width="662"}

Component instances may retain isolated persistent state on a storage medium
while they are not running, which can be used to help them maintain continuity
across restarts.

### Created {#creating}

A component instance may be created in the following ways:

-   Configured as the root component of Component manager.
-   Statically discovered as the [child][doc-manifests-children] of another
    component.
-   Dynamically created at runtime in a [collection][doc-collections].

Every component instance has a component URL that describes how to resolve the
component, and a moniker that uniquely identifies the instance by its path from
the root. For more details, see [component identifiers][doc-identifiers].

Once created, a component instance can then be [resolved](#resolving) or
[destroyed](#destroying).

### Resolved {#resolving}

Resolving a component instance fetches the component declaration for the
specified component URL. Component manager resolves component URLs by finding a
[component resolver][doc-resolvers] that supports a matching URL scheme in the
environment. Developers can resolve components manually using the
[`ffx component resolve`][ref-ffx-resolve] command.

Components must successfully resolve before they can be [started](#starting).

### Started {#starting}

Starting a component instance loads and runs the component's program and
provides it access to the capabilities that it requires.

The most common reason for starting a component instance is when another
component [binds](#binding) to one of its exposed capabilities. Developers can
also start components manually using the [`ffx component start`][ref-ffx-start]
command.

Once started, a component instance continues to run until it is
[stopped](#stopping).

### Stopped {#stopping}

Stopping a component instance terminates the component's program but preserves
its [persistent state][doc-storage]. Components enter this state when their
program exits, as defined by the component's [runner][doc-runners].

The Component Framework may stop a component instance for the following reasons:

-   The component is about to be destroyed.
-   The system is shutting down.

A component can implement a lifecycle handler ([example][handler-example]) to
receive a notification of events such as impending termination.
Note that components may not receive these events in circumstances such as
resource exhaustion, crashes, or power failure.

Once stopped, a component instance may be [restarted](#starting) or
[shutdown](#shutdown).

### Shutdown {#shutdown}

Component manager sets the final execution state of a component instance to
shutdown to indicate that it cannot be restarted and to signal that the instance
can be safely [destroyed](#destroying).

### Destroyed {#destroying}

A component instance may be destroyed in the following ways:

-   Dynamically removed from a [collection][doc-collections] at runtime. This is
    also true if the component is a descendant of another component being removed.

Once destroyed, Component manager completely removes the instance from the
component topology, including all persistent state. New instances of the same
component will each have their own identity and state distinct from all prior
instances.

## Lifecycle actions {#actions}

This section describes common actions used by the Component Framework to
transition the lifecycle state of component instances.

### Bind {#binding}

A component instance `A` _binds_ to another component instance `B` when `A`
connects to some capability that is provided by `B`. This causes component `B`
to [start](#starting) if it is not already running.

Concretely, there are two ways that `A` can bind to `B`:

-   `A` connects to a capability in its namespace which is
    [exposed][doc-manifests-expose] or [offered][doc-manifests-offer] by `B`.
    This is the most common way.
-   `A` binds to the [`fuchsia.component.Binder`][binder.fidl]
    [framework protocol][doc-framework-protocol] which is exposed or offered
    by `B`. Unlike a traditional capability, this protocol
    is implemented by the component framework.

Note: For more details on running components during development, see
[Run components][doc-run].

## Lifecycle policies {#lifecycle-policies}

This section covers additional policies you may configure to modify the startup
and termination behavior of your component.

### Eager binding {#eager}

[Component manifests][doc-manifests] let you mark a child as
[`eager`][doc-manifests-children], which causes the component framework to
implicitly bind to that child when any component binds to the parent. In other
words, this causes the child to be immediately started whenever the parent is
started.

If the eager child fails to start for any reason (such as a missing component),
Component manager exhibits the following behavior:

-   If the parent is not the root component, the parent will start but the
    component that bound to it will observe a dropped connection (just like any
    other failed binding).
-   If the parent is the root component, Component manager will crash, with an
    error message like:

    ```none {:.devsite-disable-click-to-copy}
    [component_manager] ERROR: Failed to route protocol `fuchsia.appmgr.Startup` with target component `/startup`:
    failed to resolve "fuchsia-pkg://fuchsia.com/your_component#meta/your_component.cm":
    package not found: remote resolver responded with PackageNotFound
    ```

An `eager` component should be in the same package set as its parent since the
component will be started at the same time as its parent. Typically `eager`
components should be in the product's base [package set][doc-package-set].

Components marked as `eager` whose ancestors are marked `eager` up
to the root will cause system crashes when they are not present. This is
important because many tests and products create system images containing only a
subset of all available components. You should declare these components using
[**core realm shards**][core-shard] to ensure they can be safely excluded from
test builds and product images containing subsets of components.

You can determine if your package is in base. If your package is not present in
base the command below will print no output.

```posix-terminal
fx list-packages --base {{ '<var label="package name">my-package</var>' }}
```

You can also look at all the packages in the the base package set.

```posix-terminal
fx list-packages --base
```

### Reboot on terminate {#reboot-on-terminate}

A component that has the "reboot-on-terminate" policy set will cause the system
to gracefully reboot if the component terminates for any reason (including
successful exit). This is a special feature intended for use only by system
components deemed critical to the system's function. Therefore, its use is
governed by a [security policy allowlist][fidl-child-policy].

Note: If you believe you need this option, please reach out to the
[Component Framework team][cf-dev-list].

To enable the feature, mark the child as `on_terminate: reboot` in the parent
component's [manifest][doc-manifests]:

```json5
// core.cml
{
    children: [
        ...
        {
            name: "system-update-checker",
            url: "fuchsia-pkg://fuchsia.com/system-update-checker#meta/system-update-checker.cm",
            startup: "eager",
            on_terminate: "reboot",
        },
    ],
}
```

Also, you'll need to add the component's moniker to Component manager's security
policy allowlist at
[`//src/security/policy/component_manager_policy.json5`][src-security-policy]:

```json5
// //src/security/policy/component_manager_policy.json5
{
    security_policy: {
        ...
        child_policy: {
            reboot_on_terminate: [
                ...
                "/core/system-update-checker",
            ],
        },
    },
}
```

[binder.fidl]: https://fuchsia.dev/reference/fidl/fuchsia.component#Binder
[cf-dev-list]: https://groups.google.com/a/fuchsia.dev/g/component-framework-dev
[core-shard]: /src/sys/core/README.md
[doc-framework-protocol]: capabilities/protocol.md#framework
[doc-collections]: realms.md#collections
[doc-identifiers]: identifiers.md
[doc-lifecycle]: lifecycle.md
[doc-manifests-children]: https://fuchsia.dev/reference/cml#children
[doc-manifests-expose]: https://fuchsia.dev/reference/cml#expose
[doc-manifests-offer]: https://fuchsia.dev/reference/cml#offer
[doc-manifests]: component_manifests.md
[doc-package-set]: /docs/concepts/packages/package.md#types_of_packages
[doc-resolvers]: capabilities/resolvers.md
[doc-runners]: capabilities/runners.md
[doc-storage]: capabilities/storage.md
[doc-topology]: topology.md
[doc-run]: /docs/development/components/run.md
[fidl-child-policy]: https://fuchsia.dev/reference/fidl/fuchsia.component.internal#ChildPolicyAllowlists
[handler-example]: /examples/components/lifecycle
[realm.fidl]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#Realm
[ref-ffx-resolve]: https://fuchsia.dev/reference/tools/sdk/ffx#resolve
[ref-ffx-start]: https://fuchsia.dev/reference/tools/sdk/ffx#start
[src-security-policy]: /src/security/policy/component_manager_policy.json5
