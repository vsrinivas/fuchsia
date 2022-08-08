# A2DP Loopback Test

This test uses the Mock Piconet Server to start both A2DP Sink and Source profiles in a fake
piconet, where they connect to each other and stream audio in an endless loop using the real system
audio services. This test should only be run in environments with real hardware.

## Build Configuration

Include `--with //src/connectivity/bluetooth/profiles/tests` in your `fx set`.

To run:

`fx test -vo bt-a2dp-loopback-test`
