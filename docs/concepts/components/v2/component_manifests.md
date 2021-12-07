# Component manifests {#component-manifests}

<<../_v2_banner.md>>

A [component manifest](#component-manifest) is a file that defines a component
by encoding a [component declaration](#component-declaration). This document
gives an overview of the concepts used by component declarations.
Component declarations contain:

-   Information about [how to run the component](#runtime).
-   The component's [child component instances][doc-children] and
    [component collections][doc-collections].
-   [Routing rules](#capability-routing) that describe how capabilities are
    used, exposed, and offered between components.
-   [Freeform data ("facets")][manifest-facet], which is ignored by the
    component framework but can be interpreted by third parties.

Note: For complete details on component manifest attributes and syntax, see the
[CML reference](https://fuchsia.dev/reference/cml).

## Manifests and declarations {#manifests-and-declarations}

This section explains the distinction between component manifests, component
manifest sources, and component declarations.

### Component manifest source {#component-manifest-source}

A [component manifest source][glossary.component manifest source] is a file that
encodes part of a component manifest. Component manifest sources are written in
component manifest language (CML), which is the developer-facing source format
for component manifests. CML files are JSON5 files that end with a `.cml`
extension.

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
using the [`Component`][fidl-component-decl] FIDL table.

A component [resolver][capability-resolver] retrieves the component declaration and
provides it to the component framework.

## Runtime {#runtime}

The component framework doesn't dictate a particular format for programs, but
instead requires components to specify which runtime they need by specifying a
[runner][capability-runner]. The component framework provides a built-in
[ELF runner](elf_runner.md), while other runtimes are implemented as components
within the framework. A component can specify any runner available in its
[environment][doc-environments].

The [`program`][manifest-program] section of a component manifest designates a
runner and declares to the runner how the component is run, such as the program
location and any arguments. Components using the ELF runner should specify the
binary name and arguments, see [ELF Runner](elf_runner.md).
[Other runners][capability-runner] may have other runner-specific details, documented
by that runner.

A component may also have no runtime at all by omitting the `program` section.
In this case, the component may still route capabilities and host children, but
no code will be executed for the component.

See also: [ELF Runner](elf_runner.md), [Component Runners][capability-runner]

## Capability routing {#capability-routing}

Component manifests provide a syntax for routing capabilities between
components. For a detailed walkthrough about what happens during capability
routing, see the [Capabilities overview][doc-capabilities] and
[Life of a protocol open][doc-protocol-open].

### Capability names {#capability-names}

Every capability in a `.cml` has a name so that it can be referred to in routing
declarations. A capability name consists of a string containing the characters
`a` to `z`, `A` to `Z`, `0` to `9`, underscore (`_`), hyphen (`-`), or the full
stop character (`.`).

### Capability types {#capability-types}

The following capabilities can be routed:

| type                   | description                   | routed to          |
| ---------------------- | ----------------------------- | ------------------ |
| [`protocol`]           | A filesystem node that is     | components         |
: [capability-protocol]  : used to open a channel backed :                    :
:                        : by a FIDL protocol.           :                    :
| [`service`]            | A filesystem directory that   | components         |
: [capability-service]   : is used to open a channel to  :                    :
:                        : one of several service        :                    :
:                        : instances.                    :                    :
| [`directory`]          | A filesystem directory.       | components         |
: [capability-directory] :                               :                    :
| [`storage`]            | A writable filesystem         | components         |
: [capability-storage]   : directory that is isolated to :                    :
:                        : the component using it.       :                    :
| [`resolver`]           | A capability that, when       | [environments]     |
: [capability-resolver]  : registered in an environment, : [doc-environments] :
:                        : causes a component with a     :                    :
:                        : particular URL scheme to be   :                    :
:                        : resolved with that resolver.  :                    :
| [`runner`]             | A capability that, when       | [environments]     |
: [capability-runner]    : registered in an environment, : [doc-environments] :
:                        : allows the framework to use   :                    :
:                        : that runner when starting     :                    :
:                        : components.                   :                    :

### Routing terminology {#routing-terminology}

Routing terminology divides into the following categories:

1.  Declarations of how capabilities are routed between the component, its
    parent, and its children:
    -   `offer`: Declares that the capability listed is made available to a
        [child component][doc-children] instance or a
        [child collection][doc-collections].
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

### Examples {#examples}

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

[capability-protocol]: /docs/concepts/components/v2/capabilities/protocol.md
[capability-service]: /docs/concepts/components/v2/capabilities/service.md
[capability-directory]: /docs/concepts/components/v2/capabilities/directory.md
[capability-storage]: /docs/concepts/components/v2/capabilities/storage.md
[capability-resolver]: /docs/concepts/components/v2/capabilities/resolvers.md
[capability-runner]: /docs/concepts/components/v2/capabilities/runners.md
[doc-capabilities]: /docs/concepts/components/v2/capabilities/README.md
[doc-children]: /docs/concepts/components/v2/realms.md#child-component-instances
[doc-collections]: /docs/concepts/components/v2/realms.md#collections
[doc-environments]: /docs/concepts/components/v2/environments.md
[doc-package-url]: /docs/concepts/packages/package_url.md
[doc-protocol-open]: /docs/concepts/components/v2/capabilities/life_of_a_protocol_open.md
[examples-routing]: /examples/components/routing
[fidl-component-decl]: https://fuchsia.dev/reference/fidl/fuchsia.component.decl#Component
[glossary.component declaration]: /docs/glossary/README.md#component-declaration
[glossary.component manifest]: /docs/glossary/README.md#component-manifest
[glossary.component manifest source]: /docs/glossary/README.md#component-manifest-source
[glossary.outgoing directory]: /docs/glossary/README.md#outgoing-directory
[glossary.namespace]: /docs/glossary/README.md#namespace
[glossary.package]: /docs/glossary/README.md#package
[manifest-program]: https://fuchsia.dev/reference/cml#program
[manifest-facet]: https://fuchsia.dev/reference/cml#facets
[src-cmc]: /tools/cmc
