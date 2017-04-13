# Glossary

The following terms and abbreviations are used in this codebase:

* DDC: Display Data Channel, a channel for communicating with a display,
  including the ability to fetch EDID data.  Versions 2 and later are based
  on I2C and are used by VGI, DVI, HDMI and DisplayPort.

* DDI: This term is used in Intel's documentation and probably stands for
  "Digital Display Interface".  It refers to a functional unit that can
  send data to a display using one or more interface standards
  (DisplayPort, HDMI, etc.).  A given DDI might or might not be wired up to
  a physical port.

* DP (or Dp): DisplayPort

* DP Aux: DisplayPort's auxiliary channel

* DPLL: Display PLL (Phase-Locked Loop).  In this context, the term "PLL"
  is shorthand for "PLL-based clock".  Each display output (such as
  DisplayPort or HDMI) requires a clock signal, and this clock signal is
  provided by a DPLL, which is a functional unit in the graphics hardware.

* DPCD: DisplayPort Configuration Data.  This is a set of registers exposed
  by a DisplayPort sink device.  Each register is a byte in size.  The DPCD
  can be read and written over the DisplayPort Aux channel using "native"
  read and write messages, each of which can read or write a range of
  bytes.

* EDID: Extended Display Identification Data, a standard for data
  describing a display.  This data is provided by a display over the DDC
  protocol.
