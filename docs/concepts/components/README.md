# Components

This section contains documentation about components in the new component
framework ("components v2").

Components are the basic unit of executable software on Fuchsia.

Note: the component framework is under active development. This document
only covers the [new architecture][glossary-components-v2] implemented by
`component_manager`. The [old architecture][glossary-components-v1] implemented
by `appmgr` is still in use but will be removed once the transition to the
new architecture is complete.

**Architectural concepts**

- [Introduction](introduction.md): what are components and the component
  framework.
- [Component manager](component_manager.md): the runtime.
- [Declarations](declarations.md): describe components themselves.
- [Lifecycle](declarations.md): component instance progression from creation to
  destruction.
- [Realms](realms.md): sub-trees of the component instance topology.
- [Component URLs](component_urls.md): URLs that identify components.
- [Monikers](monikers.md): identifiers for component instances.

**Developing components**

- [Capabilities](capabilities/README.md): different types of capabilities and
  how to route them between components.
- [Component manifests](component_manifests.md): how to define a component for
  the framework.
- [ELF runner](elf_runner.md): how to launch a component from an ELF file.
  Typically useful for developing system components in C++, Rust, or Go.

**Extending the component framework**

- [Runners](runners.md): instantiate components; add support for more
  runtimes.
- [Resolvers](resolvers.md): find components from URLs; add support for
  methods of software packaging and distribution.

**Debugging & troubleshooting**

- [Hub](hub.md): a live view of the component topology at runtime.
- [Black box testing](black_box_testing.md): integration testing framework.

**Internals**

- [Design principles](design_principles.md): guidelines for arriving at
  architectural decisions.
- [Life of a protocol open](life_of_a_protocol_open.md): how components connect
  to protocols in their namespaces.
