LoWPAN Interface Drivers
========================

This directory contains two types of drivers:

*   LoWPAN device drivers, which provide one or more instances of
    `fuchsia.lowpan::Device` to the [LoWPAN service](../service/README.md).
*   [Spinel][] framer drivers, which provide a
    `fuchsia.lowpan.spinel::Device` wrapper around a hardware device,
    like `fidl.hardware.spi::Device`. Spinel drivers generally start
    with `spinel-`.

[Spinel]: https://tools.ietf.org/html/draft-rquattle-spinel-unified-00
