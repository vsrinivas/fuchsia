# State of the Components v2 migration

The Component Framework is one of the key foundations for Fuchsia's usermode
runtime environment. The original incarnation of components dates back to the
inception of the Fuchsia OS and the initial commits in 2016. The framework has
steadily evolved since then.

## Components v1 vs. v2

Presently there are two revisions of the Component Framework that exist on
Fuchsia, which are referred to as [Components v1][cfv1] and
[Components v2][cfv2].

Components v1 is largely comprised of:

*   [`appmgr`][appmgr], a program that manages the runtime environment for v1
    components. `appmgr` implements the root of the v1 components tree, as well
    as some foundational services such as the Components v1 ELF runner and
    Loader service.
*   [`sysmgr`][sysmgr], a component that manages the so-called `"sys"` realm.
    `sysmgr` is launched by `appmgr`.
*   The [`.cmx`][cmx] file format for v1 component manifests.
*   The [`fuchsia.sys.*`][fuchsia-sys] FIDL library.

Components v1 development reached its peak in 2018. In 2019, Fuchsia team began
developing [Component Framework v2][intro].

Components v2 is largely comprised of:

*   [Component manager][component_manager], a program that manages the runtime
    environment for v2 components. Component manager is now responsible for
    launching `appmgr`. `appmgr` has become a v2 component itself,
    which serves as the parent of all v1 components still present in the
    system.
*   The [`.cml`][cml] file format for v2 component manifests.
*   The [`fuchsia.sys2.*`][fuchsia-sys2] FIDL library.

In addition, both Components v1 and v2 use [`cmc`][cmc] (component manifest
compiler), a build-time host tool that processes all formats of component
manifest files.

## Incremental progress

The nature of migrations is that they may take a long time and happen in
incremental steps. The final step for migrating a component from v1 to v2
typically involves replacing a `.cmx` file with a `.cml` file.

## Latest status

Last updated: **January 2021**

A high-level diagram of the system's component topology is shown below:

![Realms diagram](images/high_level_components_topology.png)

*   v2 components are shown in green.
*   v1 components are shown in red.
*   Boxes with dashed lines represent components that are only present on some
    build configurations.

Component manager is one of the [initial processes][initial-processes] that are
started in the system boot sequence.
The system startup sequence then launches a number of low-level system
components that deal with various responsibilities, including in no particular
order:

*   Power management: device administration, thermals, power button, etc'.
*   System diagnostics: logging, tracing, kernel counters etc'.
*   Device driver management.
*   Filesystem management.
*   Developer tools support, such as Remote Control Service and support for
    serial debugging.
*   Font provider.
*   A (growing) subset of the Software Delivery stack.
*   A (growing) subset of the Connectivity stack.

In addition, Component manager launches `appmgr`, itself a v2 component, in
order to manage all remaining v1 components in a dedicated sub-realm. As the
migration continues, typically components will move from the v1 sub-realm to
elsewhere in the component instance topology.

Note: `component_manager_sfw` is currently included in build configurations that
use the [Session Framework][session-framework]. This second instance of the
`component_manager` facilitates interoperability between v2 components (required
under Session Framework) and legacy v1 components. Once all v1 components have
been migrated to v2, the `session_manager` will become a direct descendent of
the root `component_manager`, and `component_manager_sfw` can be removed.

## Current areas of focus

Last updated: **January 2021**

Components v2 migrations are happening throughout the system. However there is
currently additional focus on:

*   The Software Delivery stack of components and associated tests.
*   The Netstack2 component and associated tests.
*   A subset of legacy sys realm components that are simpler to migrate.

[appmgr]: /src/sys/appmgr
[cfv1]: /docs/glossary.md#components-v1
[cfv2]: /docs/glossary.md#components-v2
[cmc]: /tools/cmc/
[cml]: /docs/concepts/components/v2/component_manifests.md
[cmx]: /docs/concepts/components/v1/component_manifests.md
[component_manager]: /docs/concepts/components/v2/component_manager.md
[fuchsia-sys]: https://fuchsia.dev/reference/fidl/fuchsia.sys
[fuchsia-sys2]: https://fuchsia.dev/reference/fidl/fuchsia.sys2
[initial-processes]: /docs/concepts/booting/everything_between_power_on_and_your_component.md#initial-processes
[intro]: /docs/concepts/components/v2/introduction.md
[session-framework]: /docs/concepts/session/introduction.md
[sysmgr]: /src/sys/sysmgr
