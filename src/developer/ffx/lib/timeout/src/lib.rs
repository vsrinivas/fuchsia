// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async::Timer;
use futures_lite::prelude::*;
use futures_util::future::FutureExt as _;
use std::time::Duration;
use thiserror::Error;

#[derive(Error, Debug, PartialEq, Eq)]
#[error("Timed out")]
pub struct TimeoutError();

pub async fn timeout<F, T>(dur: Duration, f: F) -> Result<T, TimeoutError>
where
    F: Future<Output = T>,
{
    f.map(|o| Ok(o)).or(Timer::new(dur).map(|_| Err(TimeoutError {}))).await
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_timeout() {
        assert_eq!(
            timeout(Duration::from_secs(1), async { "hello world" }).await,
            Ok("hello world")
        );
        assert_eq!(
            timeout(Duration::from_millis(1), async { Timer::new(Duration::from_secs(1)).await })
                .await,
            Err(TimeoutError {})
        );
    }
}
