# Services

A service provides a set of [FIDL protocols][glossary.protocol] over a
[channel][glossary.channel].
Logically-related protocols can be aggregated into a service and routed as a
single unit.

```fidl
library fuchsia.examples;

const MAX_STRING_LENGTH uint64 = 32;

@discoverable
protocol Echo {
    EchoString(struct {
        value string:MAX_STRING_LENGTH;
    }) -> (struct {
        response string:MAX_STRING_LENGTH;
    });
    SendString(struct {
        value string:MAX_STRING_LENGTH;
    });
    -> OnString(struct {
        response string:MAX_STRING_LENGTH;
    });
};

service EchoService {
    regular_echo client_end:Echo;
    reversed_echo client_end:Echo;
};
```

Note: For more details on FIDL protocol syntax, see the
[FIDL language reference][fidl-reference].

The service identifies each protocol by a unique name. Components access these
protocols using this name from within their [namespace][glossary.namespace].

For example, the `fuchsia.examples.EchoService` instance above provides the
following namespace paths to access the protocols it contains:

- `/svc/fuchsia.examples.EchoService/default/regular_echo`
- `/svc/fuchsia.examples.EchoService/default/reversed_echo`

The `default` marker in the above paths identifies the **service instance**.

## Instances {#instances}

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

## Routing {#routing}

Services are routed to components through
[service capabilities][service-capability].

[fidl-reference]: /docs/reference/fidl/language/language.md
[glossary.channel]: /docs/glossary/README.md#channel
[glossary.namespace]: /docs/glossary/README.md#namespace
[glossary.protocol]: /docs/glossary/README.md#protocol
[service-capability]: /docs/concepts/components/v2/capabilities/service.md
