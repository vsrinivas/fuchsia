# Netstack3's tenets and documentation for design decisions

This document enumerates tenets and design decisions for Netstack3.

The tenets below are not immutable and should be changed as needed when new
information or requirements introduces an issue with an existing tenet. In the
absence of new information, the tenets below should be followed to ensure
consistent practices/patterns across Netstack3.

## Define API boundaries with traits

Implement modules/protocols/features with a context trait that explicitly spells
out their dependencies. Examples of dependencies may be:

- Getting access to immutable or mutable state.
- Sending packets.
- Manipulating timers.
- Incrementing counters.

It helps to think about what module/protocol/component owns the state and what
work is being performed to define where API boundaries should be drawn and how
to structure traits.

### Why?

#### API separation

By defining traits to enumerate dependencies, an implementation may be written
without knowledge of how the rest of the stack is implemented.

This comes with the benefit that baked in-assumptions are more easily audited
by looking at the traits and state access is limited to what is needed instead
of having access to everything, making the implementation easier to reason
about.

#### Testability

One of the pain points with testing in Netstack2 is that tests need to spin up a
whole netstack to test relatively small features.

By using traits to define all of an implementation's dependencies on the rest of
the stack, a test can easily use fake implementations of context traits. This
reduces the minimum testable unit which produces more targeted tests.

### How

For each module/feature/component, introduce a context trait that defines
dependencies through trait methods. Name the trait `XxxContext` where `Xxx`
reflects the module/feature/component the trait is for.

- Prefer making the introduced context trait a supertrait of predefined context
  traits like `RngContext`, `TimerContext` and `InstantContext` found in the
  `context` module in Netstack3, where possible.
- If some methods for a module only require a subset of trait type parameters,
  prefer splitting into multiple traits (e.g. `XxxContext` and
  `BufferXxxContext<B: BufferMut>`) and reference the narrowest trait as a bound
  in functions.
- Prefer a traits that may be generalized with type parameters where possible,
  using type-specific traits as needed (e.g. `IpDeviceContext<I: Ip>` and
  `Ipv6DeviceContext`).

#### Example

See the [DAD implementation] for an example of an implementation defining its
dependencies through a `Context` trait with narrowly scoped tests.

## Do not assume exclusive access to state

Interfaces/traits defining API boundaries should be written in a way that is
agnostic to whether or not they have exclusive access to state.

It is okay to assume exclusive access within an API boundary, generally within a
module such as PMTU, fragmentation and IP devices.

### Background

[Originally in Netstack3], a single structure was passed around to hold the
state for all of the netstack. Every operation would receive an immutable or
mutable reference to the state to get or update state as everything was
implemented assuming a single threaded environment so there was no need for
locking/shared state.

Note that we are actively working to move away from this. See below.

### Why?

Netstack3 will need to support running in a multi-threaded environment and as a
result, share state across threads. Assuming exclusive access makes it
impossible to share the state with other workers/threads that may need
concurrent access to shared state.

### How?

Use the `with_state` pattern; functions that accept a callback which take a
reference to state (see Patterns below).

The specific details are to be determined. The work to research/experiment and
then finally migrate Netstack3 to a world where interior-mutability is the norm
is captured in <https://fxbug.dev/48578>.

#### Patterns
```rust
trait Context {
    // Do not return mutable references to state from context traits as this
    // assumes exclusive access.
    //
    // Note that when using types like `Rc` and `Arc` with `RefCell` and
    // `Mutex`/`RwLock`, it is not possible to safely return a reference to
    // state that is not wrapped in a guard type.
    fn get_state_mut(&mut self) -> &mut State;

    // Ok to accept a callback that accepts a mutable reference as the shared
    // state is passed through API boundaries (the `with_state_mut` method)
    // without assuming exclusive access.
    //
    // Note that encoding exclusivity is more powerful and should be preferred
    // where possible.
    fn with_state_mut<O, F: FnOnce(&mut State) -> O>(&mut self) -> O;
}
```

## Do not hold prefix length for IPv6 addresses

Generic IPv6 address state will not hold a prefix length. The prefix length may
still be held by other protocol-specific state for an IPv6 address such as
SLAAC.

During investigation/research, it was determined that although IPv4 depends on
the prefix length to determine whether an address is unicast in the context of
its subnet, and the broadcast and network address for the address's subnet, IPv6
does not depend on the prefix length in the same way. Unicast-ness of an IPv6
address may be determined by observing the address itself and IPv6 does not have
a notion of broadcast addresses. IPv6 does make use of the "network address" to
support a [subnet-router anycast address] but adding an anycast address will be
an explicit operation - in the same way multicast addresses/groups are
explicitly added/joined.

It should be noted that, on Fuchsia, adding an address with the
`fuchsia.net.interfaces.admin` FIDL library does not implicitly create
an on-link route for the prefix. This is similar to adding an address on Linux
using `ip-address(8)` with the `noprefixroute` flag. Note that previous
(deprecated) iterations of network configuration FIDL libraries such as
`fuchsia.netstack` and `fuchsia.net.stack` did not have the same opinion.

Given that the prefix of an address will not be used to implicitly manipulate
on-link subnet routes or anycast addresses, there is no need to hold the prefix
length with all IPv6 addresses.

### Default address selection

Per [RFC 6724 Section 2.2],

    We define the common prefix length CommonPrefixLen(S, D) of a source
    address S and a destination address D as the length of the longest
    prefix (looking at the most significant, or leftmost, bits) that the
    two addresses have in common, up to the length of S's prefix (i.e.,
    the portion of the address not including the interface ID).  For
    example, CommonPrefixLen(fe80::1, fe80::2) is 64.

Our policy of not storing the prefix introduces a conflict with this definition
of `CommonPrefixLen(S, D)` - by not holding the prefix length, we can no longer
cap `CommonPrefixLen(S, D)` to the length of `S`'s prefix.

For source address selection, per [RFC 6724 Section 5],

    ...

    Rule 8: Use longest matching prefix.
    If CommonPrefixLen(SA, D) > CommonPrefixLen(SB, D), then prefer SA.
    Similarly, if CommonPrefixLen(SB, D) > CommonPrefixLen(SA, D), then
    prefer SB.

    Rule 8 MAY be superseded if the implementation has other means of
    choosing among source addresses.  For example, if the implementation
    somehow knows which source address will result in the "best"
    communications performance.

For destination address selection per [RFC 6724 Section 6],

    ...

    Rule 9: Use longest matching prefix.
    When DA and DB belong to the same address family (both are IPv6 or
    both are IPv4): If CommonPrefixLen(Source(DA), DA) >
    CommonPrefixLen(Source(DB), DB), then prefer DA.  Similarly, if
    CommonPrefixLen(Source(DA), DA) < CommonPrefixLen(Source(DB), DB),
    then prefer DB.

    Rule 10: Otherwise, leave the order unchanged.
    If DA preceded DB in the original list, prefer DA.  Otherwise, prefer
    DB.

    Rules 9 and 10 MAY be superseded if the implementation has other
    means of sorting destination addresses.  For example, if the
    implementation somehow knows which destination addresses will result
    in the "best" communications performance.

This is not an issue because for both source and destination address selection:

- The limit of the source address's prefix length is irrelevant when
  communicating with a remote destination on a different subnet as different
  subnets will have different prefixes.
- Rules affecting interoperability precede rules 8 and 9 for source and
  destination address selection, respectively.
- The rule that requires using the longest prefix match, CommonPrefixLen(S, D),
  is allowed to be superseded by the implementation which will not limit the
  common prefix length to the source address's prefix length.

### FIDL

The decision to not include a prefix length for an interface's IPv6 address
state is captured by `fuchsia.net/InterfaceAddress`.

## Expose state deltas through events dispatched from Core

Changes to stack state are exposed through atomic events, which are issued as
close to the internal state change as possible. Events are descriptive enough
that reconstruction of state is possible from observing all events and to
support FIDL APIs such as [`fuchsia.net.interfaces/Watcher`].

No other side effects besides state-keeping are allowed in event handlers -
those are not meant as callbacks. The API is named in a way to discourage usage
as callbacks: `on_event(&mut self, event: T)`.

Core calls event handlers through context traits from within critical sections
to guarantee ordering. Bindings will typically write these events into
order-preserving channels and can reconstruct up-to-date state in order to serve
it over FIDL at a later time, while no longer holding locks. Justifying the
decision to use this pattern:

* Bindings can keep track of state to serve over FIDL without having to
  acquire locks to read it.
* All exposed state goes through a single API as opposed to a split hypothetical
  *getter* vs *observer* APIs to satisfy out-of-stack watchers and hanging-gets.
* Exposing events from within the critical sections guarantees the state is
  always constructible if written to an ordering-guaranteeing channel. The
  alternative of issuing all events from Bindings is not really viable because:
  * To guarantee linearity, Bindings would always have to call into Core with
    locks held or within the same thread to avoid invalid state being observed
    from the event handler's perspective; those are undesirable.
  * Core can't really be entirely side-effect free from a state-keeping
    perspective. We *do* want to reduce side effects, but things like DAD or,
    notably, neighbor reachability states - both of which are observable over
    FIDL - belong in Core.

At the moment of writing, this pattern is applicable to the state-watching
present in `fuchsia.net.interfaces/Watcher`,
`fuchsia.net.interfaces.admin/AddressStateProvider`,
`fuchsia.net.neighbor/EntryIterator`, and a hypothetical
`fuchsia.net.routes/Watcher`.

## API compatibility with Netstack2

Netstack3 must be fully API compatible with Netstack2 and, when it comes the
time to transition to it, we must be able to drop it in the network realm
without any change to API clients.

### Why?

A drop-in replacement means we can iteratively assert proper operation and
feature-completeness without carrying the burden of maintaining parallel client
implementations. Similarly, it decreases the cost of parallel test cases and
tooling. The shared test batteries also increase confidence since they run
against the production-ready Netstack2.

Furthermore, it allows an incremental approach to enabling Netstack3 in specific
product configurations, possibly accelerating real-world adoption before it is
at full parity with Netstack2.

### How?

As Netstack3 progresses towards feature-completeness we will iteratively enable
[FIDL integration] and [POSIX][posix-tests] tests. In the event we discover
shortcomings in the API surface, we *may* update existing APIs, but Netstack2's
implementation must be carried forward with it.

Note that `inspect` data is not considered part of the API surface and is not
part of this contract. Netstack3 is not expected to generate fully compatible
debugging or metrics information.

## Core crate public API

Netstack3 core will expose symbols through its internal module structure, and
avoid re-exporting symbols from different levels.

### Why?

Pros:
- Allows shorter naming of symbols while maintaining a clean API.
  `...udp::{Socket, Id, State}`  stutters less than `{UdpSocket, UdpId,
  UdpState}`.
- Public visibility of a symbol is immediately evaluated by the presence of the
  `pub` keyword. Note that its full module path must also be publicly exported
  for this to be true, which is taken to be true given the proposed pattern.
- Navigating to symbol definitions without reference-resolving tools is easier.

Cons:
- Exposes internal code organization, which may be brittle.

We find the pros outweigh the cons. Most of the code-movement thrash is expected
to happen while netstack3_core is part of the Fuchsia monorepo, which means all
known users can be fixed at the same time the movement happens, without external
breakages. This pain is expected to be reduced once the project reaches
maturity.

### How

All modules that contain `pub` symbols must themselves be made `pub`, so they
make it into the public API through the expected module tree.

No modules may re-export symbols.

Symbol names avoid stuttering, i.e. prefer `udp::Socket` to `udp::UdpSocket`,
`ip::Device` to `ip::IpDevice`, and `tcp::connect` to `tcp::tcp_connect`.

Avoid importing symbols directly, import their parent module instead, i.e.
prefer `use crate::transport::udp` to `use crate::transport::udp::Socket`.

> TODO(https://fxbug.dev/105636): We've decided to adopt this guidance, but
> defer renaming any symbols for now to focus on more important milestones. We
> still wish to a world with clearer imports and less stuttering, but the
> codebase may not reflect this until we decide to flip the switch on existing
> code.

[`fuchsia.net.interfaces/Watcher`]: https://fuchsia.dev/reference/fidl/fuchsia.net.interfaces?hl=en#Watcher
[DAD implementation]: https://fuchsia-review.googlesource.com/c/fuchsia/+/648202
[Originally in Netstack3]: https://cs.opensource.google/fuchsia/fuchsia/+/07b825aab40438237b2c47239786aae08c179139:src/connectivity/network/netstack3/
[subnet-router anycast address]: https://datatracker.ietf.org/doc/html/rfc4291#section-2.6.1
[RFC 6724 Section 2.2]: https://datatracker.ietf.org/doc/html/rfc6724#section-2.2
[RFC 6724 Section 5]: https://datatracker.ietf.org/doc/html/rfc6724#section-5
[RFC 6724 Section 6]: https://datatracker.ietf.org/doc/html/rfc6724#section-6
[FIDL integration]: /src/connectivity/network/tests/fidl
[posix-tests]: /src/connectivity/network/tests/bsdsocket_test.cc
