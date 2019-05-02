# Docs

This directory contains documentation of various aspects of the netstack. Briefly:
- [`CONTROL_FLOW.md`](./CONTROL_FLOW.md) describes and motivates how our control flow works
- [`CORE_BINDINGS.md`](./CORE_BINDINGS.md) describes and motivates the split of
  our codebase into two separate "core" and "bindings" components
- [`DETAILS.md`](./DETAILS.md) describes various implementation details that are
  confusing enough to be worth calling out explicitly
- [`HACKING.md`](./HACKING.md) contains instructions for hacking on the netstack
- [`IMPROVEMENTS.md`](./IMPROVEMENTS.md) contains ideas for possible future improvements
- [`IP_TYPES.md`](./IP_TYPES.md) describes our use of types and traits to represent IP versions and addresses
- [`PARSING_SERIALIZATION.md`](./PARSING_SERIALIZATION.md) describes how we
  parse and serialize packets, and how we manage packet buffers
- [`PUB_CRATE.md`](./PUB_CRATE.md) describes how we use visibility modifiers.
- [`STATIC_TYPING.md`](./STATIC_TYPING.md) motivates our enthusiastic use of static typing
