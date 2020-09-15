// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon as zx, std::collections::HashMap, std::ffi::c_void};

#[derive(PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Copy, Clone, Default)]
#[repr(C)]
pub struct EventId(u64);

#[cfg(test)]
impl From<u64> for EventId {
    fn from(id: u64) -> Self {
        Self(id)
    }
}

/// A scheduler to schedule and cancel timeouts.
#[repr(C)]
pub struct Scheduler {
    cookie: *mut c_void,
    /// Returns the current system time in nano seconds.
    now: extern "C" fn(cookie: *mut c_void) -> i64,
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

    pub fn now(&self) -> zx::Time {
        let nanos = (self.scheduler.now)(self.scheduler.cookie);
        zx::Time::from_nanos(nanos)
    }

    pub fn schedule_event(&mut self, deadline: zx::Time, event: E) -> EventId {
        let event_id = (self.scheduler.schedule)(self.scheduler.cookie, deadline.into_nanos());
        self.events.insert(event_id, event);
        event_id
    }

    pub fn cancel_event(&mut self, event_id: EventId) {
        if let Some(_) = self.events.remove(&event_id) {
            (self.scheduler.cancel)(self.scheduler.cookie, event_id);
        }
    }

    pub fn cancel_all(&mut self) {
        for (event_id, _event) in &self.events {
            (self.scheduler.cancel)(self.scheduler.cookie, *event_id);
        }
        self.events.clear();
    }

    #[cfg(test)]
    pub fn scheduled_event_count(&self) -> usize {
        self.events.len()
    }
}

impl<E: PartialEq> Timer<E> {
    #[cfg(test)]
    pub fn scheduled_events(&self, event_type: E) -> Vec<EventId> {
        self.events
            .iter()
            .filter(|(_, event)| *event == &event_type)
            .map(|(id, _)| id)
            .cloned()
            .collect()
    }
}

#[cfg(test)]
pub struct FakeScheduler {
    pub next_id: u64,
    pub deadlines: HashMap<EventId, zx::Time>,
    now: i64,
}

#[cfg(test)]
impl FakeScheduler {
    pub extern "C" fn schedule(cookie: *mut c_void, deadline: i64) -> EventId {
        let scheduler = unsafe { &mut *(cookie as *mut Self) };
        scheduler.next_id += 1;
        let event_id = EventId(scheduler.next_id);
        scheduler.deadlines.insert(event_id, zx::Time::from_nanos(deadline));
        event_id
    }
    pub extern "C" fn cancel(cookie: *mut c_void, id: EventId) {
        let scheduler = unsafe { &mut *(cookie as *mut Self) };
        scheduler.deadlines.remove(&id);
    }

    pub extern "C" fn now(cookie: *mut c_void) -> i64 {
        unsafe { (*(cookie as *mut Self)).now }
    }

    pub fn new() -> Self {
        Self { next_id: 0, deadlines: HashMap::new(), now: 0 }
    }

    pub fn set_time(&mut self, time: zx::Time) {
        self.now = time.into_nanos();
    }

    pub fn increment_time(&mut self, duration: zx::Duration) {
        self.set_time(zx::Time::from_nanos(self.now) + duration);
    }

    /// Evict and return ID and deadline of the earliest scheduled event
    pub fn next_event(&mut self) -> Option<(EventId, zx::Time)> {
        let event =
            self.deadlines.iter().min_by_key(|(_, deadline)| *deadline).map(|(id, d)| (*id, *d));
        if let Some((id, _)) = event {
            self.deadlines.remove(&id);
        }
        event
    }

    pub fn as_scheduler(&mut self) -> Scheduler {
        Scheduler {
            cookie: self as *mut Self as *mut c_void,
            now: Self::now,
            cancel: Self::cancel,
            schedule: Self::schedule,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_zircon::DurationNum};

    #[derive(PartialEq, Eq, Debug, Hash)]
    struct FooEvent(u8);

    #[test]
    fn schedule_cancel_event() {
        let mut fake_scheduler = FakeScheduler::new();
        let scheduler = fake_scheduler.as_scheduler();

        // Verify event triggers no more than once.
        let mut timer = Timer::<FooEvent>::new(scheduler);
        let deadline = zx::Time::after(5_i64.nanos());
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
        let deadline = zx::Time::after(5_i64.nanos());

        let event_id_1 = timer.schedule_event(deadline, 8);
        let event_id_2 = timer.schedule_event(deadline, 9);
        let event_id_3 = timer.schedule_event(deadline, 10);
        timer.cancel_all();
        assert_eq!(timer.triggered(&event_id_1), None);
        assert_eq!(timer.triggered(&event_id_2), None);
        assert_eq!(timer.triggered(&event_id_3), None);
    }

    #[test]
    fn fake_scheduler_next_event() {
        let mut fake_scheduler = FakeScheduler::new();
        let scheduler = fake_scheduler.as_scheduler();

        let event_id_1 = FakeScheduler::schedule(scheduler.cookie, 2i64);
        let event_id_2 = FakeScheduler::schedule(scheduler.cookie, 4i64);
        let event_id_3 = FakeScheduler::schedule(scheduler.cookie, 1i64);
        let event_id_4 = FakeScheduler::schedule(scheduler.cookie, 3i64);

        assert_eq!(fake_scheduler.next_event(), Some((event_id_3, zx::Time::from_nanos(1i64))));
        assert_eq!(fake_scheduler.next_event(), Some((event_id_1, zx::Time::from_nanos(2i64))));
        assert_eq!(fake_scheduler.next_event(), Some((event_id_4, zx::Time::from_nanos(3i64))));
        assert_eq!(fake_scheduler.next_event(), Some((event_id_2, zx::Time::from_nanos(4i64))));
        assert_eq!(fake_scheduler.next_event(), None);
    }
}
