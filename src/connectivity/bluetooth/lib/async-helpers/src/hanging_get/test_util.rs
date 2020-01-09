// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, fmt::Debug, rc::Rc};

/// An observer that can be used to observe values from a `HangingGet` or
/// `Window` instance. The observer must not be observed multiple
/// times without the value being taken with `take_value` inbetween each
/// observation. This prevents overwriting observations without checking
/// them and simplifies the logic and API surface of this fake.
#[derive(Clone, Debug)]
pub(crate) struct TestObserver<V: Debug + PartialEq> {
    value: Rc<RefCell<Option<V>>>,
    expected: Option<V>,
}

impl<V: Debug + Clone + Send + PartialEq> TestObserver<V> {
    pub fn new() -> Self {
        Self { value: Rc::new(RefCell::new(None)), expected: None }
    }

    pub fn expect_value(expected: V) -> Self {
        let mut o = TestObserver::new();
        o.expected = Some(expected);
        o
    }

    pub fn expect_no_value() -> Self {
        TestObserver::new()
    }

    pub fn observe(v: &V, o: Self) {
        let mut value = o.value.borrow_mut();
        if value.is_some() {
            panic!("This observer has an observed a value that was not taken");
        }
        match &o.expected {
            Some(expected) => assert_eq!(expected, v),
            None => panic!("This observer expected no observations to occur"),
        }
        *value = Some(v.clone());
    }

    pub fn has_value(&self) -> bool {
        self.value.borrow().is_some()
    }
}

impl<V: Debug + PartialEq> Drop for TestObserver<V> {
    fn drop(&mut self) {
        if !std::thread::panicking() {
            assert_eq!(
                *self.value.borrow(),
                self.expected,
                "Observer dropped without expected state being met"
            );
        }
    }
}

#[test]
fn test_observer_take_value() {
    TestObserver::observe(&1, TestObserver::expect_value(1));
}

#[test]
#[should_panic]
fn test_observer_take_cannot_double_observe() {
    let observer: TestObserver<i32> = TestObserver::new();
    TestObserver::observe(&1, observer.clone());
    TestObserver::observe(&2, observer.clone());
}
