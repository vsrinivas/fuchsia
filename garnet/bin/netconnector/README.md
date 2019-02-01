# NetConnector Service/Utility

## Design

NetConnector is a Fuchsia service intended to unblock development of
cross-device scenarios. It has two responsibilities:

- To serve, in LAN scope, as a rendezvous between services and the clients that want to use those services.
- To provide zx::channel forwarding so that clients and services can communicate using channels.

In addition, NetConnector hosts an mDNS implementation, which it uses to
enumerate Fuchsia devices on the LAN and which it offers as a separate service.

The word 'service' is used a lot in this document and in NetConnector. It has
a specific meaning in the context of mDNS, but otherwise, it really just means
'service' in the Fuchsia sense. A Fuchsia service is running software that
talks to a client over an zx::channel. It's normal practice to bind such a
channel to a FIDL proxy. This isn't done over NetConnector, because FIDL isn't
an suited to RPC.

Here’s the [NetConnector interface](../../public/fidl/fuchsia.netconnector/netconnector.fidl).

`RegisterServiceProvider` allows a running service to register its availability.
The method specifies a service name and a `ServiceProvider`.
Services can also be registered using a config file in the manner of sysmgr.
Like sysmgr, NetConnector will launch services on demand.

`GetDeviceServiceProvider` gets a `ServiceProvider` that provides any of the
services registered on the specified device.

`GetKnownDeviceNames` returns the names of Fuchsia devices discovered on the
LAN.

Services are identified by name. NetConnector doesn’t provide any form of
service enumeration, nor is there any notion of service type. Clients need to
know the name of the device and service they want to connect to, though
`GetKnownDeviceNames` could be used to find a known service on an otherwise
unknown device. In our current work, ledger is used to establish the device and
service names.

zx:channel provides a full-duplex message transport that can carry binary data
and handles. The channel forwarding implemented by NetConnector allows data
only; no handles are permitted in messages. I’ve experimented with support for
channel handles, which is doable and potentially useful.

NetConnector uses mDNS for Fuchsia device discovery and address resolution. It
listens for TCP connections on a single port. A separate TCP connection is
established for each `ConnectToService` call made to a `ServiceProvider` returned
by `GetDeviceServiceProvider`. A session lasts until either the client or
service closes its end of the virtual channel or until the TCP connection fails.

The mDNS implementation is also exposed separately using
[this interface](../../public/fidl/fuchsia.netconnector/mdns.fidl).

## Operation

The application `netconnector` runs either as the NetConnector service (the
'listener') or as a utility for managing the service. The NetConnector service
is started as part of the Fuchsia boot sequence, and is available in the default
application context.

As listener, `netconnector` implements the NetConnector interface described in
[netconnector.fidl](../services/netconnector.fidl). Clients ('requestors') that want to initiate communication with a
service on a remote machine call `GetDeviceServiceProvider` and then
`ConnectToService` on the service provider. Apps that want to respond
need to be registered with `netconnector`, typically via its config file.

`netconnector` has command line options, and it will read a config file. It uses
the config when it runs as listener.

The command line options for `netconnector` are:

    --show-devices              show a list of known devices
    --mdns-verbose              show mDNS traffic in the log
    --config=<path>             use <path> rather than the default config file
    --listen                    run as listener

The `--show-devices` option is only relevant when `netconnector` is running as
a utility. `--config` is only relevant to the listener.

The `--listen` option makes `netconnector` run as listener. Typically, this
argument is only used in the context of `sysmgr`'s `services.config` file.
This is because applications started from the command line are always different
instances from applications started by `sysmgr`. In other words, you can
start a listener from the command line, but it will only conflict with the
instance that requestors will get when they connect to the service.

The option `--config=<path>` controls which config file
`netconnector` reads. The default config file is located at
`/pkg/data/netconnector.config`.

The config file provides a means of registering services and devices. A config
file looks like this:

    {
      "services": {
        "netconnector.Example": "netconnector_example"
      },
      "devices": {
        "acer": "192.168.4.118",
        "nuc": "192.168.4.60"
      }
    }

Devices are configured in this way if, for some reason, mDNS discovery isn't
usable in a given network configuration.

As mentioned previously, the `listen` option should generally only be used in
`sysmgr`'s `services.config` file. For example:

    {
      "services": {
        ...
        "netconnector.NetConnector": [
            "netconnector", "--listen"
        ],
        ...
      }
    }

This registers `netconnector` under the default interface name for the
`NetConnector` interface. NetConnector is started as part of the boot sequence.

Currently, there is no support in the utility for
stopping the listener. The listener can be stopped by killing its process.
