// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::scenic::{facade::ScenicFacade, types::ScenicMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

#[async_trait(?Send)]
impl Facade for ScenicFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        match method.parse()? {
            ScenicMethod::TakeScreenshot => self.take_screenshot().await,
        }
    }
}
