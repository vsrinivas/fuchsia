# Component lifecycle

<<../_v2_banner.md>>

Component instances progress through four major lifecycle events: create, start,
stop, destroy and purge.

Component instances may retain isolated persistent state on a storage medium
while they are not running, which helps them maintain the
[illusion of continuity][principle-continuity] across restarts.

## Creating a component instance {#creating}

A component instance may be created in the following ways:

-   Configuring it as the root component of component manager.
-   Statically declaring it as the [child][doc-manifests-children] of another
    component.
-   Dynamically creating it at runtime in a [collection][doc-collections].

Every component instance has a [moniker][doc-monikers] that uniquely identifies
it, determined by its path from the root.

Once created, a component instance can then be [bound to](#binding),
[started](#starting), or [destroyed](#destroying).

## Binding to a component instance {#binding}

A component instance `A` _binds_ to another component instance `B` when `A`
connects to some capability that is provided by `B`. When this happens,
component instance `B` is started, unless it was already started. In most cases,
this is the most common reason for a component instance to [start](#starting).

Concretely, there are three ways that `A` can bind to `B`:

-   `A` connects to a capability in its namespace which is
    [exposed][doc-manifests-expose] or [offered][doc-manifests-offer] by `B`.
    This is the most common way.
-   `A` binds to one of its children using the [`Realm.BindChild`][realm.fidl]
    protocol.
-   `A` binds to the [`fuchsia.component.Binder`][binder.fidl]
    [framework protocol][doc-framework-protocol] which is exposed or offered
    by `B`. Unlike a traditional capability, this protocol
    is implemented by the component framework.

The word "bind" is meant to imply that a component is run on account of being
"bound" by its clients. In theory, when no more clients are bound to a
component, the framework could stop running it, but this behavior isn't
currently implemented.

## Starting a component instance {#starting}

Starting a component instance loads and runs the component's program and
provides it access to the capabilities that it requires.

[Every component runs for a reason][principle-accountability]. The component
framework only starts a component instance when it has work to do, such as when
another component requests to use its instance's capabilities.

Once started, a component instance continues to run until it is
[stopped](#stopping).

## Stopping a component instance {#stopping}

Stopping a component instance terminates the component's program but preserves
its [persistent state][doc-storage] so that it can continue where it left off
when subsequently restarted.

The component framework may stop a component instance for the following reasons:

-   When the component is about to be purged.
-   When the system is shutting down.

A component can implement a lifecycle handler ([example][handler-example]) to be
notified of its impending termination and other events on a best effort basis.
Note that a component can be terminated involuntarily and without notice in
circumstances such as resource exhaustion, crashes, or power failure.

Components can stop themselves by exiting. The means by which a component exits
depend on the runner that runs the component.

Once stopped, a component instance can then be [restarted](#starting) or
[destroyed](#destroying).

## Destroying a component instance {#destroying}

Once destroyed, a component instance ceases to exist and cannot be restarted.
New instances of the same component can still be created but they will each have
their own identity and state distinct from all prior instances. From an external
point of view, the component doesn't exist anymore in the component topology.

## Purging a component instance {#purging}

Purging a destroyed component instance deletes any persistent storage it's using
and all its internal state from component_manager.

## Eager binding {#eager}

[Component manifests][doc-manifests] let you mark a child as
[`eager`][doc-manifests-children], which causes the component framework to
implicitly bind to that child when any component binds to the parent. In other
words, this causes the child to be immediately started whenever the parent is
started.

If the eager child fails to start for any reason (such as a missing component),
component manager exhibits the following behavior:

-   If the parent is not the root component, the parent will start but the
    component that bound to it will observe a dropped connection (just like any
    other failed binding).
-   If the parent is the root component, component manager will crash, with an
    error message like:
    ```
    [component_manager] ERROR: Failed to route protocol `fuchsia.appmgr.Startup` with target component `/startup:0`: failed to resolve "fuchsia-pkg://fuchsia.com/your_component#meta/your_component.cm": package not found: remote resolver responded with PackageNotFound
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

[core-shard]: /src/sys/core/README.md
[doc-framework-protocol]: component_manifests.md#framework-protocols
[doc-collections]: realms.md#collections
[doc-lifecycle]: lifecycle.md
[doc-manifests-children]: component_manifests.md#children
[doc-manifests-expose]: component_manifests.md#expose
[doc-manifests-offer]: component_manifests.md#offer
[doc-manifests]: component_manifests.md
[doc-monikers]: monikers.md
[doc-package-set]: /docs/concepts/packages/package.md#types_of_packages
[doc-storage]: capabilities/storage.md
[doc-topology]: topology.md
[handler-example]: /examples/components/lifecycle
[principle-accountability]: design_principles.md#accountability
[principle-continuity]: design_principles.md#illusion-of-continuity
[realm.fidl]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#Realm
[binder.fidl]: https://fuchsia.dev/reference/fidl/fuchsia.component#Binder
