// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::basemgr::{facade::BaseManagerFacade, types::BaseManagerMethod};
use crate::server::Facade;
use failure::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

impl Facade for BaseManagerFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        base_manager_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes ACTS method command and executes corresponding Base Manager Client
// FIDL methods.
async fn base_manager_method_to_fidl(
    method_name: String,
    _args: Value,
    facade: &BaseManagerFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        BaseManagerMethod::RestartSession => {
            let result = facade.restart_session().await?;
            Ok(to_value(result)?)
        }
    }
}
