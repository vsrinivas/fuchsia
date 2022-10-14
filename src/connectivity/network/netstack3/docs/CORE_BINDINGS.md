# Core and Bindings

This document describes and motivates the design of the netstack into separate
"core" and "bindings" components.

## Architecture

The netstack codebase is split into two crates - the `core` crate, and the
top-level `netstack3` crate. We refer to these as the "core" and bindings"
respectively.

The core is a library crate, and contains most of the logic of the netstack.
However, it is also platform-agnostic. Any client of the core which produces a
binary is known as "bindings" because they bind the core to a particular
platform. Currently, `netstack3` crate, which provides bindings to Fuchsia, is
the only bindings crate in existence.

## Benefits

This section explores some of the major benefits of the core/bindings split.

### "Functional core/imperative shell"

The core/bindings split naturally implements a [functional core/imperative
shell](https://www.destroyallsoftware.com/screencasts/catalog/functional-core-imperative-shell)
design pattern. In this pattern, the bulk of the application logic lives in a
"functional core" which concerns itself with the high-level tasks of the
application. Ideally, the functional core can be unaware of "real world" details
like execution order, communication with the outside world, input validation,
etc. The "imperative shell", on the other hand, concerns itself with all of the
real world details that the functional core ignores.

*NOTE: In the original "functional core/imperative shell" concept, the
functional core is functional in the sense of not having any mutable state. Our
core is not functional in this sense, but our architecture still shares many
properties with that design, so the comparison still makes sense.*

This architecture provides us with a number of benefits:
- High-level testing of the core is easy. Since, to the core, the outside world
  simply looks like a trait implementation, faking out the entire world is
  simply a matter of implementing that trait (in particular, the
  `EventDispatcher` trait). The core's `testutil::FakeEventDispatcher` serves
  this purpose, and allows many of our tests to create an entire faked
  execution environment, execute a sequence of actions, and test to see that the
  right events were emitted in response, and in only a few lines of code.
- The core can be largely infallible. It can provide types and methods whose
  signatures force the bindings to perform any input validation ahead of time
  before calling into the core. This both simplifies the core, and also forces
  the best practice of validating external input as early as possible.
- While the core must, at a minimum, enable whatever execution model the
  bindings want to use (e.g., if the core's data structures are not thread-safe,
  then the bindings cannot execute across multiple threads), it can simply
  expose functions and leave it up to the bindings to call those functions at
  appropriate times.

### Ease of development

Since the core is platform-agnostic, it doesn't require Fuchsia's full build
system to develop on. In fact, the author of this document usually develops by
using the normal `cargo` tool rather than the Fuchsia-specific `fx` or `fargo`
tools. This unlocks both fast development cycles and the full suite of `cargo`
tooling.

### Platform-agnosticism

With the core being platform-agnostic, it should be possible to port the
netstack to other platforms with relative ease.
