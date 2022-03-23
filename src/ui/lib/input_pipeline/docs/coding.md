# input_pipeline > Coding

Reviewed on: 2022-03-22

## Shared-mutable data structures

Rust provides multiple options for how to manage ownership of shared data structures
(e.g. `Rc`, `Arc`), and multiple options for how to ensure memory safety when mutating
shared data structures (e.g. `RefCell`, `std::sync::Mutex`).

This section describes the recommended options for code within this library.
If these options seem unsuitable for your use case, please ask a reviewer _before_
writing code that uses some other option.

1. `InputHandler`s _should_ be wrapped in `Rc` instead of `Arc`. This is primarily
   for semantic consistency with the fact that `InputHandler`s are not guaranteed
   to be `Send`, but may have some performance benefits as well.

1. Implementations of `InputHandler` _should_ wrap mutable state in a `RefCell`,
   rather than a `std::sync::Mutex`, `futures::lock::Mutex`, or `parking_lot::Mutex`.
   * The primary benefit of `RefCell` over the mutexes is that, if a program
     tries to concurrently access the data in incompatible ways
     (`borrow()` + `borrow_mut()`), the program will panic instead of deadlocking.
   * A secondary benefit is that there's no confusion over which wrapper is
     being used. (`Mutex` is very confusing given that there are three commonly
     used implementations.)
   * A teritary benefit, largely speculative, is that performance may be better.
     As with `Rc` vs. `Arc`, `RefCell` doesn't require cross-core synchronization.
