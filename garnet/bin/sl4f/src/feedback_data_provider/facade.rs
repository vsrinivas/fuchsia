// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    base64,
    fidl_fuchsia_feedback::{DataProviderMarker, GetSnapshotParameters},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    serde_json,
};

/// Facade providing access to feedback interface.
#[derive(Debug)]
pub struct FeedbackDataProviderFacade {}

impl FeedbackDataProviderFacade {
    pub fn new() -> FeedbackDataProviderFacade {
        FeedbackDataProviderFacade {}
    }

    pub async fn get_snapshot(&self) -> Result<serde_json::Value, Error> {
        let data_provider =
            connect_to_service::<DataProviderMarker>().context("connect to DataProvider")?;
        let params =
            GetSnapshotParameters { collection_timeout_per_data: Some(2.minutes().into_nanos()) };
        let snapshot = data_provider.get_snapshot(params).await.context("get snapshot")?;
        match snapshot.archive {
            Some(archive) => {
                let mut buf = vec![0; archive.value.size as usize];
                archive.value.vmo.read(&mut buf, 0).context("reading vmo")?;
                let result = base64::encode(&buf);
                return Ok(serde_json::json!({
                    "zip": result,
                }));
            }
            None => Err(format_err!("No zip file data in the snapshot response")),
        }
    }
}
