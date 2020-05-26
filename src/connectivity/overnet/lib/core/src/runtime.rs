// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use std::time::Instant;

/// Spawn a local future
#[cfg(not(target_os = "fuchsia"))]
pub fn spawn(future: impl Future<Output = ()> + 'static) {
    use tokio::runtime::current_thread;
    current_thread::spawn(future.unit_error().boxed_local().compat());
}

/// Spawn a local future
#[cfg(target_os = "fuchsia")]
pub fn spawn(future: impl Future<Output = ()> + 'static) {
    fuchsia_async::spawn_local(future);
}

/// Wait until some fixed time in the future
#[cfg(not(target_os = "fuchsia"))]
pub async fn wait_until(t: Instant) {
    futures::compat::Compat01As03::new(tokio::timer::Delay::new(t)).await.unwrap();
}

/// Wait until some fixed time in the future
#[cfg(target_os = "fuchsia")]
pub async fn wait_until(t: Instant) {
    let now = Instant::now();
    if t <= now {
        return;
    }
    fuchsia_async::Timer::new(fuchsia_zircon::Time::after((t - now).into()).into()).await;
}

/// Run until `future` finishes
#[cfg(target_os = "fuchsia")]
pub fn run<R>(future: impl Future<Output = R> + 'static) -> R {
    fuchsia_async::Executor::new().unwrap().run_singlethreaded(future)
}

/// Run until `future` finishes
#[cfg(not(target_os = "fuchsia"))]
pub fn run<R>(future: impl Future<Output = R> + 'static) -> R {
    use tokio::runtime::current_thread;
    current_thread::Runtime::new()
        .unwrap()
        .block_on(future.unit_error().boxed_local().compat())
        .unwrap()
}

pub async fn maybe_wait_until(t: Option<Instant>) {
    if let Some(t) = t {
        wait_until(t).await
    } else {
        futures::future::pending().await
    }
}
