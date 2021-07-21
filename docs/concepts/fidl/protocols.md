# Protocols

A [FIDL][glossary.fidl] protocol groups methods and events to describe how one
process interacts with another over a [channel][glossary.channel].

```fidl
library fuchsia.examples;

const uint64 MAX_STRING_LENGTH = 32;

[Discoverable]
protocol Echo {
    EchoString(string:MAX_STRING_LENGTH value) -> (string:MAX_STRING_LENGTH response);
    SendString(string:MAX_STRING_LENGTH value);
    -> OnString(string:MAX_STRING_LENGTH response);
};
```

Note: For more details on FIDL protocol syntax, see the
[FIDL language reference][fidl-reference].

Protocol implementations are served from provider components using the
[outgoing directory][glossary.outgoing-directory] and consumed from another
component's [namespace][glossary.namespace].

For example, the implementation of the `fuchsia.examples.Echo` protocol above
would be present in a component's namespace at the path
`/svc/fuchsia.examples.Echo`.

## Routing

Protocols are routed to components through
[protocol capabilities][protocol-capability].

[fidl-reference]: /docs/reference/fidl/language/language.md
[glossary.channel]: /docs/glossary/README.md#channel
[glossary.fidl]: /docs/glossary/README.md#fidl
[glossary.namespace]: /docs/glossary/README.md#namespace
[glossary.outgoing-directory]: /docs/glossary/README.md#outgoing-directory
[protocol-capability]: /docs/concepts/components/v2/capabilities/protocol.md