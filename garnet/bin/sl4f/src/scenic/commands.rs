// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::scenic::{
    facade::ScenicFacade,
    types::{PresentViewRequest, ScenicMethod},
};
use failure::Error;
use serde_json::{to_value, Value};
use std::sync::Arc;

// Takes ACTS method command and executes corresponding Scenic Client
// FIDL methods.
pub async fn scenic_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<ScenicFacade>,
) -> Result<Value, Error> {
    match method_name.parse()? {
        ScenicMethod::TakeScreenshot => await!(facade.take_screenshot()),
        ScenicMethod::PresentView => {
            let request: PresentViewRequest = serde_json::from_value(args)?;
            Ok(to_value(await!(facade.present_view(request.url, request.config))?)?)
        }
    }
}
