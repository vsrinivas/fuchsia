# Tracing Mutex

[![Continuous integration](https://github.com/bertptrs/tracing-mutex/actions/workflows/ci.yml/badge.svg)](https://github.com/bertptrs/tracing-mutex/actions/workflows/ci.yml)
[![Crates.io](https://img.shields.io/crates/v/tracing-mutex.svg)](https://crates.io/crates/tracing-mutex)
[![Documentation](https://docs.rs/tracing-mutex/badge.svg)](https://docs.rs/tracing-mutex)

Avoid deadlocks in your mutexes by acquiring them in a consistent order, or else.

## Background

In any code that uses mutexes or locks, you quickly run into the possibility of deadlock. With just
two mutexes `Foo` and `Bar` you can already deadlock, assuming one thread first locks `Foo` then
attempts to get `Bar` and another first gets `Bar` then tries to get `Foo`. Now both threads are
waiting for each other to release the lock they already have.

One simple way to get around this is by ensuring that, when you need both `Foo` and `Bar`, you
should first acquire `Foo` then you can never deadlock. Of course, with just two mutexes, this is
easy to keep track of, but once your code starts to grow you might lose track of all these
dependencies. That's where this crate comes in.

This crate tracks the order in which you acquire locks in your code, tries to build a dependency
tree out of it, and panics if your dependencies would create a cycle. It provides replacements for
existing synchronization primitives with an identical API, and should be a drop-in replacement.

Inspired by [this blogpost][whileydave], which references a similar behaviour implemented by
[Abseil][abseil-mutex] for their mutexes.

[whileydave]: https://whileydave.com/2020/12/19/dynamic-cycle-detection-for-lock-ordering/
[abseil-mutex]: https://abseil.io/docs/cpp/guides/synchronization

## Usage

Add this dependency to your `Cargo.lock` file like any other:

```toml
[dependencies]
tracing-mutex = "0.2"
```

Then use the locks provided by this library instead of the ones you would use otherwise.
Replacements for the synchronization primitives in `std::sync` can be found in the `stdsync` module.
Support for other synchronization primitives is planned.

```rust
use tracing_mutex::stdsync::TracingMutex;

let some_mutex = TracingMutex::new(42);
*some_mutex.lock().unwrap() += 1;
println!("{:?}", some_mutex);
```

The interdependencies between locks are automatically tracked. If any locking operation would
introduce a cyclic dependency between your locks, the operation panics instead. This allows you to
immediately notice the cyclic dependency rather than be eventually surprised by it in production.

Mutex tracing is efficient, but it is not completely overhead-free. If you cannot spare the
performance penalty in your production environment, this library also offers debug-only tracing.
`DebugMutex`, also found in the `stdsync` module, is a type alias that evaluates to `TracingMutex`
when debug assertions are enabled, and to `Mutex` when they are not. Similar helper types are
available for other synchronization primitives.

### Features

- Dependency-tracking wrappers for all locking primitives
- Optional opt-out for release mode code
- Support for primitives from:
  - `std::sync`
  - `parking_lot`
  - Any library that implements the `lock_api` traits

## Future improvements

- Improve performance in lock tracing
- Optional logging to make debugging easier
- Better and configurable error handling when detecting cyclic dependencies
- Support for other locking libraries
- Support for async locking libraries
- Support for `Send` mutex guards

**Note:** `parking_lot` has already began work on its own deadlock detection mechanism, which works
in a different way. Both can be complimentary.

## License

Licensed under either of

- Apache License, Version 2.0, ([LICENSE-APACHE](./LICENSE-APACHE) or
  http://www.apache.org/licenses/LICENSE-2.0)
- MIT license ([LICENSE-MIT](./LICENSE-MIT) or http://opensource.org/licenses/MIT)

at your option.

### Contributing

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in the
work by you, as defined in the Apache-2.0 license, shall be dual licensed as above, without any
additional terms or conditions.
