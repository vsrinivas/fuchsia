Spinel SPI Framer
=================

This directory contains the sources for a SPI-based Spinel framer
device driver. This driver takes a `fuchsia.hardware.spi::Device`
and provides a `fuchsia.lowpan.spinel::Device`.

The framing protocol used is that which is described in [draft-rquattle-spinel-unified-00][].

[draft-rquattle-spinel-unified-00]: https://tools.ietf.org/html/draft-rquattle-spinel-unified-00#appendix-A.2
