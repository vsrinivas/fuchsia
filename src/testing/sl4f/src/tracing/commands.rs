// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::tracing::{facade::TracingFacade, types::TracingMethod};
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

#[async_trait(?Send)]
impl Facade for TracingFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            TracingMethod::Initialize => self.initialize(args).await,
            TracingMethod::Start => self.start().await,
            TracingMethod::Stop => self.stop().await,
            TracingMethod::Terminate => self.terminate(args).await,
        }
    }
}
