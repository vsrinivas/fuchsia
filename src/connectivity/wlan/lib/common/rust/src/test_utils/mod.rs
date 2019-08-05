// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::appendable::{Appendable, BufferTooSmall};

pub mod fake_frames;
pub mod fake_stas;

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
            _ => panic!($fmt, $($args)*),
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
        assert_variant!($test, $variant $( | $others)* => {}, $fmt $(, $args)*);
    };
    // Custom expression and custom error message.
    ($test:expr, $variant:pat $( | $others:pat)* => $e:expr, $fmt:expr $(, $args:tt)*) => {
        assert_variant!($test, $variant $( | $others)* => { $e }, $fmt $(, $args)*);
    };
    // Default error message.
    ($test:expr, $variant:pat $( | $others:pat)*) => {
        assert_variant!($test, $variant $( | $others)* => {});
    };
    // Optional trailing comma after expression.
    ($test:expr, $variant:pat $( | $others:pat)* => $e:expr,) => {
        assert_variant!($test, $variant $( | $others)* => { $e });
    };
    // Optional trailing comma.
    ($test:expr, $variant:pat $( | $others:pat)* => $b:block,) => {
        assert_variant!($test, $variant $( | $others)* => $b);
    };
    ($test:expr, $variant:pat $( | $others:pat)* => $b:expr,) => {
        assert_variant!($test, $variant $( | $others)* => { $b });
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
    fn assert_variant_full_match() {
        // Success:
        assert_variant!(Foo::A(8), Foo::A(8));

        // Check same variant with different value:
        let result = panic::catch_unwind(|| {
            assert_variant!(Foo::A(8), Foo::A(7));
        });
        assert!(result.is_err());

        // Check different variant:
        let result = panic::catch_unwind(|| {
            assert_variant!(Foo::A(8), Foo::C);
        });
        assert!(result.is_err());
    }

    #[test]
    fn assert_variant_multi_variant() {
        // Success:
        assert_variant!(Foo::A(8), Foo::A(8) | Foo::C);
        assert_variant!(Foo::C, Foo::A(8) | Foo::C);

        // Failure:
        let result = panic::catch_unwind(|| {
            assert_variant!(Foo::C, Foo::A(_) | Foo::B { .. });
        });
        assert!(result.is_err());
    }

    #[test]
    fn assert_variant_partial_match() {
        // Success:
        assert_variant!(Foo::A(8), Foo::A(_));
        assert_variant!(Foo::B { named: 7, bar: 8 }, Foo::B { .. });
        assert_variant!(Foo::B { named: 7, bar: 8 }, Foo::B { named: 7, .. });

        // Failure:
        let result = panic::catch_unwind(|| {
            assert_variant!(Foo::A(8), Foo::B { .. });
        });
        assert!(result.is_err());
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
    fn assert_variant_custom_message() {
        // Success:
        assert_variant!(Foo::A(8), Foo::A(_), "custom error message");

        // Failure:
        let result = panic::catch_unwind(|| {
            assert_variant!(Foo::A(8), Foo::B { .. }, "custom error message");
        });
        let error = result.unwrap_err();
        assert_variant!(error.downcast_ref::<&'static str>(), Some(message) => {
            assert_eq!(message, &"custom error message")
        });
    }
}
