# Application netconnector

The application `netconnector` runs either as the NetConnector service (the
'listener') or as a utility for managing the service.

As listener, `netconnector` implements the NetConnector interface described in
[netconnector.fidl](https://fuchsia.googlesource.com/netconnector/+/master/services/netconnector.fidl). Clients ('requestors') that want to initiate communication with a
service on a remote machine call `GetDeviceServiceProvider` and then
`ConnectToService` on the service provider. Apps that want to respond
need to be registered with `netconnector`, typically via its config file.

`netconnector` has command line options, and it will read a config file. It uses
the config when it runs as listener.

The command line options for `netconnector` are:

    --show-devices              show a list of known devices
    --config=<path>             use <path> rather than the default config file
    --listen                    run as listener

The `--show-devices` option is only relevant when `netconnector` is running as
a utility. `--config` is only relevant to the listener.

The `--listen` option makes `netconnector` run as listener. Typically, this
argument is only used in the context of `bootstrap`'s `services.config` file.
This is because applications started from the command line are always different
instances from applications started by `bootstrap`. In other words, you can
start a listener from the command line, but it will only conflict with the
instance that requestors will get when they connect to the service.

The option `--config=<path>` controls which config file
`netconnector` reads. The default config file is located at
`/system/data/netconnector/netconnector.config`.

The config file provides a means of registering services and devices. A config
file looks like this:

    {
      "services": {
        "netconnector::Example": "file:///system/apps/netconnector_example"
      },
      "devices": {
        "acer": "192.168.4.118",
        "nuc": "192.168.4.60"
      }
    }

Devices are configured in this way if, for some reason, mDNS discovery isn't
usable in a given network configuration.

As mentioned previously, the `listen` option should generally only be used in
`bootstrap`'s `services.config` file. For example:

    {
      "services": {
        ...
        "netconnector::NetConnector": [
            "file:///system/apps/netconnector", "--listen"
        ],
        ...
      }
    }

This registers `netconnector` under the default interface name for the
`NetConnector` interface. The first time something attempts to connect to
that service, `netconnector` will start up, read its config file and run
indefinitely as listener. Note that `bootstrap` won't start `netconnector`
unless something tries to connect to it. Typically, that something would be
`netconnector` running in its utility mode.

Provided the listener is registered with `bootstrap` as shown above, the
listener can be started like this:

    $ netconnector

Requestors should run in the same context or a child context so they have
access to the listener.

Currently, there is no support in the utility for
stopping the listener. The listener can be stopped by killing its process.
