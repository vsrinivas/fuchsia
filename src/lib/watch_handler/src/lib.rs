// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! A common handler for hanging_gets

use anyhow::{format_err, Error};

/// Function used to determine whether a change should cause any parked watchers to return.
type ChangeFunction<T> = Box<dyn Fn(&T, &T) -> bool + Send + Sync + 'static>;

/// Trait that should be implemented to send data to the hanging get watcher.
pub trait Sender<T> {
    /// Should send a response immediately to the sender.
    fn send_response(self, data: T);
}

/// Default change function that checks for equality
pub fn equality_change_function<T: PartialEq>() -> ChangeFunction<T> {
    Box::new(|old: &T, new: &T| old != new)
}

/// Handler for hanging gets.
/// The common way to use this is to implement the Sender<T> trait for the FIDL responder that the handler is used for.
/// There should be one instance of HangingGetHandler per interface per connection, as the handler needs to keep track
/// of state per connection.
pub struct WatchHandler<T, ST> {
    /// The callback to be parked in the handler.
    /// There can only be one.
    watch_responder: Option<ST>,
    /// Represents the current state of the system.
    current_value: T,
    /// The last value that was sent to the client.
    last_sent_value: Option<T>,
    /// Function called on change. If function returns true, tells the handler that it should send to the hanging get.
    change_function: ChangeFunction<T>,
}

impl<T, ST> WatchHandler<T, ST>
where
    T: Clone + PartialEq + 'static,
    ST: Sender<T> + 'static,
{
    /// Creates a new instance of WatchHandler which will return immediately on first watch.
    /// Uses default change function which just checks for any change.
    pub fn create(initial_value: T) -> Self {
        Self {
            watch_responder: None,
            last_sent_value: None,
            current_value: initial_value,
            change_function: equality_change_function(),
        }
    }
}

impl<T, ST> WatchHandler<T, ST>
where
    T: Clone + 'static,
    ST: Sender<T> + 'static,
{
    /// Creates a new instance of WatchHandler which will return immediately on first watch.
    pub fn create_with_change_fn(change_function: ChangeFunction<T>, initial_value: T) -> Self {
        Self {
            watch_responder: None,
            last_sent_value: None,
            current_value: initial_value,
            change_function: change_function,
        }
    }

    /// Park a new hanging get in the handler
    pub fn watch(&mut self, responder: ST) -> Result<(), Error> {
        if let None = self.watch_responder {
            self.watch_responder = Some(responder);
            self.send_if_needed();
            Ok(())
        } else {
            Err(format_err!("Inconsistent state; existing handler in state"))
        }
    }

    /// Sets a new change function.
    /// The hanging get will only return when the change function evaluates to true when comparing the last value sent to
    /// the client and the current value. Takes effect immediately; if change function evaluates to true then the pending
    /// responder will be called.
    pub fn set_change_function(&mut self, change_function: ChangeFunction<T>) {
        self.change_function = change_function;
        self.send_if_needed();
    }

    /// Called to update the current value of the handler, sending changes using the watcher if needed.
    pub fn set_value(&mut self, new_value: T) {
        self.current_value = new_value;
        self.send_if_needed();
    }

    /// Called when receiving a notification that value has changed.
    fn send_if_needed(&mut self) {
        let should_send = match self.last_sent_value.as_ref() {
            Some(last_value) => (self.change_function)(&last_value, &self.current_value),
            None => true,
        };

        if should_send {
            if let Some(responder) = self.watch_responder.take() {
                responder.send_response(self.current_value.clone());
                self.last_sent_value = Some(self.current_value.clone());
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    const ID_INVALID: i32 = 0;
    const ID1: i32 = 1;
    const ID2: i32 = 2;
    const ID3: i32 = 3;
    const ID4: i32 = 4;

    #[derive(Clone, PartialEq)]
    struct TestStruct {
        id: i32,
    }

    struct TestSender {
        sent_value: Rc<RefCell<i32>>,
    }

    impl Sender<TestStruct> for TestSender {
        fn send_response(self, data: TestStruct) {
            self.sent_value.replace(data.id);
        }
    }

    #[test]
    fn test_watch() {
        let mut handler = WatchHandler::<TestStruct, TestSender>::create(TestStruct { id: ID1 });

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // First call should return immediately
        assert_eq!((*sent_value.borrow()), ID1);

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // Second call doesn't return because value hasn't changed
        assert_eq!((*sent_value.borrow()), ID_INVALID);

        handler.set_value(TestStruct { id: ID2 });

        // When value changes, returns immediately
        assert_eq!((*sent_value.borrow()), ID2);

        handler.set_value(TestStruct { id: ID3 });

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // Call after change also returns immediately
        assert_eq!((*sent_value.borrow()), ID3);
    }

    #[test]
    fn test_watch_fails() {
        let mut handler = WatchHandler::<TestStruct, TestSender>::create(TestStruct { id: ID1 });

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        // first watch returns immediately
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        // second watch hangs
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        // third results in an error
        let result = handler.watch(TestSender { sent_value: sent_value.clone() });

        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_watch_with_change_function() {
        let mut handler = WatchHandler::<TestStruct, TestSender>::create_with_change_fn(
            equality_change_function(),
            TestStruct { id: ID1 },
        );
        let sent_value = Rc::new(RefCell::new(ID_INVALID));

        handler.set_change_function(Box::new(|_old, _new| false));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // first watch should return immediately regardless
        assert_eq!((*sent_value.borrow()), ID1);

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.set_change_function(Box::new(|old, new| new.id - old.id > 1));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();
        handler.set_value(TestStruct { id: ID2 });

        // If change function returns false, will not return
        assert_eq!((*sent_value.borrow()), ID_INVALID);

        handler.set_value(TestStruct { id: ID3 });

        // But subsequent change that satsifies change function will cause return
        assert_eq!((*sent_value.borrow()), ID3);

        // Setting the change function with a pending watch should cause sender to send.
        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.set_change_function(Box::new(|_old, _new| false));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();
        handler.set_value(TestStruct { id: ID4 });

        assert_eq!((*sent_value.borrow()), ID_INVALID);

        handler.set_change_function(Box::new(|_old, _new| true));
        assert_eq!((*sent_value.borrow()), ID4);
    }
}
