// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::server::Facade;
use anyhow::{format_err, Error};
use async_trait::async_trait;
use serde_json::{to_value, Value};

use crate::proxy::facade::ProxyFacade;

#[async_trait(?Send)]
impl Facade for ProxyFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.as_ref() {
            "OpenProxy" => {
                let target_port: u16 = serde_json::from_value(args)?;
                let open_port = self.open_proxy(target_port).await?;
                Ok(to_value(open_port)?)
            }
            "DropProxy" => {
                let target_port: u16 = serde_json::from_value(args)?;
                self.drop_proxy(target_port);
                Ok(to_value(())?)
            }
            "StopAllProxies" => {
                self.stop_all_proxies();
                Ok(to_value(())?)
            }
            _ => return Err(format_err!("Invalid proxy facade method: {:?}", method)),
        }
    }
}
