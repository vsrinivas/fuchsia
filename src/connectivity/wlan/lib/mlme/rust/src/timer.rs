// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    fuchsia_zircon::{self as zx, DurationNum},
    std::collections::HashMap,
    std::ffi::c_void,
};

#[derive(PartialEq, Eq, Hash, Debug, Copy, Clone)]
#[repr(C)]
pub struct EventId(u64);

/// A scheduler to schedule and cancel timeouts.
#[repr(C)]
pub struct Scheduler {
    cookie: *mut c_void,
    /// Requests to schedule an event. Returns a a unique ID used to cancel the scheduled event.
    schedule: extern "C" fn(cookie: *mut c_void, deadline: i64) -> EventId,
    /// Cancels a previously scheduled event.
    cancel: extern "C" fn(cookie: *mut c_void, id: EventId),
}

/// A timer to schedule and cancel timeouts and retrieve triggered events.
pub struct Timer<E> {
    events: HashMap<EventId, E>,
    scheduler: Scheduler,
}

impl<E> Timer<E> {
    pub fn triggered(&mut self, event_id: &EventId) -> Option<E> {
        self.events.remove(event_id)
    }
}

impl<E> Timer<E> {
    pub fn new(scheduler: Scheduler) -> Self {
        Self { events: HashMap::default(), scheduler }
    }

    pub fn schedule_event(&mut self, deadline: zx::Time, event: E) -> EventId {
        let event_id = (self.scheduler.schedule)(self.scheduler.cookie, deadline.into_nanos());
        self.events.insert(event_id, event);
        event_id
    }

    pub fn cancel_event(&mut self, event_id: EventId) {
        self.events.remove(&event_id);
        (self.scheduler.cancel)(self.scheduler.cookie, event_id);
    }

    pub fn cancel_all(&mut self) {
        for (event_id, event) in &self.events {
            (self.scheduler.cancel)(self.scheduler.cookie, *event_id);
        }
        self.events.clear();
    }
}

#[cfg(test)]
pub struct FakeScheduler {
    next_id: u64,
}

#[cfg(test)]
impl FakeScheduler {
    pub extern "C" fn schedule(cookie: *mut c_void, deadline: i64) -> EventId {
        unsafe {
            (*(cookie as *mut Self)).next_id += 1;
            EventId((*(cookie as *mut Self)).next_id)
        }
    }
    pub extern "C" fn cancel(cookie: *mut c_void, id: EventId) {}

    pub fn new() -> Self {
        Self { next_id: 0 }
    }

    pub fn as_scheduler(&mut self) -> Scheduler {
        Scheduler {
            cookie: self as *mut Self as *mut c_void,
            cancel: Self::cancel,
            schedule: Self::schedule,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn schedule_cancel_event() {
        #[derive(PartialEq, Eq, Debug, Hash)]
        struct FooEvent(u8);

        let mut fake_scheduler = FakeScheduler::new();
        let scheduler = fake_scheduler.as_scheduler();

        // Verify event triggers no more than once.
        let mut timer = Timer::<FooEvent>::new(scheduler);
        let deadline = zx::Time::after(5i64.nanos());
        let event_id = timer.schedule_event(deadline, FooEvent(8));
        assert_eq!(timer.triggered(&event_id), Some(FooEvent(8)));
        assert_eq!(timer.triggered(&event_id), None);

        // Verify event does not trigger if it was canceled.
        let event_id = timer.schedule_event(deadline, FooEvent(9));
        timer.cancel_event(event_id);
        assert_eq!(timer.triggered(&event_id), None);

        // Verify multiple events can be scheduled and canceled.
        let event_id_1 = timer.schedule_event(deadline, FooEvent(8));
        let event_id_2 = timer.schedule_event(deadline, FooEvent(9));
        let event_id_3 = timer.schedule_event(deadline, FooEvent(10));
        timer.cancel_event(event_id_2);
        assert_eq!(timer.triggered(&event_id_2), None);
        assert_eq!(timer.triggered(&event_id_3), Some(FooEvent(10)));
        assert_eq!(timer.triggered(&event_id_1), Some(FooEvent(8)));
    }

    #[test]
    fn cancel_all() {
        let mut fake_scheduler = FakeScheduler::new();
        let scheduler = fake_scheduler.as_scheduler();
        let mut timer = Timer::<_>::new(scheduler);
        let deadline = zx::Time::after(5i64.nanos());

        let event_id_1 = timer.schedule_event(deadline, 8);
        let event_id_2 = timer.schedule_event(deadline, 9);
        let event_id_3 = timer.schedule_event(deadline, 10);
        timer.cancel_all();
        assert_eq!(timer.triggered(&event_id_1), None);
        assert_eq!(timer.triggered(&event_id_2), None);
        assert_eq!(timer.triggered(&event_id_3), None);
    }
}
