// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::mem;
use std::sync::Arc;

use crate::fs::FdEvents;
use crate::task::Waiter;

/// An entry in an ObserverList.
struct Observer {
    /// The waiter that is waking for the FdEvent.
    waiter: Arc<Waiter>,

    /// The FdEvents bitmaks that the waiter is waiting for.
    events: FdEvents,

    /// Whether the observer wishes to remain in the ObserverList after one of
    /// the FdEvents that the waiter is waiting for occurs.
    persistent: bool,
}

/// A list of waiters waiting for FdEvents.
///
/// For FdEvents that are generated inside Starnix, we walk the observer list
/// on the thread that triggered the event to notify the waiters that the event
/// has occurred. The waiters will then wake up on their own thread to handle
/// the event.
#[derive(Default)]
pub struct ObserverList {
    /// The list of observers.
    observers: Mutex<Vec<Observer>>,
}

impl ObserverList {
    /// Establish a wait for the given FdEvents.
    ///
    /// The waiter will be notified when an event matching the events mask
    /// occurs.
    ///
    /// This function does not actually block the waiter. To block the waiter,
    /// call the "wait" function on the waiter.
    pub fn wait_async(&self, waiter: &Arc<Waiter>, events: FdEvents) {
        let mut observers = self.observers.lock();
        (*observers).push(Observer { waiter: Arc::clone(waiter), events, persistent: false });
    }

    /// Notify any observers that the given events have occurred.
    ///
    /// Walks the observer list and wakes each observer that is waiting on an
    /// event that matches the given mask. Persistent observers remain in the
    /// list. Non-persistent observers are removed.
    ///
    /// The waiters will wake up on their own threads to handle these events.
    /// They are not called synchronously by this function.
    pub fn notify(&self, events: FdEvents) {
        let mut observers = self.observers.lock();
        *observers = mem::take(&mut *observers)
            .into_iter()
            .filter(|observer| {
                if observer.events & events {
                    observer.waiter.wake();
                    return observer.persistent;
                }
                return true;
            })
            .collect();
    }
}
