# DID PTS Instructions

## Setup
Source code setup:
* These tests require a build that disables all external profile clients (sometimes called an "arrested" build).
* Follow the instructions in `//src/connectivity/bluetooth/examples/bt-device-id-client/README.md` to include the `bt-device-id-client` utility in the build.

## IXIT Values
Use all the default settings.

## Default test instructions
1. (target shell) `bt-cli`
2. (bt-cli) `discoverable`
3. (host shell) `ffx component run /core/ffx-laboratory:bt-device-id-client fuchsia-pkg://fuchsia.com/bt-device-id-client#meta/bt-device-id-client.cm`
4. *Start test*

## TESTS

### DID/SR/SDI/BV-01-I
Use default test instructions.

### DID/SR/SDI/BV-02-I
Use default test instructions.

### DID/SR/SDI/BV-03-I
Use default test instructions.

### DID/SR/SDI/BV-04-I
Use default test instructions.
