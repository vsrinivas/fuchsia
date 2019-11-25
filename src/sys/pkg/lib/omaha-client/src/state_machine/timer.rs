// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::BoxFuture;
use futures::prelude::*;
use std::time::Duration;

/// A Timer that can block for a duration specified.
pub trait Timer {
    /// Returns a future that will block for a duration specified by |delay|.
    fn wait(&mut self, delay: Duration) -> BoxFuture<'static, ()>;
}

/// A stub timer that doesn't wait.
pub struct StubTimer;

impl Timer for StubTimer {
    fn wait(&mut self, _delay: Duration) -> BoxFuture<'static, ()> {
        future::ready(()).boxed()
    }
}

#[cfg(test)]
pub use mock::MockTimer;

#[cfg(test)]
mod mock {
    use super::*;
    use futures::executor::{block_on, LocalPool};
    use futures::task::LocalSpawnExt;
    use std::collections::VecDeque;

    /// A mocked timer that will assert expected durations.
    #[derive(Debug)]
    pub struct MockTimer {
        expected_durations: VecDeque<(Duration, Duration)>,
    }

    impl MockTimer {
        pub fn new() -> Self {
            MockTimer { expected_durations: VecDeque::new() }
        }

        /// Add a new duration to the end of the expected durations.
        pub fn expect(&mut self, duration: Duration) {
            self.expect_range(duration, duration);
        }

        /// Add a new duration range to the end of the expected durations.
        pub fn expect_range(&mut self, min_duration: Duration, max_duration: Duration) {
            self.expected_durations.push_back((min_duration, max_duration));
        }
    }

    impl Timer for MockTimer {
        fn wait(&mut self, duration: Duration) -> BoxFuture<'static, ()> {
            if let Some((min_duration, max_duration)) = self.expected_durations.pop_front() {
                assert!(
                    duration >= min_duration && duration <= max_duration,
                    "{:?} out of range [{:?}, {:?}]",
                    duration,
                    min_duration,
                    max_duration
                );
                future::ready(()).boxed()
            } else {
                // No more expected durations left, blocking the Timer forever.
                // Users of MockTimer are expected to use run_until_stalled()
                // if timer is used in an infinite loop.
                future::pending().boxed()
            }
        }
    }

    impl Drop for MockTimer {
        fn drop(&mut self) {
            // Make sure all the expected durations have been waited on.
            assert!(self.expected_durations.is_empty());
        }
    }

    #[test]
    fn test_simple() {
        let mut timer = MockTimer::new();
        timer.expect(Duration::from_secs(5555));

        block_on(timer.wait(Duration::from_secs(5555)));
    }

    #[test]
    fn test_wait_twice() {
        let mut timer = MockTimer::new();
        timer.expect(Duration::from_secs(5555));
        timer.expect(Duration::from_secs(6666));

        block_on(async {
            timer.wait(Duration::from_secs(5555)).await;
            timer.wait(Duration::from_secs(6666)).await;
        });
    }

    #[test]
    fn test_wait_loop() {
        let mut timer = MockTimer::new();
        timer.expect(Duration::from_secs(1));
        timer.expect(Duration::from_secs(2));
        timer.expect(Duration::from_secs(3));

        let mut pool = LocalPool::new();
        pool.spawner()
            .spawn_local(async move {
                let mut i = 1;
                loop {
                    timer.wait(Duration::from_secs(i)).await;
                    i += 1;
                }
            })
            .unwrap();
        pool.run_until_stalled();
    }

    #[test]
    #[should_panic(expected = "5555")]
    fn test_wrong_time() {
        let mut timer = MockTimer::new();
        timer.expect(Duration::from_secs(5555));

        block_on(timer.wait(Duration::from_secs(6666)));
    }

    #[test]
    #[should_panic(expected = "is_empty()")]
    fn test_expect_more_wait() {
        let mut timer = MockTimer::new();
        timer.expect(Duration::from_secs(5555));
        timer.expect(Duration::from_secs(6666));

        block_on(timer.wait(Duration::from_secs(5555)));
    }
}
