# fit-promise library

This was formerly part of the `lib/fit` library.
Further migrations and renamings are expected.

## fpromise::promise, fpromise::future, fpromise::executor, etc.

- When writing asynchronous event-driven programs, it's convenient to be able
  to stage a sequence of asynchronous tasks.  This tends to be challenging
  to implement in a callback-driven manner due to object lifetime issues,
  so we would like an alternative pattern that is easier to apply correctly,
  such as by expressing asynchronous logic as a compositions of futures.
- The C++ 14 standard library offers `std::future` but it is tied to a
  thread-based execution model.  Awaiting a future requires blocking, which
  is bad for event loops.
- So libfit offers a family of APIs that work better with event loops.
