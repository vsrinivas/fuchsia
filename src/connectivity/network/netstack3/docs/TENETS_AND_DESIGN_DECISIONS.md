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

[DAD implementation]: https://fuchsia-review.googlesource.com/c/fuchsia/+/648202
