# Application netconnector_example

The application `netconnector_example` runs either as an example NetConnector
requestor or as a responding service. As a responding service, it can either
be registered in NetConnector's configuration, or it can register itself with
NetConnector as a running service provider.

With no arguments, `netconnector_example` runs as a responding service.
It's invoked this way by `netconnector` by virtue of being registered as a
service in `netconnector.config` (or on the `netconnector` command line).

With the `--register-provider` option, `netconnector_example` also runs as a
responding service, but it doesn't need to be registered as a service using
the NetConnector config. Instead, the user invokes `netconnector_example` with
`--register-provider`, and `netconnector_example` registers that running
instance with NetConnector. This allows the user to decide what context the
responding service should run in.

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
