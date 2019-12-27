// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

/// Future resolves once it has determined the system's health state.
/// Returns Ok(()) on healthy and Err(reason) on unhealthy.
/// Used after a system update to determine if the device should be rolled back.
pub async fn check_system_health() -> Result<(), Error> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async::{self as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn test_succeeds() -> Result<(), Error> {
        check_system_health().await
    }
}
