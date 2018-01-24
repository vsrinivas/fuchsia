# Fuchsia Development

This document is a top-level entry point to all of Fuchsia documentation related
to **developing** Fuchsia and software running on Fuchsia.

## Developer workflow

This sections describes the workflows and tools for building, running, testing
and debugging Fuchsia and programs running on Fuchsia.

 - [Getting started](getting_started.md) - **start here**. This document covers
   getting the source, building and running Fuchsia.
 - [Contributing changes](CONTRIBUTING.md)
 - [Multiple device setup](multi_device.md)
 - [Debugging](debugging.md)
 - [Tracing]
 - [Toolchain](toolchain.md)

Build system (**TODO**: should these docs be merged?):

 - [Build system](build_system.md)
 - [Build system overview](build_overview.md)
 - [Build system variants](build_variants.md)

Language-specific workflow guides:

 - [Dart](dart.md)
 - [Flutter modules][flutter_module] - how to write a graphical module using
   Flutter
 - [Rust](rust.md)

## Development hardware

This section covers Fuchsia development hardware targets.

 - [Acer Switch Alpha 12][acer_12]
 - [Intel NUC][intel_nuc] (also [this](developing_on_nuc.md))
 - [Pixelbook][pixelbook]

## Conventions

This section covers Fuchsia-wide conventions and best practices.

 - [Layers](layers.md) - the Fuchsia layer cake, ie. how Fuchsia subsystems are
   split into a stack of layers
 - [Repository structure](layer_repository_structure.md) - standard way of
   organizing code within a Fuchsia layer repository
 - [Documentation standards](documentation_standards.md)
 - [Testing best practices][testing_best_practices]

Language-specific conventions:

 - [Naming C/C++ objects](languages/c-cpp/naming.md)

[acer_12]: https://fuchsia.googlesource.com/zircon/+/master/docs/targets/acer12.md "Acer 12"
[intel_nuc]: https://fuchsia.googlesource.com/zircon/+/master/docs/targets/nuc.md "Intel NUC"
[pixelbook]: /hardware/pixelbook.md "Pixelbook"
[flutter_module]: https://fuchsia.googlesource.com/peridot/+/master/examples/HOWTO_FLUTTER.md "Flutter modules"
[testing_best_practices]: /best-practices/testing.md "Testing best practices"
[Tracing]: https://fuchsia.googlesource.com/garnet/+/master/docs/tracing_usage_guide.md
