// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported Feedback commands.
#[derive(Debug, PartialEq)]
pub enum FeedbackDataProviderMethod {
    /// Wraps `fuchsia.feedback.DataProvider#GetSnapshot`
    GetSnapshot,
}

impl std::str::FromStr for FeedbackDataProviderMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetSnapshot" => Ok(FeedbackDataProviderMethod::GetSnapshot),
            _ => {
                return Err(format_err!("invalid Feedback DataProvider Facade method: {}", method))
            }
        }
    }
}
