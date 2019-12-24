// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::traceutil::{facade::TraceutilFacade, types::TraceutilMethod};
use anyhow::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::Value;

impl Facade for TraceutilFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        traceutil_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes SL4F method command and executes corresponding Traceutil methods.
async fn traceutil_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &TraceutilFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        TraceutilMethod::GetTraceFile => facade.get_trace_file(args).await,
    }
}
