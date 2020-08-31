// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! A common handler for hanging_gets

use thiserror::Error;

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
    current_value: Option<T>,
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
    /// Creates a new instance of WatchHandler. If |initial_value| is provided, it will return
    /// immediately on first watch. Otherwise, the first watch returns when the value is set for
    /// the first time.
    /// Uses default change function which just checks for any change.
    pub fn create(initial_value: Option<T>) -> Self {
        Self::create_with_change_fn(equality_change_function(), initial_value)
    }
}

impl<T, ST> WatchHandler<T, ST>
where
    T: Clone + 'static,
    ST: Sender<T> + 'static,
{
    /// Creates a new instance of WatchHandler. If |initial_value| is provided, it will return
    /// immediately on first watch. Otherwise, the first watch returns when the value is set for
    /// the first time.
    pub fn create_with_change_fn(
        change_function: ChangeFunction<T>,
        initial_value: Option<T>,
    ) -> Self {
        Self {
            watch_responder: None,
            last_sent_value: None,
            current_value: initial_value,
            change_function: change_function,
        }
    }

    /// Park a new hanging get in the handler. If a hanging get is already parked, returns the
    /// new responder.
    pub fn watch(&mut self, responder: ST) -> Result<(), WatchError<ST>> {
        if let None = self.watch_responder {
            self.watch_responder = Some(responder);
            self.send_if_needed();
            Ok(())
        } else {
            Err(WatchError { responder })
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
        self.current_value = Some(new_value);
        self.send_if_needed();
    }

    /// Called when receiving a notification that value has changed.
    fn send_if_needed(&mut self) {
        let value_to_send = match (self.last_sent_value.as_ref(), self.current_value.as_ref()) {
            (Some(last), Some(current)) if (self.change_function)(last, current) => Some(current),
            (Some(_), Some(_)) => None,
            (None, Some(current)) => Some(current),
            (_, None) => None,
        };

        if let Some(value) = value_to_send {
            if let Some(responder) = self.watch_responder.take() {
                responder.send_response(value.clone());
                self.last_sent_value = Some(value.clone());
            }
        }
    }
}

/// Error returned if watch fails.
#[derive(Error)]
#[error("Inconsistent state; existing handler in state")]
pub struct WatchError<ST> {
    /// The responder that could not be parked.
    pub responder: ST,
}

impl<ST> std::fmt::Debug for WatchError<ST> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        write!(f, "{}", self)
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

    #[derive(Debug, PartialEq)]
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
        let mut handler =
            WatchHandler::<TestStruct, TestSender>::create(Some(TestStruct { id: ID1 }));

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
    fn test_watch_no_initial() {
        let mut handler = WatchHandler::<TestStruct, TestSender>::create(None);

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // First call does not return until value is set
        assert_eq!((*sent_value.borrow()), ID_INVALID);

        handler.set_value(TestStruct { id: ID1 });
        assert_eq!((*sent_value.borrow()), ID1);

        let mut handler = WatchHandler::<TestStruct, TestSender>::create(None);
        handler.set_value(TestStruct { id: ID2 });

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // First call returns immediately if value is already set.
        assert_eq!((*sent_value.borrow()), ID2);
    }

    #[test]
    fn test_watch_fails() {
        let mut handler =
            WatchHandler::<TestStruct, TestSender>::create(Some(TestStruct { id: ID1 }));

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        // first watch returns immediately
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        // second watch hangs
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        let sent_value = Rc::new(RefCell::new(ID4));
        // third results in an error
        let result = handler.watch(TestSender { sent_value: sent_value.clone() });

        assert_eq!(result.unwrap_err().responder, TestSender { sent_value: sent_value.clone() });
    }

    #[test]
    fn test_watch_with_change_function() {
        let mut handler = WatchHandler::<TestStruct, TestSender>::create_with_change_fn(
            equality_change_function(),
            Some(TestStruct { id: ID1 }),
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

    #[test]
    fn test_watch_with_change_fn_no_initial() {
        let mut handler = WatchHandler::<TestStruct, TestSender>::create_with_change_fn(
            Box::new(|_old, _new| false),
            None,
        );

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // First call does not return until value is set
        assert_eq!((*sent_value.borrow()), ID_INVALID);

        // First value returns after set regardless of change fn
        handler.set_value(TestStruct { id: ID1 });
        assert_eq!((*sent_value.borrow()), ID1);

        let mut handler = WatchHandler::<TestStruct, TestSender>::create_with_change_fn(
            Box::new(|_old, _new| false),
            None,
        );
        handler.set_value(TestStruct { id: ID2 });

        let sent_value = Rc::new(RefCell::new(ID_INVALID));
        handler.watch(TestSender { sent_value: sent_value.clone() }).unwrap();

        // First call returns immediately if value is already set regardless of change fn
        assert_eq!((*sent_value.borrow()), ID2);
    }
}
