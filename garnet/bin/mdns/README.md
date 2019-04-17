# mDNS Service and Utility

This directory contains the implementation of the fuchsia.mdns service and
mdns-util, a developer utility for issuing mDNS-related commands from a console.

## Service

The service subdirectory builds the `mdns` package, which provides the mDNS
service implementation, and the `mdns_config` package, which registers the
service and instructs the component manager to start the service on boot.

The mdns service will log mDNS-related network traffic if:

1. the `enable_mdns_trace` gn arg is set to `true`, and
2. tracing is turned on using mdns-util (`$ mdns-util verbose`).

## Utility

The utility, `mdns-util` has the following usage:

```
commands:
    resolve <host_name>
    subscribe <service_name>
    respond <service_name> <instance_name> <port>
    verbose (requires enable_mdns_trace gn arg)
    quiet (requires enable_mdns_trace gn arg)
options:
    --timeout=<seconds>       # applies to resolve
    --text=<text,...>         # applies to respond
    --announce=<subtype,...>  # applies to respond
options must precede the command
<host_name> and <instance_name> cannot end in '.'
<service_name> must start with '_' and end in '._tcp.' or '._udp.'
```

For detailed constraints on the format of strings, see the FIDL definition.

- **resolve** resolves a host name to one or two IP addresses.
- **subscribe** searches for service instances on the local subnet.
- **respond** publishes a service using instance on the local subnet.
- **verbose** turns on mDNS traffic logging.
- **quiet** turns off mDNS traffic logging.

In order for traffic logging to work, the `enable_mdns_trace` gn arg must be
set during the build.
