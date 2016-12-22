# Application netconnector_example

The application `netconnector_example` runs either as an example NetConnector
requestor or as a responding service.

With no arguments, `netconnector_example` runs as a responding service.
It's invoked this way by `netconnector` by virtue of being registered as a
service in `netconnector.config`.

With the `--request-device=<name>` option, `netconnector_example` runs as a
requestor, requesting the example responding service on the specified device.
The device name must be registered with the `netconnector` listener, and
`netconnector_example` must be registered as a responding service with the
running listener on the specified device.

Here's an example of `netconnector_example` being registered in the
`netconnector` config file:

    {
      "host": "my_acer",
      "services": {
        "netconnector::Example": "file:///system/apps/netconnector_example"
      },
      "devices": {
        "acer": "192.168.4.118",
        "nuc": "192.168.4.60"
      }
    }

The `netconnector_example` requestor and responding service have a short
conversation which appears as log messages.
