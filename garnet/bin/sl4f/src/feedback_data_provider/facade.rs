// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    base64,
    fidl_fuchsia_feedback::{DataProviderMarker, GetBugreportParameters},
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

    pub async fn get_bugreport(&self) -> Result<serde_json::Value, Error> {
        let data_provider =
            connect_to_service::<DataProviderMarker>().context("connect to DataProvider")?;
        let params =
            GetBugreportParameters { collection_timeout_per_data: Some(2.minutes().into_nanos()) };
        let bugreport = data_provider.get_bugreport(params).await.context("get bugreport")?;
        match bugreport.bugreport {
            Some(attachment) => {
                let mut buf = vec![0; attachment.value.size as usize];
                attachment.value.vmo.read(&mut buf, 0).context("reading vmo")?;
                let result = base64::encode(&buf);
                return Ok(serde_json::json!({
                    "zip": result,
                }));
            }
            None => Err(format_err!("No zip file data in the bugreport response")),
        }
    }
}
