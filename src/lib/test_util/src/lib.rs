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
pub struct Counter {
    count: Mutex<usize>,
}

impl Counter {
    /// Initializes a new counter to the given value.
    pub fn new(initial: usize) -> Self {
        Counter { count: Mutex::new(initial) }
    }

    /// Increments the counter by one.
    pub fn inc(&self) {
        *self.count.lock().unwrap() += 1;
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

        static NTHREADS: usize = 3;

        for _ in 0..NTHREADS {
            let thread_tx = tx.clone();
            let child = thread::spawn(move || {
                CALL_COUNT.inc();
                thread_tx.send(CALL_COUNT.get()).unwrap();
            });

            children.push(child);
        }

        let mut ordered_ids = BTreeSet::new();
        for _ in 0..NTHREADS {
            ordered_ids.insert(rx.recv().unwrap());
        }

        // Wait for the threads to complete any remaining work
        for child in children {
            child.join().expect("child thread panicked");
        }

        let mut expected_id: usize = 1;
        for id in ordered_ids.iter() {
            assert!(*id == expected_id);
            expected_id += 1;
        }

        assert_eq!(CALL_COUNT.get(), NTHREADS);
    }
}
