# Components

This directory contains documentation about components in the new component
framework ("components v2").

Components are the basic unit of executable software on Fuchsia. They may
provide and consume capabilities such as services and directories, declare
routing of these capabilities, provide isolation boundaries, and have continuity
between executions.

**Architectural concepts**

- [Introduction](introduction.md): the purpose of the component framework, and
  fundamental design principles.
- [Realms](realms.md): sub-trees of the component instance topology.
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
- [Instrumentation hooks](instrumentation_hooks.md): intercept events in the
  component runtime.

**Debugging & troubleshooting**

- [Hub](hub.md): a live view of the component topology at runtime.
- [Black box testing](black_box_testing.md): integration testing framework.

**Implementation details**

- [Life of a service open](life_of_a_service_open.md): how components connect to
  services in their namespaces.
