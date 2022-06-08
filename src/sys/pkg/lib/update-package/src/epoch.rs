// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around parsing the epoch.json file.

use {epoch::EpochFile, fidl_fuchsia_io as fio, fuchsia_zircon_status::Status, thiserror::Error};

/// An error encountered while parsing the epoch.json file.
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum ParseEpochError {
    #[error("while opening the file")]
    OpenFile(#[source] fuchsia_fs::node::OpenError),

    #[error("while reading the file")]
    ReadFile(#[source] fuchsia_fs::file::ReadError),

    #[error("while deserializing: `{0:?}`")]
    Deserialize(String, #[source] serde_json::Error),
}

pub(crate) async fn epoch(proxy: &fio::DirectoryProxy) -> Result<Option<u64>, ParseEpochError> {
    // Open the epoch.json file.
    let fopen_res =
        fuchsia_fs::directory::open_file(proxy, "epoch.json", fio::OpenFlags::RIGHT_READABLE).await;
    if let Err(fuchsia_fs::node::OpenError::OpenError(Status::NOT_FOUND)) = fopen_res {
        return Ok(None);
    }

    // Read the epoch.json file.
    let contents = fuchsia_fs::file::read_to_string(&fopen_res.map_err(ParseEpochError::OpenFile)?)
        .await
        .map_err(ParseEpochError::ReadFile)?;

    // Parse the json string to extract the epoch.
    match serde_json::from_str(&contents).map_err(|e| ParseEpochError::Deserialize(contents, e))? {
        EpochFile::Version1 { epoch } => Ok(Some(epoch)),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::TestUpdatePackage, assert_matches::assert_matches,
        fuchsia_async as fasync, serde_json::json,
    };

    #[fasync::run_singlethreaded(test)]
    async fn parse_epoch_success() {
        let p = TestUpdatePackage::new()
            .add_file(
                "epoch.json",
                json!({
                    "version": "1",
                    "epoch": 3
                })
                .to_string(),
            )
            .await;
        assert_matches!(p.epoch().await, Ok(Some(3)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_epoch_success_missing_epoch_file() {
        let p = TestUpdatePackage::new();
        assert_matches!(p.epoch().await, Ok(None));
    }

    #[fasync::run_singlethreaded(test)]
    async fn parse_epoch_fail_deserialize() {
        let p = TestUpdatePackage::new().add_file("epoch.json", "oh no! this isn't json.").await;
        assert_matches!(
            p.epoch().await,
            Err(ParseEpochError::Deserialize(s,_)) if s == "oh no! this isn't json."
        );
    }
}
