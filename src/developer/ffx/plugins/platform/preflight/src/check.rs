// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::PreflightConfig;
use anyhow::Result;
use async_trait::async_trait;

pub mod build_prereqs;
pub mod femu_graphics;

/// The result of execution of a `PreflightCheck`. In all cases, the first `String` parameter
/// contains a message for the end user explaining the result.
#[derive(Debug)]
pub enum PreflightCheckResult {
    /// Everything checked out!
    Success(String),

    /// A non-optimal configuration was detected. It will not stop the user from
    /// building/running Fuchsia, but the experience will be degraded.
    Warning(String),

    /// An unsupported condition or configuration was detected. The optional
    /// second parameter contains instructions for resolving the issue. `None`
    /// indicates the issue cannot be resolved.
    Failure(String, Option<String>),
}

#[async_trait(?Send)]
pub trait PreflightCheck {
    /// Runs the check with `config`.
    ///
    /// Returns `Ok(PreflightCheckResult)` if the check was able to execute
    /// to completion, and `Err()` otherwise.
    async fn run(&self, config: &PreflightConfig) -> Result<PreflightCheckResult>;
}
