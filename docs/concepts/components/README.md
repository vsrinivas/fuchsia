# Components

This section contains documentation about components in the new component
framework ([components v2][glossary-components-v2]).

Components are the basic unit of executable software on Fuchsia.

Note: The component framework is under active development. This document
only covers the new architecture (components v2) implemented by
`component_manager`. The old architecture ([components v1][glossary-components-v1])
implemented by `appmgr` is still in use but will be removed once
the transition to the new architecture is complete.

## Architectural concepts

- [Introduction](introduction.md): What are components and the component
  framework.
- [Component manager](component_manager.md): The runtime.
- [Declarations](declarations.md): Describe components themselves.
- [Component URLs](component_urls.md): URLs that identify components.
- [Lifecycle](lifecycle.md): Component instance progression from creation to
  destruction.
- [Topology](topology.md): The relationships among component instances.
- [Realms](realms.md): Sub-trees of the component instance topology.
- [Monikers](monikers.md): Identifiers for component instances based on
  the component topology.
- [The difference between components and processes](components_and_processes.md):
  The relationship between components and processes.

## Developing components

- [Capabilities](capabilities/README.md): Different types of capabilities and
  how to route them between components.
- [Component manifests](component_manifests.md): How to define a component for
  the framework.
- [ELF runner](elf_runner.md): How to launch a component from an ELF file.
  Typically useful for developing system components in C++, Rust, or Go.

## Extending the component framework

- [Runners](runners.md): Instantiate components; add support for more
  runtimes.
- [Resolvers](resolvers.md): Find components from URLs; add support for
  methods of software packaging and distribution.

## Debugging and troubleshooting

- [Hub](hub.md): A live view of the component topology at runtime.
- [Black box testing](black_box_testing.md): Integration testing framework.

## Internals

- [Design principles](design_principles.md): Guidelines for arriving at
  architectural decisions.
- [Life of a protocol open](life_of_a_protocol_open.md): How components connect
  to protocols in their namespaces.

[glossary-components-v1]: /docs/glossary.md#components-v1
[glossary-components-v2]: /docs/glossary.md#components-v2

