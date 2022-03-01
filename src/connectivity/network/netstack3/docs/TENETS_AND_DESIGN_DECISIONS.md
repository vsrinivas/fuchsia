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

[DAD implementation]: https://fuchsia-review.googlesource.com/c/fuchsia/+/648202
[Originally in Netstack3]: https://cs.opensource.google/fuchsia/fuchsia/+/07b825aab40438237b2c47239786aae08c179139:src/connectivity/network/netstack3/
