// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::basemgr::{facade::BaseManagerFacade, types::BaseManagerMethod};
use failure::Error;
use serde_json::{to_value, Value};
use std::sync::Arc;

// Takes ACTS method command and executes corresponding Base Manager Client
// FIDL methods.
pub async fn base_manager_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: Arc<BaseManagerFacade>,
) -> Result<Value, Error> {
    match method_name.parse()? {
        BaseManagerMethod::RestartSession => {
            let result = await!(facade.restart_session())?;
            Ok(to_value(result)?)
        }
    }
}
