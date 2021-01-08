// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::errors::HealthCheckError;

/// Dummy function to indicate where health checks will eventually go, and how to handle associated
/// errors.
pub async fn do_health_checks() -> Result<(), HealthCheckError> {
    Ok(())
}
