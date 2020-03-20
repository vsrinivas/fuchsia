// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_update::CheckNotStartedReason;
use serde::{Deserialize, Serialize};

/// Enum for supported update related commands.
pub enum UpdateMethod {
    CheckNow,
    GetCurrentChannel,
    GetTargetChannel,
    SetTargetChannel,
    GetChannelList,
}

impl std::str::FromStr for UpdateMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "CheckNow" => Ok(UpdateMethod::CheckNow),
            "GetCurrentChannel" => Ok(UpdateMethod::GetCurrentChannel),
            "GetTargetChannel" => Ok(UpdateMethod::GetTargetChannel),
            "SetTargetChannel" => Ok(UpdateMethod::SetTargetChannel),
            "GetChannelList" => Ok(UpdateMethod::GetChannelList),
            _ => return Err(format_err!("Invalid Update Facade method: {}", method)),
        }
    }
}

#[derive(Serialize, Debug, Deserialize, PartialEq, Eq)]
pub(super) enum CheckStartedResultDef {
    Started,
    Internal,
    InvalidOptions,
    AlreadyInProgress,
    Throttled,
}

#[derive(Serialize, Debug, Deserialize, PartialEq, Eq)]
pub(super) struct CheckNowResult {
    pub check_started: CheckStartedResultDef,
}

impl From<Result<(), CheckNotStartedReason>> for CheckNowResult {
    fn from(other: Result<(), CheckNotStartedReason>) -> Self {
        let check_started = match other {
            Ok(()) => CheckStartedResultDef::Started,
            Err(CheckNotStartedReason::Internal) => CheckStartedResultDef::Internal,
            Err(CheckNotStartedReason::InvalidOptions) => CheckStartedResultDef::InvalidOptions,
            Err(CheckNotStartedReason::AlreadyInProgress) => {
                CheckStartedResultDef::AlreadyInProgress
            }
            Err(CheckNotStartedReason::Throttled) => CheckStartedResultDef::Throttled,
        };
        Self { check_started }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{json, to_value};

    #[test]
    fn serialize_check_started_result() {
        assert_eq!(
            to_value(CheckNowResult { check_started: CheckStartedResultDef::Started }).unwrap(),
            json!({"check_started": "Started"})
        );
    }

    #[test]
    fn convert_check_now_result() {
        assert_eq!(
            CheckNowResult::from(Ok(())),
            CheckNowResult { check_started: CheckStartedResultDef::Started }
        );
        assert_eq!(
            CheckNowResult::from(Err(CheckNotStartedReason::InvalidOptions)),
            CheckNowResult { check_started: CheckStartedResultDef::InvalidOptions }
        );
    }
}
