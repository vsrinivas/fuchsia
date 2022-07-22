# (Temporary) Display Driver for Intel Tiger Lake GPUs

<!-- TODO(fxbug.dev/104963): This driver should be merged with intel-i915. -->
This is a temporary / experimental driver only used for supporting Intel Tiger
Lake GPUs; Only Tiger Lake devices can be bound to this driver.

## Target Hardware

Changes to the driver should be reviewed against the documentation for the
following hardware.

* Tiger Lake - on Intel NUC11

Some of the hardware listed here is not supported, meaning we don't regularly
run tests on it. [The hardware development page][fuchsia-development-hardware]
on fuchsia.dev has the most up to date hardware support status.

## Documentation References

The driver code here is based on the
[Programmer's Reference Manuals][intel-graphics-prms] published by Intel for
graphics driver developers.

To facilitate reviews, driver code should include comments pointing to the
relevant manual sections. Reference comments should include the following parts:

1. The document reference ID, such as `IHD-OS-TGL-Vol 2c-12.21`. This
   facilitates searching for a downloadable version of the manual.
1. The section title, such as `"Sequences for DisplayPort" > "Enable Sequence"`.
   This facilitates searching for equivalent information in a different manual.
   This example uses a two-level section name because `"Enable Sequence"` is too
   generic, resulting in too many hits.
1. A part number, if the document is split in multiple parts. For example,
   `IHD-OS-TGL-Vol 2c-12.21` is split into two parts. Without a `Part 1` /
   `Part 2` qualifier, page numbers would be ambiguous.
1. A page number or page range, such as `pages 143-145`. This optimizes for
   reviewers who need to check that the code matches the documentation.

The most relevant manuals for this driver are:

* [Tiger Lake (TGL)][intel-tgl-prm]
* [IGD OpRegion / Software SCI][intel-opregion-prm]


[fuchsia-development-hardware]: https://fuchsia.dev/fuchsia-src/development/hardware
[intel-graphics-prms]: https://01.org/linuxgraphics/documentation/hardware-specification-prms
[intel-tgl-prm]: https://01.org/node/37295
[intel-opregion-prm]: https://01.org/linuxgraphics/documentation/intel%C2%AE-integrated-graphics-device-opregion-specification
