# FFX Connection Test

This test verifies the behavior and stability of the connection that proxies
fidl protocols between ffx on a host device and a component on a target device.

## Setup

These tests connect to [RCS][rcs] on the target and use it to launch a puppet
component under /core/ffx-laboratory and connect to it using a test protocol.
It then uses the test protocol to reproduce edge cases that stress the proxy
connection.

## Running

To build, add `--with //src/developer/ffx:tests` to your `fx set` command.

Make sure a target device is running, and execute the test with

```
FUCHSIA_NODENAME=<device nodename> fx test ffx_connection_test
```

[rcs]: /src/developer/remote-control
