Fuchsia Documentation
=======================================

This repository contains documentation for Fuchsia.

# General

+ [Code of Conduct](/CODE_OF_CONDUCT.md)
+ [Contribution guidelines](/CONTRIBUTING.md)
+ [Repository structure](/layer_repository_structure.md)
+ [Documentation standards](/documentation_standards.md)


# How do I?

+ [How do I get started with Fuchsia?][getting_started]

+ [How do I use the build system?][build_system]

+ How do I boot on my...
  + [Acer Switch Alpha 12?][acer_12]
  + [Intel NUC?][intel_nuc] (also [this](/developing_on_nuc.md))
  + [Pixelbook?][pixelbook]

+ [How do I write a flutter module?][flutter_module]

+ [How do I contribute changes?][contributing]

+ How do I write code?
  + In [Dart](/dart.md)
  + In [Rust](/rust.md)

+ [How do I work with multiple devices?](/multi_device.md)


# Individual Project Documentation

+ [Zircon][zircon]

    Zircon is the microkernel underlying the rest of Fuchsia. Zircon
    also provides core drivers and Fuchsia's libc implementation.


# Reference Material

+ [The Fuchsia Book](book.md)


[zircon]: https://fuchsia.googlesource.com/zircon/+/master/README.md "Zircon"
[getting_started]: getting_started.md "Getting started"
[build_system]: build_system.md "Build system"
[acer_12]: https://fuchsia.googlesource.com/zircon/+/master/docs/targets/acer12.md "Acer 12"
[intel_nuc]: https://fuchsia.googlesource.com/zircon/+/master/docs/targets/nuc.md "Intel NUC"
[pixelbook]: /hardware/pixelbook.md "Pixelbook"
[flutter_module]: https://fuchsia.googlesource.com/peridot/+/master/examples/HOWTO_FLUTTER.md "Flutter modules"
[contributing]: /CONTRIBUTING.md "Contributing changes"
