# A2DP Loopback Test App

This app uses the Mock Piconet Server to launch both Sink and Source profiles in
a fake piconet, where they connect to each other and stream audio in an endless
loop using the real system audio services.

Pass the --duration flag to specify how long it should run.

## Build Configuration

Include `--with //src/connectivity/bluetooth/profiles/tests` in your `fx set`.

To run:

`fx shell run fuchsia-pkg://fuchsia.com/bt-a2dp-loopback#meta/bt-a2dp-loopback.cmx`
