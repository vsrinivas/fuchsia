// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config::PreflightConfig;
use anyhow::Result;
use async_trait::async_trait;
use std::fmt;

pub mod build_prereqs;
pub mod emu_acceleration;
pub mod emu_networking;
pub mod femu_graphics;
pub mod ssh_checks;

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

/// Summary after running all checks.
#[derive(Debug)]
pub enum RunSummary {
    /// All checks passed.
    Success,

    /// No failures, but some warnings.
    Warning,

    /// Some failures, but all of them are recoverable.
    RecoverableFailure,

    /// Some failures, and at least one is not recoverable.
    Failure,
}

#[async_trait(?Send)]
pub trait PreflightCheck {
    /// Runs the check with `config`.
    ///
    /// Returns `Ok(PreflightCheckResult)` if the check was able to execute
    /// to completion, and `Err()` otherwise.
    async fn run(&self, config: &PreflightConfig) -> Result<PreflightCheckResult>;
}

impl fmt::Display for RunSummary {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                RunSummary::Success => "success",
                RunSummary::Warning => "warning",
                RunSummary::RecoverableFailure => "recoverable_failure",
                RunSummary::Failure => "failure",
            }
        )
    }
}

pub fn summarize_results(results: &Vec<PreflightCheckResult>) -> RunSummary {
    let mut num_warnings = 0;
    let mut num_failures = 0;
    let mut num_failures_recoverable = 0;
    for result in results {
        if matches!(&result, PreflightCheckResult::Warning(..)) {
            num_warnings += 1;
        }
        if matches!(result, PreflightCheckResult::Failure(..)) {
            num_failures += 1;
            let is_recoverable = match &result {
                PreflightCheckResult::Failure(_, message) => message.is_some(),
                _ => false,
            };
            if is_recoverable {
                num_failures_recoverable += 1;
            }
        }
    }
    if num_warnings == 0 && num_failures == 0 {
        RunSummary::Success
    } else if num_failures == 0 {
        RunSummary::Warning
    } else {
        if num_failures == num_failures_recoverable {
            RunSummary::RecoverableFailure
        } else {
            RunSummary::Failure
        }
    }
}
