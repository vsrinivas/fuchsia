# Fuchsia Audio Device Output harness

This component creates audio output using the fuchsia-audio-device-output,
by creating an instance of the driver and directly connecting it to the
AudioDeviceEnumerator provided to the component. This can be used to test
system behavior or for integration testing of fuchsia-audio-device-output.
