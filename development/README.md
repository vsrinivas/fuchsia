# Development

This document is a top-level entry point to all of Fuchsia documentation related
to **developing** Fuchsia and software running on Fuchsia.

## Developer workflow

This sections describes the workflows and tools for building, running, testing
and debugging Fuchsia and programs running on Fuchsia.

 - [Getting started](../getting_started.md) - **start here**. This document
   covers getting the source, building and running Fuchsia.
 - [Multiple device setup](../multi_device.md)
 - [Debugging](../debugging.md)
 - [Tracing][tracing]
 - [Toolchain](../toolchain.md)

## Build system

 - [Build system overview](../build_overview.md)
 - [Build system variants](../build_variants.md)

## Languages

 - [C/C++](languages/c-cpp/README.md)
 - [Dart](languages/dart/README.md)
 - [Go](languages/go/README.md)
 - [Rust](languages/rust/README.md)
 - [Flutter modules][flutter_module] - how to write a graphical module using
 Flutter

## Hardware

This section covers Fuchsia development hardware targets.

 - [Acer Switch Alpha 12][acer_12]
 - [Intel NUC][intel_nuc] (also [this](hardware/developing_on_nuc.md))
 - [Pixelbook](hardware/pixelbook.md)

## Conventions

This section covers Fuchsia-wide conventions and best practices.

 - [Layers](../layers.md) - the Fuchsia layer cake, ie. how Fuchsia subsystems are
   split into a stack of layers
 - [Repository structure](../layer_repository_structure.md) - standard way of
   organizing code within a Fuchsia layer repository
 - [Documentation standards](../best-practices/documentation_standards.md)
 - [Testing best practices](../best-practices/testing.md)

## Miscellaneous

 - [CTU analysis in Zircon](../ctu_analysis.md)
 - [Persistent disks in QEMU](../qemu_persistent_disk.md)


[acer_12]: https://fuchsia.googlesource.com/zircon/+/master/docs/targets/acer12.md "Acer 12"
[intel_nuc]: https://fuchsia.googlesource.com/zircon/+/master/docs/targets/nuc.md "Intel NUC"
[pixelbook]: hardware/pixelbook.md "Pixelbook"
[flutter_module]: https://fuchsia.googlesource.com/peridot/+/master/examples/HOWTO_FLUTTER.md "Flutter modules"
[tracing]: https://fuchsia.googlesource.com/garnet/+/master/docs/tracing_usage_guide.md
