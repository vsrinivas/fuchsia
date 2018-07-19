# Audio DFX Tests

This directory contains the sources for a test binary that validates a DFX
shared library implementation. The `audio_dfx_tests.cc` file contains tests to
verify all of the 'C' functions comprising the audio DFX binary interface. It
also includes tests that validate (at a very basic level) the correct processing
of the actual audio processing performed by the effects in the shared library.
Just as the `audio_dfx/lib` directory contains sources from which a vendor can
build their own DFX library implementation, similarly `audio_dfx/test` contains
a suggested starting point for the tests that vendors should create to verify
correct operation of their library.

Coupled with the `audio_device_fx.h` file from parent directory, this example
test binary is built along with the `libaudio_dfx.so` shared library itself.
Note that the two binaries (library and test) are built in the same package, so
that the test binary can directly load and call the library.
