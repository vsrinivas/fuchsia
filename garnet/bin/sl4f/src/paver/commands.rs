// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{facade::PaverFacade, types::Method};
use failure::Error;
use serde_json::{from_value, to_value, Value};
use std::sync::Arc;

// Takes SL4F method command and executes corresponding file facade method.
pub async fn paver_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<PaverFacade>,
) -> Result<Value, Error> {
    handle_request(method_name.parse()?, args, &facade).await
}

async fn handle_request(method: Method, args: Value, facade: &PaverFacade) -> Result<Value, Error> {
    match method {
        Method::QueryActiveConfiguration => {
            let result = facade.query_active_configuration().await?;
            Ok(to_value(result)?)
        }
        Method::QueryConfigurationStatus => {
            let result = facade.query_configuration_status(from_value(args)?).await?;
            Ok(to_value(result)?)
        }
        Method::ReadAsset => {
            let result = facade.read_asset(from_value(args)?).await?;
            Ok(to_value(result)?)
        }
    }
}
