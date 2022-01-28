LoWPAN OpenThread Driver
========================

This LoWPAN driver takes a `fuchsia.lowpan.spinel::Device` and provides
a `fuchsia.lowpan::Device`, which it registers with the LoWPAN Service.
The underlying implementation is provided by OpenThread.

### Connectivity State Diagram

![LoWPAN Connectivity State Diagram](doc/lowpan-connectivity-state.svg)
