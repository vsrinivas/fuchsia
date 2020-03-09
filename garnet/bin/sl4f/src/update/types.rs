// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_update::{CheckStartedResult, ManagerState, State};
use serde_derive::{Deserialize, Serialize};

/// Enum for supported update related commands.
pub enum UpdateMethod {
    GetState,
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
            "GetState" => Ok(UpdateMethod::GetState),
            "CheckNow" => Ok(UpdateMethod::CheckNow),
            "GetCurrentChannel" => Ok(UpdateMethod::GetCurrentChannel),
            "GetTargetChannel" => Ok(UpdateMethod::GetTargetChannel),
            "SetTargetChannel" => Ok(UpdateMethod::SetTargetChannel),
            "GetChannelList" => Ok(UpdateMethod::GetChannelList),
            _ => return Err(format_err!("Invalid Update Facade method: {}", method)),
        }
    }
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "ManagerState")]
enum ManagerStateDef {
    Idle = 0,
    CheckingForUpdates = 1,
    UpdateAvailable = 2,
    PerformingUpdate = 3,
    WaitingForReboot = 4,
    FinalizingUpdate = 5,
    EncounteredError = 6,
}

mod manager_state_wrapper {
    use super::*;
    use serde::{Deserialize, Deserializer, Serialize, Serializer};

    pub fn serialize<S: Serializer>(
        value: &Option<ManagerState>,
        serializer: S,
    ) -> Result<S::Ok, S::Error> {
        #[derive(Serialize)]
        struct Helper<'a>(#[serde(with = "ManagerStateDef")] &'a ManagerState);

        value.as_ref().map(Helper).serialize(serializer)
    }

    pub fn deserialize<'de, D: Deserializer<'de>>(
        deserializer: D,
    ) -> Result<Option<ManagerState>, D::Error> {
        #[derive(Deserialize)]
        struct Helper(#[serde(with = "ManagerStateDef")] ManagerState);

        let helper = Option::deserialize(deserializer)?;
        Ok(helper.map(|Helper(external)| external))
    }
}

#[derive(Serialize, Deserialize)]
#[serde(remote = "State")]
struct StateDef {
    #[serde(with = "manager_state_wrapper")]
    state: Option<ManagerState>,
    #[serde(skip_serializing_if = "Option::is_none")]
    version_available: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
pub struct StateHelper(#[serde(with = "StateDef")] pub State);

#[derive(Serialize, Deserialize)]
#[serde(remote = "CheckStartedResult")]
enum CheckStartedResultDef {
    Started = 0,
    InProgress = 1,
    Throttled = 2,
}

#[derive(Serialize, Deserialize, Debug, PartialEq)]
pub struct CheckNowResult {
    #[serde(with = "CheckStartedResultDef")]
    pub check_started: CheckStartedResult,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{json, to_value};

    #[test]
    fn serialize_state_idle() {
        assert_eq!(
            to_value(StateHelper(State {
                state: Some(ManagerState::Idle),
                version_available: None
            }))
            .unwrap(),
            json!({"state":"Idle"})
        );
    }

    #[test]
    fn serialize_state_performing_update() {
        assert_eq!(
            to_value(StateHelper(State {
                state: Some(ManagerState::PerformingUpdate),
                version_available: Some("1.0".to_string())
            }))
            .unwrap(),
            json!({"state":"PerformingUpdate","version_available":"1.0"})
        );
    }

    #[test]
    fn serialize_check_started_result() {
        assert_eq!(
            to_value(CheckNowResult { check_started: CheckStartedResult::Started }).unwrap(),
            json!({"check_started": "Started"})
        );
    }
}
