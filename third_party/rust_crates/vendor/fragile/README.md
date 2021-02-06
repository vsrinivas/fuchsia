# Fragile

This library provides wrapper types that permit sending non Send types to other
threads and use runtime checks to ensure safety.

It provides two types: `Fragile<T>` and `Sticky<T>` which are similar in nature but
have different behaviors with regards to how destructors are executed.  The former
will panic if the destructor is called in another thread, the latter will temporarily
leak the object until the thread shuts down.

```rust
use std::thread;

// creating and using a fragile object in the same thread works
let val = Fragile::new(true);
assert_eq!(*val.get(), true);
assert!(val.try_get().is_ok());

// once send to another thread it stops working
thread::spawn(move || {
    assert!(val.try_get().is_err());
}).join()
    .unwrap();
```
