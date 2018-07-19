# DFX Shared Library for Testing

This directory contains the sources for a `libaudio_dfx.so` binary that
implements the ABIs and essential logic of a DFX shared library. This includes
an implementation of the basic 'C' interface (`dfx_lib.cc`), a base class for
device effects (`dfx_base.cc`/`dfx_base.h`), and three effects derived from that
class (delay, swap, and rechannel -- `dfx_delay.cc`/`dfx_delay.h`, `dfx_swap.h`
and `dfx_rechannel.h` respectively).

Coupled with the `audio_device_fx.h` file from parent directory, this example
shared library is built along with a `audio_dfx_tests` test binary that verifies
its correct operation. Note that the two binaries (library and test) are built
in the same package, so that the tests can directly load and call the library.
