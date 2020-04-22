// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    cell::RefCell,
    rc::Rc,
    time::{Duration, Instant, SystemTime},
};

use super::{ComplexTime, TimeSource};

/// A default `TimeSource` implementation that uses `SystemTime::now()` and `Instant::now()` to get
/// the current times.
pub struct StandardTimeSource;
impl TimeSource for StandardTimeSource {
    fn now_in_walltime(&self) -> SystemTime {
        SystemTime::now()
    }
    fn now_in_monotonic(&self) -> Instant {
        Instant::now()
    }
    fn now(&self) -> ComplexTime {
        ComplexTime::from((SystemTime::now(), Instant::now()))
    }
}

/// A mock `TimeSource` that can be manipulated as needed.
///
/// Meant to be used when writing tests.  As `SystemTime` is partly opaque and is `Instant`
/// completely are opaque, it needs to be initially constructed from a real source of those objects.
///
/// # Example
/// ```
/// let mock_source = MockTimeSource::new_from_now();
/// ```
///
/// The MockTimeSource uses `Clone`-able interior mutability to allow it to be manipulated and used
/// concurrently.
#[derive(Clone, Debug)]
pub struct MockTimeSource {
    time: Rc<RefCell<ComplexTime>>,
}

impl TimeSource for MockTimeSource {
    fn now_in_walltime(&self) -> SystemTime {
        self.time.borrow().wall
    }
    fn now_in_monotonic(&self) -> Instant {
        self.time.borrow().mono
    }
    fn now(&self) -> ComplexTime {
        *(self.time.borrow())
    }
}

impl MockTimeSource {
    /// Create a new `MockTimeSource` from a `ComplexTime` to be it's initial time.
    ///
    /// # Example
    /// ```
    /// let current_time = StandardTimeSource.now();
    /// let mock_source = MockTimeSource.from(current_time);
    /// mock_source.advance(Duration::from_secs(3600))
    /// let next_time = mock_source.now();
    /// ```
    pub fn new(t: impl Into<ComplexTime>) -> Self {
        MockTimeSource { time: Rc::new(RefCell::new(t.into())) }
    }

    /// Create a new `MockTimeSource`, initialized to the values from `SystemTime` and `Instant`
    pub fn new_from_now() -> Self {
        Self::new(StandardTimeSource.now())
    }

    /// Advance the mock time source forward (e.g. during a test)
    ///
    /// If the `MockTimeSource` has been Cloned, this will advance both.
    ///
    /// # Example
    /// ```
    /// let one_mock_source = MockTimeSource::from_now();
    /// let initial_time = one_mock_source.now();
    /// let two_mock_source = one_mock_source.clone();
    ///
    /// let one_hour = Duration::from_secs(3600);
    /// one_mock_source.advance(one_hour);
    /// let later_time = one_mock_source.now();
    ///
    /// assert_eq!(one_mock_source.now(), two_mock_source.now());
    /// assert_eq!(one_mock_source.now(), initial_time + one_hour);
    /// assert_eq!(two_mock_source.now(), initial_time + one_hour);
    /// ```
    ///
    /// This method uses a mutable reference for `self` for clarity.  The interior mutability of the
    /// time does not require it.
    pub fn advance(&mut self, duration: Duration) {
        let mut borrowed_time = self.time.borrow_mut();
        *borrowed_time += duration;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_impl_time_source_for_mock_time_source() {
        let time = StandardTimeSource.now();
        let mock_source = MockTimeSource::new(time);
        assert_eq!(mock_source.now(), time);
        assert_eq!(mock_source.now_in_walltime(), time.wall);
        assert_eq!(mock_source.now_in_monotonic(), time.mono);
    }

    /// Test that the MockTimeSource doesn't move on it's own.
    #[test]
    fn test_mock_timesource_doesnt_advance_on_its_own() {
        let source = MockTimeSource::new_from_now();
        let now = source.now();
        std::thread::sleep(Duration::from_millis(100));
        let same_as_now = source.now();

        assert_eq!(same_as_now, now);
    }

    /// Test that the mock timesource will advance in the expected way when it's asked to.
    #[test]
    fn test_mock_timesource_will_advance() {
        let mut source = MockTimeSource::new_from_now();
        let now = source.now();

        let duration = Duration::from_secs(60);
        source.advance(duration);

        let later = source.now();
        let expected = now + duration;

        assert_eq!(later, expected);
    }

    #[test]
    fn test_cloned_mock_time_source_also_advances() {
        let mut one_mock_source = MockTimeSource::new_from_now();
        let initial_time = one_mock_source.now();
        let two_mock_source = one_mock_source.clone();

        let one_hour = Duration::from_secs(3600);
        one_mock_source.advance(one_hour);
        let later_time = one_mock_source.now();

        assert_eq!(later_time, initial_time + one_hour);
        assert_eq!(two_mock_source.now(), initial_time + one_hour);
    }
}
