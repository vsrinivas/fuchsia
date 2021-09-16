# Fuchsia Networking API Evolution

Fuchsia Networking offers a broad range of APIs with sometimes overlapping
functionality. This document is the basis of ongoing API evolution.

## Background

At the time of writing, Fuchsia Networking APIs are the result of rapid
evolution and often-unintentional stabilization, resulting in a large surface
with out-of-tree consumers. Many of these APIs evolved before newer FIDL
features were available, making in-place migration even more difficult.

More than other system areas, Networking must be mindful of its APIs becoming
prematurely load bearing.

## Design Principles

### Deliberate Stabilization

Consider this excerpt from the [FIDL API Rubric][library_structure]:

> How you decompose these definitions into libraries has a large effect on the
> consumers of these definitions because a FIDL library is the unit of
> dependency and distribution for your protocols.

Be mindful of this when creating new protocols. Undue grouping of protocols,
even when semantically logical, can produce unwanted distribution coupling. In
other words, consider minting new libraries for new protocols to avoid premature
inclusion in the SDK.

Consider `fuchsia.net.Connectivity` as a motivating example: by having been
placed in `fuchsia.net`, its inclusion in the SDK was entangled with that
library; it was de-facto stabilized without a formal decision.

Note that `fuchsia.net.Connectivity` was removed in 5500eec6aa48c65516eae4d5ef4.

### Role Separation

The [FIDL API Rubric][library_structure] advises:

> To decide whether to decompose a library into smaller libraries, consider the
> following questions:
>
> - Do the customers for the library break down into separate roles that would
>   want to use a subset of the functionality or declarations in the library? If
>   so, consider breaking the library into separate libraries that target each
>   role.

We prefer a different framing in Networking: separate your API into different
roles by breaking it into multiple *protocols*; separate your API into different
stabilization units by breaking into multiple *libraries*.

Consider `fuchsia.netstack/Netstack` and `fuchsia.net.stack/Stack` as motivating
examples: neither encode access roles, requiring that any agent that wishes to
observe the system state is also granted access to modify it. In practice, this
needlessly grants consumers such as Chromium write-access to networking
internals.

### Cardinality

Avoid introducing APIs that are non-orthogonal to existing APIs. Providing
multiple entry points for the same purpose can lead to confusion for consumers
and subtle behaviour differences.

Consider `fuchsia.netstack/Netstack.GetInterfaces` and
`fuchsia.netstack/Netstack.OnInterfacesChanged` as motivating examples:
`OnInterfacesChanged` provides a superset of the functionality of
`GetInterfaces` by emitting a synthetic event upon binding, yet consumers
misunderstood and [misused the API][chromium_on_interfaces_changed].

### Prior Art and Compatibility

Consider compatibility with existing software when designing APIs. As Fuchsia's
product requirements grow in scope, so will the burden of porting software to
platform-specific APIs; consider how your API might be translated to an existing
API that is widely used in the industry. Furthermore, existing APIs have evolved
to provide solutions to problems we may not have considered, it behooves us to
learn from their efforts.

The obvious motivating example here is POSIX sockets; `fuchsia.posix.socket`
evolved directly to address this need, complete with the over-fitting described
above. At the time of writing, the API transports C structures on the wire
directly, creating portability problems and resulting in an ABI that is not
fully defined in FIDL.

A less-obvious motivating example is [`netlink`]; networking management APIs are
not standardized in POSIX, yet `netlink` is widely used in existing software to
interact with Linux's networking subsystems. Both `fuchsia.netstack/Netstack`
and `fuchsia.net.stack/Stack` were designed without considering equivalent
functionality provided by `netlink`.

### Time Pressure

It will sometimes be necessary to provide partners with SDK-exposed APIs on a
time scale that does not permit a reasoned general-purpose solution to be
designed. Prefer the narrowest possible design that provides for the partner's
needs to avoid acquiring unintended consumers. Provide your API in a new
protocol to ease future deprecation.

This approach allows us to respond quickly without compromising our design
principles in the long term.

## Site Survey and Ongoing Work

At present, these are the libraries considered in this document:

- `fuchsia.hardware.ethernet`
   + being replaced with `fuchsia.hardware.network`
   + neither is planned for SDK inclusion
- `fuchsia.net`
   + included in SDK
   + contains only common types, no protocols
-  fuchsia.net.dhcp
   + included in SDK via dependency in `fuchsia.netstack`
   + planned for removal from SDK
-  fuchsia.net.dhcpv6
   + not planned for SDK inclusion
- `fuchsia.net.filter`
   + not planned for SDK inclusion
   + contains only protocol `Filter`
   + needs rework in consideration of `NETLINK_NETFILTER` (undocumented?)
- `fuchsia.net.http`
   + out of scope of this document, not maintained by networking
- `fuchsia.net.mdns`
   + out of scope of this document. do we need to bring it in scope?
- `fuchsia.net.name`
   + not included in SDK
   + protocol `LookupAdmin` is the role-separated pair of `Lookup`
- `fuchsia.net.neighbor`
   + not planned for SDK inclusion
- `fuchsia.net.oldhttp`
   + out of scope of this document, not maintained by networking
   + only referenced in
     https://source.chromium.org/chromium/chromium/src/+/main:fuchsia/http/
   + slated for deletion
- `fuchsia.net.routes`
   + included in SDK
   + narrow route+MAC resolution API in service of partner needs
- `fuchsia.net.stack`
   + not included in SDK
   + broad general-purpose protocol `Stack` does not employ role separation,
     should be decomposed into several protocols after consulting industry
     standards
   + protocol `Log` to be replaced by https://fxbug.dev/54198
- `fuchsia.net.tun`
   + not included in SDK
   + ownership-based
- `fuchsia.netstack`
   + included in SDK
   + protocol `Netstack` being replaced in https://fxbug.dev/21222
- `fuchsia.posix.socket`
   + not included in SDK
   + used via fdio which is included in SDK
   + undergoing portability improvements in https://fxbug.dev/44347

[library_structure]: /docs/concepts/api/fidl.md#library_structure
[chromium_on_interfaces_changed]: https://chromium-review.googlesource.com/c/chromium/src/+/2331860
[`netlink`]: https://man7.org/linux/man-pages/man7/netlink.7.html
