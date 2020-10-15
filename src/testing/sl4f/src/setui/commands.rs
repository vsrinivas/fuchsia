// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::Value;

use crate::setui::facade::SetUiFacade;
use crate::setui::types::SetUiMethod;

#[async_trait(?Send)]
impl Facade for SetUiFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            SetUiMethod::SetNetwork => self.set_network(args).await,
            SetUiMethod::GetNetwork => self.get_network_setting().await,
            SetUiMethod::SetIntl => self.set_intl_setting(args).await,
            SetUiMethod::GetIntl => self.get_intl_setting().await,
        }
    }
}
