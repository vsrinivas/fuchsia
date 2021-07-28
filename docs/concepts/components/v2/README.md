# Components

Components are the basic unit of executable software on Fuchsia.

Note: This section contains documentation about components using the modern
component framework ([Components v2][glossary.components v2]). The Fuchsia
platform team is currently [migrating legacy components][migration] to the
modern component framework. For details on legacy components, see
[Legacy components][legacy-components].

## Architectural concepts

-   [Introduction](introduction.md): Understanding components and the component
    framework.
-   [Component manager](component_manager.md): The runtime.
-   [Lifecycle](lifecycle.md): Component instance progression from creation to
    destruction.
-   [Topology](topology.md): The relationships among component instances.
-   [Realms](realms.md): Sub-trees of the component instance topology.
-   [Monikers](monikers.md): Identifiers for component instances based on the
    component topology.

## Developing components

-   [Capabilities](capabilities/README.md): Different types of capabilities and
    how to route them between components.
-   [Component manifests](component_manifests.md): How to define a component for
    the framework.
-   [Component URLs][doc-component-urls] are URLs that identify components.
-   [ELF runner](elf_runner.md): How to launch a component from an ELF file.
    Typically useful for developing system components in C++, Rust, or Go.

## Extending the component framework

-   [Runners](capabilities/runners.md): Instantiate components; add support for
    more runtimes.
-   [Resolvers](capabilities/resolvers.md): Find components from URLs; add
    support for methods of software packaging and distribution.

## Diagnostics

-   [Hub](hub.md): A live view of the component topology at runtime.

## Testing

-   [Test components][test-components]: defining components that implement tests
    and running them.
-   [Test Runner Framework][trf]: writing idiomatic tests in different languages
    that use common testing frameworks.
-   [Complex topologies and integration testing][integration-testing]: testing
    interactions between multiple components in isolation from the rest of the
    system.
-   [OpaqueTest](opaque_test.md): Hermetic testing framework (DEPRECATED).

## Internals

-   [Component Framework design principles](design_principles.md)
-   [Component manifest design principles][rfc0093]
-   [Components vs. processes](components_vs_processes.md): how the concepts
    differ.

## Misc

-   [Termination policies](termination_policies.md): Policies that
    can be configured to react to component termination

[glossary.components v1]: /docs/glossary/README.md#components-v1
[glossary.components v2]: /docs/glossary/README.md#components-v2
[doc-component-urls]: /docs/concepts/components/component_urls.md
[legacy-components]: /docs/concepts/components/v1/README.md
[migration]: /docs/contribute/open_projects/components/migration.md
[rfc0093]: /docs/contribute/governance/rfcs/0093_component_manifest_design_principles.md
[test-components]: /docs/concepts/testing/v2/test_component.md
[trf]: /docs/concepts/testing/v2/test_runner_framework.md
[integration-testing]: /docs/concepts/testing/v2/integration_testing.md
