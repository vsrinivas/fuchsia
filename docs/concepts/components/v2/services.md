# Services

<<../_v2_banner.md>>

A service provides a set of [FIDL][glossary.fidl] [protocols][glossary.protocol]
over a [channel][glossary.channel].

## Instances

Multiple named instances of a service can be hosted by a single component.
If no instance name is provided, the default instance name `default` is used.

A component can also access multiple instances in its incoming namespace.
These are presented in the incoming namespace as subdirectories of the service.

For example, the Launcher service with instance `default` would be accessible
at the path `/svc/fuchsia.sys.Launcher/default`.

## Protocols

A service is a grouping of named FIDL [protocols][glossary.protocol].
Logically-related protocols can be aggregated into a service and routed as a
single unit.

An example of a FIDL service definition (defined in fuchsia.network):

```fidl
service Provider {
    fuchsia.net.NameLookup name_lookup;
    fuchsia.posix.socket.Provider socket_provider;
}
```

Each protocol has a name and is accessible as a subdirectory of the service
instance. For example, the `socket_provider` protocol of the
`fuchsia.network.Provider` service instance `default` is accessible at the path
`/svc/fuchsia.network.Provider/default/socket_provider`.

Note: If the instance name and protocol are known ahead of time, it is possible
to open the protocol directly with zero round-trips.

## Routing

Services are routed to other Components through
[service capabilities][service-capability].

[glossary.fidl]: /docs/glossary/README.md#fidl
[glossary.protocol]: /docs/glossary/README.md#protocol
[glossary.channel]: /docs/glossary/README.md#channel
[service-capability]: /docs/concepts/components/v2/capabilities/service.md
