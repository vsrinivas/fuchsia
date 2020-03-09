// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate defines a collection of useful utilities for testing rust code.

use std::sync::Mutex;

/// Asserts that an expression matches some expected pattern. The pattern may
/// be any valid rust pattern, which includes multiple patterns (`Foo | Bar`)
/// and an arm guard (`Foo::Bar(ref val) if val == "foo"`).
///
/// On panic, this macro will print the value of the input expression
/// using its debug representation, along with the expected pattern.
///
/// # Examples
///
/// ```
/// use test_util::assert_matches;
///
/// ##[derive(Debug)]
/// enum Foo {
///     Bar(String),
///     Baz,
/// }
///
/// # fn main() {
///     let a = Foo::Baz;
///     assert_matches!(a, Foo::Baz);
///
///     let b = Foo::Bar("foo".to_owned());
///     assert_matches!(b, Foo::Bar(ref val) if val == "foo");
/// # }
#[macro_export]
macro_rules! assert_matches {
    ($input:expr, $($pat:pat)|+) => {
        match $input {
            $($pat)|* => (),
            _ => panic!(
                r#"assertion failed: `(actual matches expected)`
   actual: `{:?}`,
 expected: `{}`"#,
                &$input,
                stringify!($($pat)|*)
            ),
        };
    };

    ($input:expr, $($pat:pat)|+ if $arm_guard:expr) => {
        match $input {
            $($pat)|* if $arm_guard => (),
            _ => panic!(
                r#"assertion failed: `(actual matches expected)`
   actual: `{:?}`,
 expected: `{} if {}`"#,
                &$input,
                stringify!($($pat)|*),
                stringify!($arm_guard)
            ),
        };
    };
}

/// Asserts that the first argument is strictly less than the second.
#[macro_export]
macro_rules! assert_lt {
    ($x:expr, $y:expr) => {
        assert!(
            $x < $y,
            "assertion `{} < {}` failed; actual: {} is not less than {}",
            stringify!($x),
            stringify!($y),
            $x,
            $y
        );
    };
}

/// Asserts that the first argument is less than or equal to the second.
#[macro_export]
macro_rules! assert_leq {
    ($x:expr, $y:expr) => {
        assert!(
            $x <= $y,
            "assertion `{} <= {}` failed; actual: {} is not less than or equal to {}",
            stringify!($x),
            stringify!($y),
            $x,
            $y
        );
    };
}

/// Asserts that the first argument is strictly greater than the second.
#[macro_export]
macro_rules! assert_gt {
    ($x:expr, $y:expr) => {
        assert!(
            $x > $y,
            "assertion `{} > {}` failed; actual: {} is not greater than {}",
            stringify!($x),
            stringify!($y),
            $x,
            $y
        );
    };
}

/// Asserts that the first argument is greater than or equal to the second.
#[macro_export]
macro_rules! assert_geq {
    ($x:expr, $y:expr) => {
        assert!(
            $x >= $y,
            "assertion `{} >= {}` failed; actual: {} is not greater than or equal to {}",
            stringify!($x),
            stringify!($y),
            $x,
            $y
        );
    };
}

/// Asserts that `x` and `y` are within `delta` of one another.
///
/// `x` and `y` must be of a common type that supports both subtraction and negation. (Note that it
/// would be natural to define this macro using `abs()`, but when attempting to do so, the compiler
/// fails to apply its inference rule for under-constrained types. See
/// [https://github.com/rust-lang/reference/issues/104].)
#[macro_export]
macro_rules! assert_near {
    ($x: expr, $y: expr, $delta: expr) => {
        let difference = $x - $y;
        assert!(
            -$delta <= difference && difference <= $delta,
            "assertion `{} is near {} (within delta {})` failed; actual: |{} - {}| > {}",
            stringify!($x),
            stringify!($y),
            stringify!($delta),
            $x,
            $y,
            $delta
        );
    };
}

/// A mutually exclusive counter that is not shareable, but can be defined statically for the
/// duration of a test. This simplifies the implementation of a simple test-global counter,
/// avoiding the complexity of alternatives like std::sync::atomic objects that are typically
/// wrapped in Arc()s, cloned, and require non-intuitive memory management options.
///
/// # Example
///
/// ```
///    use test_util::Counter;
///    use lazy_static::lazy_static;
///
///    #[test]
///    async fn my_test() {
///        lazy_static! {
///            static ref CALL_COUNT: Counter = Counter::new(0);
///        }
///
///        let handler = || {
///            // some async callback
///            // ...
///            CALL_COUNT.inc();
///        };
///        handler();
///        // ...
///        CALL_COUNT.inc();
///
///        assert_eq!(CALL_COUNT.get(), 2);
///    }
/// ```
///
/// *Important:* Since inc() and get() obtain separate Mutex lock()s to access the underlying
/// counter value, it is very possible that a separate thread, if also mutating the same counter,
/// may increment the value between the first thread's calls to inc() and get(), in which case,
/// the two threads could get() the same value (the result after both calls to inc()). If get()
/// is used to, for example, print the values after each call to inc(), the resulting values might
/// include duplicate intermediate counter values, with some numbers skipped, but the final value
/// after all threads complete will be the exact number of all calls to inc() (offset by the
/// initial value).
///
/// To provide slightly more consistent results, inc() returns the new value after incrementing
/// the counter, obtaining the value while the lock is held. This way, each incremental value will
/// be returned to the calling threads; *however* the threads could still receive the values out of
/// order.
///
/// Consider, thread 1 calls inc() starting at count 0. A value of 1 is returned, but before thread
/// 1 receives the new value, it might be interrupted by thread 2. Thread 2 increments the counter
/// from 1 to 2, return the new value 2, and (let's say, for example) prints the value "2". Thread 1
/// then resumes, and prints "1".
///
/// Specifically, the Counter guarantees that each invocation of inc() will return a value that is
/// 1 greater than the previous value returned by inc() (or 1 greater than the `initial` value, if
/// it is the first invocation). Call get() after completing all invocations of inc() to get the
/// total number of times inc() was called (offset by the initial value).
pub struct Counter {
    count: Mutex<usize>,
}

impl Counter {
    /// Initializes a new counter to the given value.
    pub fn new(initial: usize) -> Self {
        Counter { count: Mutex::new(initial) }
    }

    /// Increments the counter by one and returns the new value.
    pub fn inc(&self) -> usize {
        let mut count = self.count.lock().unwrap();
        *count += 1;
        *count
    }

    /// Returns the current value of the counter.
    pub fn get(&self) -> usize {
        *self.count.lock().unwrap()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        lazy_static::lazy_static,
        std::collections::BTreeSet,
        std::sync::mpsc,
        std::sync::mpsc::{Receiver, Sender},
        std::thread,
    };

    #[derive(Debug)]
    enum Foo {
        Bar(String),
        Baz,
        Bat,
    }

    #[test]
    fn test_simple_match_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_));
    }

    #[test]
    #[should_panic]
    fn test_simple_match_fails() {
        let input = Foo::Baz;
        assert_matches!(input, Foo::Bar(_));
    }

    #[test]
    fn test_match_with_arm_guard_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(ref val) if val == "foo");
    }

    #[test]
    #[should_panic]
    fn test_match_with_arm_guard_fails() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(ref val) if val == "bar");
    }

    #[test]
    fn test_match_with_multiple_patterns_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_) | Foo::Baz);
    }

    #[test]
    #[should_panic]
    fn test_match_with_multiple_patterns_fails() {
        let input = Foo::Bat;
        assert_matches!(input, Foo::Bar(_) | Foo::Baz);
    }

    #[test]
    fn test_match_with_multiple_patterns_and_arm_guard_passes() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_) | Foo::Baz if true);
    }

    #[test]
    #[should_panic]
    fn test_match_with_multiple_patterns_and_arm_guard_fails() {
        let input = Foo::Bat;
        assert_matches!(input, Foo::Bar(_) | Foo::Baz if false);
    }

    #[test]
    fn test_assertion_does_not_consume_input() {
        let input = Foo::Bar("foo".to_owned());
        assert_matches!(input, Foo::Bar(_));
        if let Foo::Bar(ref val) = input {
            assert_eq!(val, "foo");
        }
    }

    #[test]
    fn test_assert_lt_passes() {
        assert_lt!(1, 2);
        assert_lt!(1u8, 2u8);
        assert_lt!(1u16, 2u16);
        assert_lt!(1u32, 2u32);
        assert_lt!(1u64, 2u64);
        assert_lt!(-1, 3);
        assert_lt!(-1i8, 3i8);
        assert_lt!(-1i16, 3i16);
        assert_lt!(-1i32, 3i32);
        assert_lt!(-1i64, 3i64);
        assert_lt!(-2.0, 7.0);
        assert_lt!(-2.0f32, 7.0f32);
        assert_lt!(-2.0f64, 7.0f64);
        assert_lt!('a', 'b');
    }

    #[test]
    #[should_panic(expected = "assertion `a < b` failed; actual: 2 is not less than 2")]
    fn test_assert_lt_fails_a_equals_b() {
        let a = 2;
        let b = 2;
        assert_lt!(a, b);
    }

    #[test]
    #[should_panic(expected = "assertion `a < b` failed; actual: 5 is not less than 2")]
    fn test_assert_lt_fails_a_greater_than_b() {
        let a = 5;
        let b = 2;
        assert_lt!(a, b);
    }

    #[test]
    fn test_assert_leq_passes() {
        assert_leq!(1, 2);
        assert_leq!(2, 2);
        assert_leq!(-2.0, 3.0);
        assert_leq!(3.0, 3.0);
        assert_leq!('a', 'b');
        assert_leq!('b', 'b');
    }

    #[test]
    #[should_panic(
        expected = "assertion `a <= b` failed; actual: 3 is not less than or equal to 2"
    )]
    fn test_assert_leq_fails() {
        let a = 3;
        let b = 2;
        assert_leq!(a, b);
    }

    #[test]
    fn test_assert_gt_passes() {
        assert_gt!(2, 1);
        assert_gt!(2u8, 1u8);
        assert_gt!(2u16, 1u16);
        assert_gt!(2u32, 1u32);
        assert_gt!(2u64, 1u64);
        assert_gt!(3, -1);
        assert_gt!(3i8, -1i8);
        assert_gt!(3i16, -1i16);
        assert_gt!(3i32, -1i32);
        assert_gt!(3i64, -1i64);
        assert_gt!(7.0, -2.0);
        assert_gt!(7.0f32, -2.0f32);
        assert_gt!(7.0f64, -2.0f64);
        assert_gt!('b', 'a');
    }

    #[test]
    #[should_panic(expected = "assertion `a > b` failed; actual: 2 is not greater than 2")]
    fn test_assert_gt_fails_a_equals_b() {
        let a = 2;
        let b = 2;
        assert_gt!(a, b);
    }

    #[test]
    #[should_panic(expected = "assertion `a > b` failed; actual: -1 is not greater than 2")]
    fn test_assert_gt_fails_a_less_than_b() {
        let a = -1;
        let b = 2;
        assert_gt!(a, b);
    }

    #[test]
    fn test_assert_geq_passes() {
        assert_geq!(2, 1);
        assert_geq!(2, 2);
        assert_geq!(3.0, -2.0);
        assert_geq!(3.0, 3.0);
        assert_geq!('b', 'a');
        assert_geq!('b', 'b');
    }

    #[test]
    #[should_panic(
        expected = "assertion `a >= b` failed; actual: 2 is not greater than or equal to 3"
    )]
    fn test_assert_geq_fails() {
        let a = 2;
        let b = 3;
        assert_geq!(a, b);
    }

    #[test]
    fn test_assert_near_passes() {
        // Test both possible orderings and equality with literals.
        assert_near!(1.0001, 1.0, 0.01);
        assert_near!(1.0, 1.0001, 0.01);
        assert_near!(1.0, 1.0, 0.0);

        // Ensure the macro operates on all other expected input types.
        assert_near!(1.0001f32, 1.0f32, 0.01f32);
        assert_near!(1.0001f64, 1.0f64, 0.01f64);
        assert_near!(7, 5, 2);
        assert_near!(7i8, 5i8, 2i8);
        assert_near!(7i16, 5i16, 2i16);
        assert_near!(7i32, 5i32, 2i32);
        assert_near!(7i64, 5i64, 2i64);
    }

    #[test]
    #[should_panic]
    fn test_assert_near_fails() {
        assert_near!(1.00001, 1.0, 1e-8);
    }

    // Test error message with integers so display is predictable.
    #[test]
    #[should_panic(
        expected = "assertion `a is near b (within delta d)` failed; actual: |3 - 5| > 1"
    )]
    fn test_assert_near_fails_with_message() {
        let a = 3;
        let b = 5;
        let d = 1;
        assert_near!(a, b, d);
    }

    #[test]
    fn test_inc() {
        lazy_static! {
            static ref CALL_COUNT: Counter = Counter::new(0);
        }

        CALL_COUNT.inc();

        assert_eq!(CALL_COUNT.get(), 1);
    }

    #[test]
    fn test_incs_from_10() {
        lazy_static! {
            static ref CALL_COUNT: Counter = Counter::new(10);
        }

        CALL_COUNT.inc();
        CALL_COUNT.inc();
        CALL_COUNT.inc();

        assert_eq!(CALL_COUNT.get(), 13);
    }

    #[test]
    fn async_counts() {
        lazy_static! {
            static ref CALL_COUNT: Counter = Counter::new(0);
        }

        let (tx, rx): (Sender<usize>, Receiver<usize>) = mpsc::channel();
        let mut children = Vec::new();

        static NTHREADS: usize = 10;

        for _ in 0..NTHREADS {
            let thread_tx = tx.clone();
            let child = thread::spawn(move || {
                let new_value = CALL_COUNT.inc();
                thread_tx.send(new_value).unwrap();
                println!("Sent: {} (OK if out of order)", new_value);
            });

            children.push(child);
        }

        let mut ordered_ids: BTreeSet<usize> = BTreeSet::new();
        for _ in 0..NTHREADS {
            let received_id = rx.recv().unwrap();
            println!("Received: {} (OK if in yet a different order)", received_id);
            ordered_ids.insert(received_id);
        }

        // Wait for the threads to complete any remaining work
        for child in children {
            child.join().expect("child thread panicked");
        }

        // All threads should have incremented the count by 1 each.
        assert_eq!(CALL_COUNT.get(), NTHREADS);

        // All contiguous incremental values should have been received, though possibly not in
        // order. The BTreeSet will return them in order so the complete set can be verified.
        let mut expected_id: usize = 1;
        for id in ordered_ids.iter() {
            assert_eq!(*id, expected_id);
            expected_id += 1;
        }
    }
}
