// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::traceutil::{facade::TraceutilFacade, types::TraceutilMethod};
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

#[async_trait(?Send)]
impl Facade for TraceutilFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            TraceutilMethod::GetTraceFile => self.get_trace_file(args).await,
        }
    }
}
