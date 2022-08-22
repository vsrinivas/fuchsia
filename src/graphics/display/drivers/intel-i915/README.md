# Display Driver for Intel GPUs

## Target Hardware

Changes to the driver should be reviewed against the documentation for the
following hardware.

* Tiger Lake - on Intel NUC11
* Kaby Lake - on Google Pixelbook (eve), Google Pixelbook Go (atlas), and Intel
  NUC7
* Skylake - on Acer Switch Alpha 12

Some of the hardware listed here is not supported, meaning we don't regularly
run tests on it. [The hardware development page][fuchsia-development-hardware]
on fuchsia.dev has the most up to date hardware support status.

## Documentation References

### Intel Programmer Manuals

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
* [Kaby Lake (KBL)][intel-kbl-prm]
* [Skylake (SKL)][intel-skl-prm]
* [IGD OpRegion / Software SCI][intel-opregion-prm]

### Standards

The following standards are particularly relevant to this driver.

* [HDMI Forum][hdmi-forum-about] - all standards are members-only
    * High-Definition Multimedia Interface (HDMI) 2.1a, 7 February 2022
* [MIPI Alliance][mipi-alliance-about] - all standards are members-only
    *  Display Serial Interface (DSI) 1.3.2, 17 December 2020
* [Standard Panel Working Group][spwg-about]
    * [SPWG Notebook Panel Specification][spwg-panel-standard] Version 3.8
* [USB Implementers Forum][usb-if-about] -
  [public standards][usb-if-public-standards]
    * [Universal Serial Bus 4][usb4-spec] (USB4)
    * [USB Type-C Cable and Connector][usb-type-c] Release 2.1, May 2021
    * [USB Power Delivery][usb-power-delivery] (PD) Revision 3.1 Version 1.5,
      July 2022
    * [Universal Serial Bus 3.2][usb3-spec] (USB3) Revision 1.1, June 2022
* [VESA][vesa-about]
    * [Public standards][vesa-public-standards]
        * Display Stream Compression (DSC, VDSC) 1.2b, 21 August 2021
        * Enhanced Extended Display Identification Data (E-EDID) Release A,
          Revision 2, revised 31 December 2020
        * DisplayID 2.1, 18 November 2021
    * [Members-only][vesa-members-standards]
        * DisplayPort (DP) 2.0, revised 31 December 2020
        * DisplayPort Alt Mode on USB Type-C 2.0, revised 21 March 2020
        * Embedded DisplayPort (eDP) 1.4b, revised 31 December 2020


[fuchsia-development-hardware]: https://fuchsia.dev/fuchsia-src/development/hardware
[hdmi-forum-about]: https://hdmiforum.org/about/
[mipi-alliance-about]: https://www.mipi.org/about-us
[intel-graphics-prms]: https://01.org/linuxgraphics/documentation/hardware-specification-prms
[intel-tgl-prm]: https://01.org/node/37295
[intel-kbl-prm]: https://01.org/linuxgraphics/hardware-specification-prms/2016-intelr-processors-based-kaby-lake-platform
[intel-skl-prm]: https://01.org/linuxgraphics/documentation/hardware-specification-prms/2015-2016-intel-processors-based-skylake-platform
[intel-opregion-prm]: https://01.org/linuxgraphics/documentation/intel%C2%AE-integrated-graphics-device-opregion-specification
[spwg-about]: https://web.archive.org/web/20130516220616/http://www.spwg.org/
[spwg-panel-standard]: https://web.archive.org/web/20120424092158/http://www.spwg.org/spwg_spec_version3.8_3-14-2007.pdf
[usb-if-about]: https://www.usb.org/about
[usb-if-public-standards]: https://www.usb.org/documents
[usb-power-delivery]: https://www.usb.org/document-library/usb-power-delivery
[usb3-spec]: https://www.usb.org/document-library/usb-32-revision-11-june-2022
[usb4-spec]: https://www.usb.org/document-library/usb4r-specification
[usb-type-c]: https://www.usb.org/document-library/usb-type-cr-cable-and-connector-specification-release-21
[vesa-about]: https://vesa.org/about-vesa/
[vesa-members-standards]: https://vesa.org/join-vesamemberships/member-downloads/
[vesa-public-standards]: https://vesa.org/vesa-standards/
