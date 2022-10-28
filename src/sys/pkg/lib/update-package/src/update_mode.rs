// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around parsing the update-mode file.

use {
    fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    serde::{Deserialize, Serialize},
    std::str::FromStr,
    thiserror::Error,
};

/// An error encountered while parsing the update-mode file.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ParseUpdateModeError {
    #[error("while opening the file")]
    OpenFile(#[source] fuchsia_fs::node::OpenError),

    #[error("while reading the file")]
    ReadFile(#[source] fuchsia_fs::file::ReadError),

    #[error("while deserializing: '{0:?}'")]
    Deserialize(String, #[source] serde_json::Error),

    #[error("update mode not supported: '{0}'")]
    UpdateModeNotSupported(String),
}

/// Wrapper for deserializing the update-mode file.
#[derive(Clone, Debug, PartialEq, Eq, Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum UpdateModeFile {
    #[serde(rename = "1")]
    Version1 {
        // We make this a String and not an UpdateMode so that we can seperate
        // the unsupported mode errors from other json deserialization errors.
        // For example,this would be considered valid json with an unsupported mode:
        // { version: "1", content: { "mode" : "banana" } }
        #[serde(rename = "mode")]
        update_mode: String,
    },
}

/// Enum to describe the supported update modes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum UpdateMode {
    /// Follow the normal system update flow.
    Normal,
    /// Instead of the normal flow, write a recovery image and reboot into it.
    ForceRecovery,
}

impl std::default::Default for UpdateMode {
    fn default() -> Self {
        Self::Normal
    }
}

impl FromStr for UpdateMode {
    type Err = ParseUpdateModeError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "normal" => Ok(UpdateMode::Normal),
            "force-recovery" => Ok(UpdateMode::ForceRecovery),
            other => Err(ParseUpdateModeError::UpdateModeNotSupported(other.to_string())),
        }
    }
}

pub(crate) async fn update_mode(
    proxy: &fio::DirectoryProxy,
) -> Result<Option<UpdateMode>, ParseUpdateModeError> {
    // Open the update-mode file.
    let fopen_res =
        fuchsia_fs::directory::open_file(proxy, "update-mode", fio::OpenFlags::RIGHT_READABLE)
            .await;
    if let Err(fuchsia_fs::node::OpenError::OpenError(Status::NOT_FOUND)) = fopen_res {
        return Ok(None);
    }

    // Read the update-mode file.
    let contents =
        fuchsia_fs::file::read_to_string(&fopen_res.map_err(ParseUpdateModeError::OpenFile)?)
            .await
            .map_err(ParseUpdateModeError::ReadFile)?;

    // Parse the json string to extract UpdateMode.
    match serde_json::from_str(&contents)
        .map_err(|e| ParseUpdateModeError::Deserialize(contents, e))?
    {
        UpdateModeFile::Version1 { update_mode } => update_mode.parse().map(Some),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::TestUpdatePackage, assert_matches::assert_matches,
        fuchsia_async as fasync, proptest::prelude::*, serde_json::json,
    };

    fn valid_update_mode_json_value(mode: &str) -> serde_json::Value {
        json!({
            "version": "1",
            "content": {
                "mode": mode,
            },
        })
    }

    fn valid_update_mode_json_string(mode: &str) -> String {
        valid_update_mode_json_value(mode).to_string()
    }

    proptest! {
        #[test]
        fn test_json_serialize_roundtrip(s in ".+") {
            // Generate json and show that it successfully deserializes into the wrapper object.
            let starting_json_value = valid_update_mode_json_value(&s);
            let deserialized_object: UpdateModeFile =
                serde_json::from_value(starting_json_value.clone())
                    .expect("json to deserialize");
            assert_eq!(deserialized_object, UpdateModeFile::Version1 { update_mode: s });

            // Serialize back into serde_json::Value object & show we get same json we started with.
            // Note: even though serialize generally means "convert to string", in this case we're
            // serializing to a serde_json::Value to ignore ordering when we check equality.
            let final_json_value =
                serde_json::to_value(&deserialized_object)
                    .expect("serialize to value");
            assert_eq!(final_json_value, starting_json_value);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_update_mode_success_normal() {
        let p = TestUpdatePackage::new()
            .add_file("update-mode", &valid_update_mode_json_string("normal"))
            .await;
        assert_matches!(p.update_mode().await, Ok(Some(UpdateMode::Normal)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_update_mode_success_force_recovery() {
        let p = TestUpdatePackage::new()
            .add_file("update-mode", &valid_update_mode_json_string("force-recovery"))
            .await;
        assert_matches!(p.update_mode().await, Ok(Some(UpdateMode::ForceRecovery)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_update_mode_success_missing_update_mode_file() {
        let p = TestUpdatePackage::new();
        assert_matches!(p.update_mode().await, Ok(None));
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_update_mode_fail_unsupported_mode() {
        let p = TestUpdatePackage::new()
            .add_file("update-mode", &valid_update_mode_json_string("potato"))
            .await;
        assert_matches!(
            p.update_mode().await,
            Err(ParseUpdateModeError::UpdateModeNotSupported(mode)) if mode=="potato"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_update_mode_fail_deserialize() {
        let p = TestUpdatePackage::new().add_file("update-mode", "oh no! this isn't json.").await;
        assert_matches!(
            p.update_mode().await,
            Err(ParseUpdateModeError::Deserialize(s,_)) if s == "oh no! this isn't json."
        );
    }
}
