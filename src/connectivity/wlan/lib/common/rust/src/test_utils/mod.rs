// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::appendable::{Appendable, BufferTooSmall},
    fuchsia_async::{DurationExt, OnTimeout, TimeoutExt},
    fuchsia_zircon as zx,
    futures::Future,
};

pub mod fake_frames;
pub mod fake_stas;

/// A trait which allows to expect a future to terminate within a given time or panic otherwise.
pub trait ExpectWithin: Future + Sized {
    fn expect_within<S: ToString + Clone>(
        self,
        duration: zx::Duration,
        msg: S,
    ) -> OnTimeout<Self, Box<dyn FnOnce() -> Self::Output>> {
        let msg = msg.clone().to_string();
        self.on_timeout(duration.after_now(), Box::new(move || panic!("{}", msg)))
    }
}

impl<F: Future + Sized> ExpectWithin for F {}

pub struct FixedSizedTestBuffer(Vec<u8>);
impl FixedSizedTestBuffer {
    pub fn new(capacity: usize) -> Self {
        Self(Vec::with_capacity(capacity))
    }
}
impl Appendable for FixedSizedTestBuffer {
    fn append_bytes(&mut self, bytes: &[u8]) -> Result<(), BufferTooSmall> {
        if !self.can_append(bytes.len()) {
            return Err(BufferTooSmall);
        }
        self.0.append_bytes(bytes)
    }

    fn append_bytes_zeroed(&mut self, len: usize) -> Result<&mut [u8], BufferTooSmall> {
        if !self.can_append(len) {
            return Err(BufferTooSmall);
        }
        self.0.append_bytes_zeroed(len)
    }

    fn bytes_written(&self) -> usize {
        self.0.bytes_written()
    }

    fn can_append(&self, bytes: usize) -> bool {
        self.0.len() + bytes <= self.0.capacity()
    }
}

/// Macro to assert a value matches a variant.
/// This macro is particularly useful when partially matching a variant.
///
/// Example:
/// ```
/// // Basic matching:
/// let foo = Foo::B(42);
/// assert_variant!(foo, Foo::B(_)); // Success
/// assert_variant!(foo, Foo::A); // Panics: "unexpected variant: B(42)"
///
/// // Multiple variants matching:
/// let foo = Foo::B(42);
/// assert_variant!(foo, Foo::A | Foo::B(_)); // Success
/// assert_variant!(foo, Foo::A | Foo::C); // Panics: "unexpected variant: B(42)"
///
/// // Advanced matching:
/// let foo: Result<Option<u8>, u8> = Result::Ok(Some(5));
/// assert_variant!(foo, Result::Ok(Some(1...5))); // Success
/// assert_variant!(foo, Result::Ok(Some(1...4))); // Panics: "unexpected variant: Ok(Some(5))"
///
/// // Custom message
/// let foo = Foo::B(42);
/// // Panics: "expected Foo::A, actual: B(42)"
/// assert_variant!(foo, Foo::A, "expected Foo::A, actual: {:?}", foo);
///
/// // Optional expression:
/// let foo = Foo::B(...);
/// assert_variant!(foo, Foo::B(v) => {
///     assert_eq!(v.id, 5);
///     ...
/// });
///
/// // Unwrapping partially matched variant:
/// let foo = Foo::B(...);
/// let bar = assert_variant!(foo, Foo::B(bar) => bar);
/// let xyz = bar.foo_bar(...);
/// assert_eq!(xyz, ...);
/// ```
#[macro_export]
macro_rules! assert_variant {
    // Use custom formatting when panicing.
    ($test:expr, $variant:pat $( | $others:pat)* => $e:expr, $fmt:expr $(, $args:tt)*) => {
        match $test {
            $variant $(| $others)* => $e,
            _ => panic!($fmt, $($args,)*),
        }
    };
    // Use default message when panicing.
    ($test:expr, $variant:pat $( | $others:pat)* => $e:expr) => {
        match $test {
            $variant $(| $others)* => $e,
            other => panic!("unexpected variant: {:?}", other),
        }
    };
    // Custom error message.
    ($test:expr, $variant:pat $( | $others:pat)* , $fmt:expr $(, $args:tt)*) => {
        $crate::assert_variant!($test, $variant $( | $others)* => {}, $fmt $(, $args)*);
    };
    // Custom expression and custom error message.
    ($test:expr, $variant:pat $( | $others:pat)* => $e:expr, $fmt:expr $(, $args:tt)*) => {
        $crate::assert_variant!($test, $variant $( | $others)* => { $e }, $fmt $(, $args)*);
    };
    // Default error message.
    ($test:expr, $variant:pat $( | $others:pat)*) => {
        $crate::assert_variant!($test, $variant $( | $others)* => {});
    };
    // Optional trailing comma after expression.
    ($test:expr, $variant:pat $( | $others:pat)* => $e:expr,) => {
        $crate::assert_variant!($test, $variant $( | $others)* => { $e });
    };
    // Optional trailing comma.
    ($test:expr, $variant:pat $( | $others:pat)* => $b:block,) => {
        $crate::assert_variant!($test, $variant $( | $others)* => $b);
    };
    ($test:expr, $variant:pat $( | $others:pat)* => $b:expr,) => {
        $crate::assert_variant!($test, $variant $( | $others)* => { $b });
    };
}

/// Asserts the value at a particular index of an expression
/// evaluating to a type implementing the Index trait. This macro
/// is effectively a thin wrapper around `assert_variant` that will
/// pretty-print the entire indexable value if the assertion fails.
///
/// This macro is particularly useful when failure to assert a single
/// value in a Vec requires more context to debug the failure.
///
/// # Examples
///
/// ```
/// let v = vec![0, 2];
/// // Success
/// assert_variant_at_idx!(v, 0, 0);
/// // Panics: "unexpected variant at 0 in v:
/// // [
/// //   0,
/// //   2,
/// // ]"
/// assert_variant_at_idx!(v, 0, 2);
/// ```
#[macro_export]
macro_rules! assert_variant_at_idx {
    ($indexable:expr, $idx:expr, $variant:pat $( | $others:pat)*) => {
        let indexable_name = stringify!($indexable);
        $crate::assert_variant!(&$indexable[$idx], $variant $(| $others)*,
                        "unexpected variant at {:?} in {}:\n{:#?}",
                        $idx, indexable_name, $indexable);
    };
}

#[cfg(test)]
mod tests {
    use std::panic;

    #[derive(Debug)]
    enum Foo {
        A(u8),
        B { named: u8, bar: u16 },
        C,
    }

    #[test]
    fn assert_variant_full_match_success() {
        assert_variant!(Foo::A(8), Foo::A(8));
    }

    #[test]
    #[should_panic(expected = "unexpected variant")]
    fn assert_variant_full_match_fail_with_same_variant_different_value() {
        assert_variant!(Foo::A(8), Foo::A(7));
    }

    #[test]
    #[should_panic(expected = "unexpected variant")]
    fn assert_variant_full_match_fail_with_different_variant() {
        assert_variant!(Foo::A(8), Foo::C);
    }

    #[test]
    fn assert_variant_multi_variant_success() {
        assert_variant!(Foo::A(8), Foo::A(8) | Foo::C);
        assert_variant!(Foo::C, Foo::A(8) | Foo::C);
    }

    #[test]
    #[should_panic(expected = "unexpected variant")]
    fn assert_variant_multi_variant_failure() {
        assert_variant!(Foo::C, Foo::A(_) | Foo::B { .. });
    }

    #[test]
    fn assert_variant_partial_match_success() {
        assert_variant!(Foo::A(8), Foo::A(_));
        assert_variant!(Foo::B { named: 7, bar: 8 }, Foo::B { .. });
        assert_variant!(Foo::B { named: 7, bar: 8 }, Foo::B { named: 7, .. });
    }

    #[test]
    #[should_panic(expected = "unexpected variant")]
    fn assert_variant_partial_match_failure() {
        assert_variant!(Foo::A(8), Foo::B { .. });
    }

    #[test]
    fn assert_variant_block_expr() {
        assert_variant!(Foo::A(8), Foo::A(value) => {
            assert_eq!(value, 8);
        });
        assert_variant!(Foo::B { named: 7, bar: 8 }, Foo::B { named, .. } => {
            assert_eq!(named, 7);
        });

        let named = assert_variant!(Foo::B { named: 7, bar: 8 }, Foo::B { named, .. } => named);
        assert_eq!(named, 7);

        assert_variant!(Foo::B { named: 7, bar: 8 }, Foo::B { .. } => (), "custom error message");
    }

    #[test]
    fn assert_variant_custom_message_success() {
        assert_variant!(Foo::A(8), Foo::A(_), "custom error message");
    }

    #[test]
    #[should_panic(expected = "custom error message")]
    fn assert_variant_custom_message_failure() {
        assert_variant!(Foo::A(8), Foo::B { .. }, "custom error message");
    }

    #[test]
    #[should_panic(expected = "custom error message token1 token2")]
    fn assert_variant_custom_message_with_multiple_fmt_tokens_failure() {
        assert_variant!(Foo::A(8), Foo::B { .. }, "custom error message {} {}", "token1", "token2");
    }

    #[test]
    fn assert_variant_at_idx_success() {
        let v = vec![0, 2];
        assert_variant_at_idx!(v, 0, 0);
    }

    #[test]
    #[should_panic(expected = "unexpected variant at 0 in v:\n[\n    0,\n    2,\n]")]
    fn assert_variant_at_idx_failure() {
        let v = vec![0, 2];
        assert_variant_at_idx!(v, 0, 2);
    }
}
