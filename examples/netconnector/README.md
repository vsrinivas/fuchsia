# Application netconnector_example

The application `netconnector_example` runs either as an example NetConnector
requestor or as a responder.

With no arguments, `netconnector_example` runs as a responder and implements
the `Responder` interface. Typically, it's invoked this way by `bootstrap` by
virtue of being registered as a service in `bootstrap`'s `services.config`.

With the `--request-device=<name>` option, `netconnector_example` runs as a
requestor, requesting the `example` responder on the specified device. The
device name must be registered with the `netconnector` listener, and
`netconnector_example` must be registered as a responder with the running
listener on the specified device.

Here's an example of `netconnector_example` being registered in the
`netconnector` config file:

    {
      "host": "my_acer",
      "responders": {
        "example": "netconnector::ExampleResponder"
      },
      "devices": {
        "acer": "192.168.4.118",
        "nuc": "192.168.4.60"
      }
    }

Here's `netconnector_example` being resistered in `bootstrap`'s
`services.config` file:

    {
      "services": {
        ...
        "netconnector::ExampleResponder":
            "file:///system/apps/netconnector_example",
        ...
      }
    }

The `netconnector_example` requestor and responder have a short conversation
which appears as log messages.
