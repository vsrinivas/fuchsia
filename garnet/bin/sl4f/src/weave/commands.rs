// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use crate::weave::{facade::WeaveFacade, types::WeaveMethod};
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for WeaveFacade {
    async fn handle_request(&self, method: String, _args: Value) -> Result<Value, Error> {
        Ok(match method.parse()? {
            WeaveMethod::GetPairingCode => to_value(self.get_pairing_code().await?),
            WeaveMethod::GetQrCode => to_value(self.get_qr_code().await?),
            WeaveMethod::GetPairingState => to_value(self.get_pairing_state().await?),
        }?)
    }
}
