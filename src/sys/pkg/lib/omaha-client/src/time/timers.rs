// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use super::PartialComplexTime;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ExpectedWait {
    Until(PartialComplexTime),
    For(Duration, Duration),
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RequestedWait {
    Until(PartialComplexTime),
    For(Duration),
}

pub use stub::StubTimer;

mod stub {
    use super::super::*;
    use futures::future::BoxFuture;
    use futures::prelude::*;

    pub struct StubTimer;
    impl Timer for StubTimer {
        /// Wait until at least one of the given time bounds has been reached.
        fn wait_until(&mut self, _time: impl Into<PartialComplexTime>) -> BoxFuture<'static, ()> {
            future::ready(()).boxed()
        }

        /// Wait for the given duration (from now).
        fn wait_for(&mut self, _duration: Duration) -> BoxFuture<'static, ()> {
            future::ready(()).boxed()
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use futures::executor::block_on;
        use std::time::Duration;

        #[test]
        fn test_wait_always_ready() {
            block_on(StubTimer.wait_until(StandardTimeSource.now() + Duration::from_secs(5555)));
            block_on(StubTimer.wait_for(Duration::from_secs(5555)));
        }
    }
}

pub use mock::MockTimer;

mod mock {
    use super::super::*;
    use super::{ExpectedWait, RequestedWait};
    use futures::future::BoxFuture;
    use futures::prelude::*;
    use std::{cell::RefCell, collections::VecDeque, fmt::Debug, rc::Rc};

    /// A mocked timer that will assert expected waits, and block forever after it has used them.
    #[derive(Debug)]
    pub struct MockTimer {
        expected_waits: VecDeque<ExpectedWait>,
        requested_waits: Rc<RefCell<Vec<RequestedWait>>>,
    }

    impl MockTimer {
        pub fn new() -> Self {
            MockTimer {
                expected_waits: VecDeque::new(),
                requested_waits: Rc::new(RefCell::new(Vec::new())),
            }
        }

        /// Expect a wait until the given PartialComplexTime.
        pub fn expect_until(&mut self, time: impl Into<PartialComplexTime>) {
            self.expected_waits.push_back(ExpectedWait::Until(time.into()))
        }

        /// Expect a wait for the given Duration.
        pub fn expect_for(&mut self, duration: Duration) {
            self.expected_waits.push_back(ExpectedWait::For(duration, duration))
        }

        /// Add a new wait to the end of the expected durations.
        pub fn expect_for_range(&mut self, min: Duration, max: Duration) {
            self.expected_waits.push_back(ExpectedWait::For(min, max))
        }

        /// Check that a given Wait was expected.  If no expected waits have been set, then
        /// do nothing after recording the Wait.
        fn handle_wait(&mut self, requested: RequestedWait) -> BoxFuture<'static, ()> {
            if let Some(expected) = self.expected_waits.pop_front() {
                match (requested, expected) {
                    (RequestedWait::For(duration), ExpectedWait::For(min, max)) => {
                        assert!(
                            duration >= min && duration <= max,
                            "{:?} out of range [{:?}, {:?}]",
                            duration,
                            min,
                            max,
                        );
                    }
                    (RequestedWait::Until(requested), ExpectedWait::Until(expected)) => {
                        assert!(
                            requested == expected,
                            "wait_until() called with wrong time: {}, expected {}",
                            requested,
                            expected
                        );
                    }
                    (requested, expected) => {
                        panic!(
                            "Timer called with wrong wait: {:?}, expected {:?}",
                            requested, expected
                        );
                    }
                }
                self.requested_waits.borrow_mut().push(requested);
                future::ready(()).boxed()
            } else {
                // No more expected durations left, blocking the Timer forever.
                // Users of MockTimer are expected to use run_until_stalled()
                // if timer is used in an infinite loop.
                future::pending().boxed()
            }
        }

        pub fn get_requested_waits_view(&self) -> Rc<RefCell<Vec<RequestedWait>>> {
            Rc::clone(&self.requested_waits)
        }
    }

    impl Default for MockTimer {
        fn default() -> Self {
            Self::new()
        }
    }

    impl Timer for MockTimer {
        /// Wait until at least one of the given time bounds has been reached.
        fn wait_until(&mut self, time: impl Into<PartialComplexTime>) -> BoxFuture<'static, ()> {
            self.handle_wait(RequestedWait::Until(time.into()))
        }

        /// Wait for the given duration (from now).
        fn wait_for(&mut self, duration: Duration) -> BoxFuture<'static, ()> {
            self.handle_wait(RequestedWait::For(duration))
        }
    }

    impl Drop for MockTimer {
        fn drop(&mut self) {
            // Make sure all the expected durations have been waited on.
            assert!(self.expected_waits.is_empty());
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use futures::executor::{block_on, LocalPool};
        use futures::task::LocalSpawnExt;
        use std::time::Duration;

        #[test]
        fn test_wait_until_expected() {
            let mock_time = MockTimeSource::new_from_now();
            let time = mock_time.now() + Duration::from_secs(5555);

            let mut timer = MockTimer::new();
            timer.expect_until(time);

            block_on(timer.wait_until(time));
        }

        #[test]
        fn test_wait_for_expected() {
            let mut timer = MockTimer::new();
            timer.expect_for(Duration::from_secs(5555));

            block_on(timer.wait_for(Duration::from_secs(5555)));
        }

        #[test]
        fn test_wait_for_twice() {
            let mut timer = MockTimer::new();
            timer.expect_for(Duration::from_secs(5555));
            timer.expect_for(Duration::from_secs(6666));

            block_on(async {
                timer.wait_for(Duration::from_secs(5555)).await;
                timer.wait_for(Duration::from_secs(6666)).await;
            });
        }

        #[test]
        fn test_wait_for_loop() {
            let mut timer = MockTimer::new();
            timer.expect_for(Duration::from_secs(1));
            timer.expect_for(Duration::from_secs(2));
            timer.expect_for(Duration::from_secs(3));

            let mut pool = LocalPool::new();
            pool.spawner()
                .spawn_local(async move {
                    let mut i = 1;
                    loop {
                        timer.wait_for(Duration::from_secs(i)).await;
                        i += 1;
                    }
                })
                .unwrap();
            pool.run_until_stalled();
        }

        #[test]
        fn test_wait_for_expected_duration() {
            let mut timer = MockTimer::new();
            timer.expect_for_range(Duration::from_secs(10), Duration::from_secs(20));

            block_on(async { timer.wait_for(Duration::from_secs(15)) });
        }

        #[test]
        #[should_panic(expected = "out of range")]
        fn test_wait_for_expected_duration_out_of_range_low() {
            let mut timer = MockTimer::new();
            timer.expect_for_range(Duration::from_secs(10), Duration::from_secs(20));

            block_on(async { timer.wait_for(Duration::from_secs(3)) });
        }

        #[test]
        #[should_panic(expected = "out of range")]
        fn test_wait_for_expected_duration_out_of_range_high() {
            let mut timer = MockTimer::new();
            timer.expect_for_range(Duration::from_secs(10), Duration::from_secs(20));

            block_on(async { timer.wait_for(Duration::from_secs(30)) });
        }

        #[test]
        #[should_panic(expected = "5555")]
        fn test_wait_for_wrong_duration() {
            let mut timer = MockTimer::new();
            timer.expect_for(Duration::from_secs(5555));

            block_on(timer.wait_for(Duration::from_secs(6666)));
        }

        #[test]
        #[should_panic(expected = "is_empty()")]
        fn test_expect_more_wait_for() {
            let mut timer = MockTimer::new();
            timer.expect_for(Duration::from_secs(5555));
            timer.expect_for(Duration::from_secs(6666));

            block_on(timer.wait_for(Duration::from_secs(5555)));
        }

        #[test]
        #[should_panic(expected = "Timer called with wrong wait")]
        fn test_wait_for_wrong_wait() {
            let mock_time = MockTimeSource::new_from_now();
            let time = mock_time.now() + Duration::from_secs(5555);

            let mut timer = MockTimer::new();
            timer.expect_until(time);

            block_on(timer.wait_for(Duration::from_secs(6666)));
        }
    }
}

pub use blocking::{BlockedTimer, BlockingTimer, InfiniteTimer};

mod blocking {
    use super::super::*;
    use super::RequestedWait;
    use futures::channel::{mpsc, oneshot};
    use futures::future::BoxFuture;
    use futures::prelude::*;

    /// A mock timer that will notify a channel when creating a timer.
    #[derive(Debug)]
    pub struct BlockingTimer {
        chan: mpsc::Sender<BlockedTimer>,
    }

    /// An omaha state machine timer waiting to be unblocked. Dropping a BlockedTimer will cause
    /// the timer to panic.
    #[derive(Debug)]
    pub struct BlockedTimer {
        wait: RequestedWait,
        unblock: oneshot::Sender<()>,
    }

    impl BlockingTimer {
        /// Returns a new BlockingTimer and a channel to receive BlockedTimer instances.
        pub fn new() -> (Self, mpsc::Receiver<BlockedTimer>) {
            let (send, recv) = mpsc::channel(0);
            (Self { chan: send }, recv)
        }

        fn wait(&mut self, wait: RequestedWait) -> BoxFuture<'static, ()> {
            let mut chan = self.chan.clone();

            async move {
                let (send, recv) = oneshot::channel();
                chan.send(BlockedTimer { wait, unblock: send }).await.unwrap();

                recv.await.unwrap();
            }
            .boxed()
        }
    }

    impl BlockedTimer {
        /// The requested duration of this timer.
        pub fn requested_wait(&self) -> RequestedWait {
            self.wait
        }

        /// Unblock the timer, panicing if it no longer exists.
        pub fn unblock(self) {
            self.unblock.send(()).unwrap()
        }
    }

    impl Timer for BlockingTimer {
        /// Wait until at least one of the given time bounds has been reached.
        fn wait_until(&mut self, time: impl Into<PartialComplexTime>) -> BoxFuture<'static, ()> {
            self.wait(RequestedWait::Until(time.into()))
        }

        /// Wait for the given duration (from now).
        fn wait_for(&mut self, duration: Duration) -> BoxFuture<'static, ()> {
            self.wait(RequestedWait::For(duration))
        }
    }

    /// A mock timer that will block forever.
    #[derive(Debug)]
    pub struct InfiniteTimer;

    impl Timer for InfiniteTimer {
        /// Wait until at least one of the given time bounds has been reached.
        fn wait_until(&mut self, _time: impl Into<PartialComplexTime>) -> BoxFuture<'static, ()> {
            future::pending().boxed()
        }

        /// Wait for the given duration (from now).
        fn wait_for(&mut self, _duration: Duration) -> BoxFuture<'static, ()> {
            future::pending().boxed()
        }
    }
}
