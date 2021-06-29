// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::mem;
use std::sync::Arc;

use crate::fs::FdEvents;
use crate::task::Waiter;
use crate::types::*;

struct Observer {
    waiter: Arc<Waiter>,
    events: FdEvents,
    persistent: bool,
}

#[derive(Default)]
pub struct ObserverList {
    observers: Mutex<Vec<Observer>>,
}

impl ObserverList {
    fn add_observer(&self, waiter: &Arc<Waiter>, events: FdEvents, persistent: bool) {
        let mut observers = self.observers.lock();
        (*observers).push(Observer { waiter: Arc::clone(waiter), events, persistent });
    }

    pub fn wait_once(&self, waiter: &Arc<Waiter>, events: FdEvents) -> Result<(), Errno> {
        self.add_observer(waiter, events, false);
        waiter.wait()
    }

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
