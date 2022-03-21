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
the stack, a test can easily use mock implementations of context traits. This
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

[DAD implementation]: https://fuchsia-review.googlesource.com/c/fuchsia/+/648202
[Originally in Netstack3]: https://cs.opensource.google/fuchsia/fuchsia/+/07b825aab40438237b2c47239786aae08c179139:src/connectivity/network/netstack3/
[subnet-router anycast address]: https://datatracker.ietf.org/doc/html/rfc4291#section-2.6.1
[RFC 6724 Section 2.2]: https://datatracker.ietf.org/doc/html/rfc6724#section-2.2
[RFC 6724 Section 5]: https://datatracker.ietf.org/doc/html/rfc6724#section-5
[RFC 6724 Section 6]: https://datatracker.ietf.org/doc/html/rfc6724#section-6
