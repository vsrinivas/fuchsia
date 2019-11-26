// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::scenic::{
    facade::ScenicFacade,
    types::{PresentViewRequest, ScenicMethod},
};
use crate::server::Facade;
use failure::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

impl Facade for ScenicFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        scenic_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes ACTS method command and executes corresponding Scenic Client
// FIDL methods.
async fn scenic_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &ScenicFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        ScenicMethod::TakeScreenshot => facade.take_screenshot().await,
        ScenicMethod::PresentView => {
            let request: PresentViewRequest = serde_json::from_value(args)?;
            Ok(to_value(facade.present_view(request.url, request.config).await?)?)
        }
    }
}
