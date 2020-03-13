// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};
use crate::input::{facade::InputFacade, types::InputMethod};
use crate::server::Facade;

impl Facade for InputFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        input_method_to_fidl(method, args, self).boxed_local()
    }
}

/// Takes ACTS method command and executes corresponding Input FIDL methods.
async fn input_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &InputFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        InputMethod::Tap => {
            let result = facade.tap(args).await?;
            Ok(to_value(result)?)
        }
        InputMethod::Swipe => {
            let result = facade.swipe(args).await?;
            Ok(to_value(result)?)
        }
    }
}
