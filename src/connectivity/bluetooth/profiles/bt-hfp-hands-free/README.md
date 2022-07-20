# Bluetooth Profile: Hands-Free Profile Hands Free

This component implements the Hands Free role of Hands-Free Profile (HFP)
version 1.8 as specified iby the Bluetooth SIG in the [official specification](https://www.bluetooth.org/DocMan/handlers/DownloadDoc.ashx?doc_id=489628).

## Running tests

HFP relies on unit tests to validate behavior. Add the following to your Fuchsia set configuration
to include the profile unit tests:

`--with //src/connectivity/bluetooth/profiles/bt-hfp-audio-gateway:bt-hfp-hands-free-tests`

To run the tests:

```
fx test bt-hfp-hands-free-tests
