# Services

<<../_v2_banner.md>>

A service provides a set of [FIDL][glossary.fidl]
[protocols][glossary.protocol]
over a [channel][glossary.channel].

## Instances

Multiple named instances of a service can be hosted by a single component.
These are presented in the [namespace][glossary.namespace] of the consuming
component as subdirectories of the service.

For example, a component hosting different implementations of
`fuchsia.sys.Launcher` might expose a `privileged` and `sandboxed` instance.
These instances would be accessed by a client using the paths
`/svc/fuchsia.sys.Launcher/privileged` and
`/svc/fuchsia.sys.Launcher/sandboxed` respectively.

### Default instance

By convention, if a service only ever has a single instance, or if clients of
a service typically don't care which instance they connect to, the provider of
the service should expose an instance named `default`.

For example, if clients typically don't care about which `fuchsia.sys.Launcher`
implementation they use, the providing component could expose a `default`
instance that the client accesses using the path
`/svc/fuchsia.sys.Launcher/default`.

Default instances are a useful convention that allow the caller to avoid
enumerating instances.

### Services routed from collections {#services-routed-from-collections}

When a [service is exposed][services-from-collections] from a
[dynamic collection][collection], the service instances of each component in
the collection are aggregated into a single namespace for the client to
access. The instances are named with the scheme
`"$component_name,$instance_name"`. For example, for a component named `pcie`
within a collection that exposes a service instance named `0`, the resulting
path would be `/svc/fuchsia.dev.Block/pcie,0/protocol`.

The `$component_name` is the name given to the
[`fuchsia.sys2/Realm.CreateChild`][realm.fidl] API when the component is
created. The `$instance_name` is defined by the component itself.

Services exposed from collections do not have a natural `default` instance.
Clients must always enumerate the service directory to see which instances are
available.

## Protocols

A service is a grouping of named FIDL [protocols][glossary.protocol].
Logically-related protocols can be aggregated into a service and routed as a
single unit.

An example of a FIDL service definition (defined in fuchsia.network):

```fidl
service Provider {
    fuchsia.net.name.Lookup name_lookup;
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

[collection]: /docs/concepts/components/v2/realms.md#collections
[glossary.channel]: /docs/glossary/README.md#channel
[glossary.fidl]: /docs/glossary/README.md#fidl
[glossary.namespace]: /docs/glossary/README.md#namespace
[glossary.protocol]: /docs/glossary/README.md#protocol
[realm.fidl]: https://fuchsia.dev/reference/fidl/fuchsia.sys2#Realm
[service-capability]: /docs/concepts/components/v2/capabilities/service.md
[services-from-collections]: /docs/concepts/components/v2/capabilities/service.md#routing-service-capability-collection
