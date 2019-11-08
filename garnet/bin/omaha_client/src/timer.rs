// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use futures::future::BoxFuture;
use futures::prelude::*;
use omaha_client::state_machine::Timer;
use std::time::Duration;

pub struct FuchsiaTimer;

impl Timer for FuchsiaTimer {
    fn wait(&mut self, delay: Duration) -> BoxFuture<'static, ()> {
        fasync::Timer::new(fasync::Time::after(delay.into())).boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::task::Poll;

    #[test]
    fn test_timer() {
        let mut exec = fasync::Executor::new().unwrap();
        let start_time = fasync::Time::now();

        let mut timer = FuchsiaTimer;
        let mut future = timer.wait(Duration::from_secs(1234));
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut future));

        let future_time = exec.wake_next_timer().unwrap();
        assert_eq!(1234, (future_time - start_time).into_seconds());

        assert_eq!(Poll::Ready(()), exec.run_until_stalled(&mut future));
    }
}
