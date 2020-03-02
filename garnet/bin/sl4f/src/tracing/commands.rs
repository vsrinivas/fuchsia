// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::tracing::{facade::TracingFacade, types::TracingMethod};
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::Value;

impl Facade for TracingFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        tracing_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes SL4F method command and executes corresponding Tracing methods.
async fn tracing_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &TracingFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        TracingMethod::Initialize => facade.initialize(args).await,
        TracingMethod::Start => facade.start().await,
        TracingMethod::Stop => facade.stop().await,
        TracingMethod::Terminate => facade.terminate(args).await,
    }
}
