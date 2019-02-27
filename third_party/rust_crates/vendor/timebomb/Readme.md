Timebomb
========

[![Build Status](https://travis-ci.org/csherratt/timebomb.svg?branch=master)](https://travis-ci.org/csherratt/timebomb)

This is a simple timeout mechanism for Rust that is intended for use with unit tests.

```rust
extern crate timebomb;
use timebomb::timeout_ms;

#[test]
fn something_bad() {
	// This will timeout in 1 second if the test did not pass
	timeout_ms(|| {
		// oops infinite loop
		loop {}
	}, 1000);
}
```
