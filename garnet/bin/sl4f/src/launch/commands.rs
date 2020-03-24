// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::launch::{facade::LaunchFacade, types::LaunchMethod};
use crate::server::Facade;
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

impl Facade for LaunchFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        launch_method_to_fidl(method, args, self).boxed_local()
    }
}

/// Takes ACTS method command and executes corresponding Launch FIDL methods.
async fn launch_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &LaunchFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        LaunchMethod::Launch => {
            let result = facade.launch(args).await?;
            Ok(to_value(result)?)
        }
    }
}
