// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use failure::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::Value;

use crate::setui::facade::SetUiFacade;
use crate::setui::types::SetUiMethod;

impl Facade for SetUiFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        setui_method_to_fidl(method, args, self).boxed_local()
    }
}

/// Takes JSON-RPC method command and forwards to corresponding SetUi FIDL method.
async fn setui_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &SetUiFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        SetUiMethod::Mutate => facade.mutate(args).await,
    }
}
