# Bluetooth v2 Manifest Integration Tests

A series of integration-style tests to validate the correctness of CFv2 manifests. These
tests only validate the .CML declaration and do not test component behavior.

## Build Configuration

Include `--with //src/connectivity/bluetooth/tests` in your `fx set`.

To run the AVRCP smoke test:

`fx test bt-avrcp-smoke-test`

To run the AVRCP-Target smoke test:

`fx test bt-avrcp-target-smoke-test`

To run the RFCOMM smoke test:

`fx test bt-rfcomm-smoke-test`

To run the HFP Audio Gateway smoke test:

`fx test bt-hfp-audio-gateway-smoke-test`

To run the A2DP smoke test:
`fx test bt-a2dp-smoke-test`

To run the bt-init smoke test:
`fx test bt-init-smoke-test`

To run the bt-gap smoke test:
`fx test bt-gap-smoke-test`
