// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_zircon as zx,
    futures::{future::FutureExt, Future},
    std::{convert::TryFrom, io, time::Duration},
    trust_dns_proto::{Executor, Time},
};

pub mod async_resolver;
pub mod tcp;
pub mod udp;

/// A Fuchsia Executor which implements the `trust_dns_proto::Executor` trait.
pub struct FuchsiaExec(fasync::Executor);

impl FuchsiaExec {
    /// Constructs a Fuchsia Executor.
    pub fn new() -> Result<Self, zx::Status> {
        fasync::Executor::new().map(|fexec| Self(fexec))
    }

    /// Gets a mutable reference to the internal `fuchsia_async::Executor`.
    pub fn get(&mut self) -> &mut fasync::Executor {
        &mut self.0
    }
}

impl Executor for FuchsiaExec {
    fn new() -> Self {
        Self::new().expect("failed to create fuchsia executor")
    }

    fn block_on<F: Future>(&mut self, future: F) -> F::Output {
        self.0.run_singlethreaded(future)
    }
}

/// A Fuchsia Time with time-related capabilities which implements the `trust_dns_proto::Time` trait.
pub struct FuchsiaTime;

#[async_trait]
impl Time for FuchsiaTime {
    async fn timeout<F: 'static + Future + Send>(
        duration: Duration,
        future: F,
    ) -> Result<F::Output, io::Error> {
        let nanos = i64::try_from(duration.as_nanos()).expect("failed to cast the input into i64 ");
        let zx_duration = zx::Duration::from_nanos(nanos);

        future
            .map(|output| Ok(output))
            .on_timeout(fasync::Time::after(zx_duration), || {
                Err(io::Error::new(io::ErrorKind::TimedOut, "future timed out"))
            })
            .await
    }

    async fn delay_for(duration: Duration) -> () {
        let nanos = i64::try_from(duration.as_nanos()).expect("failed to cast the input into i64");
        let zx_duration = zx::Duration::from_nanos(nanos);
        fasync::Timer::new(fasync::Time::after(zx_duration)).await
    }
}
