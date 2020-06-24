// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::component::{facade::ComponentFacade, types::ComponentMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for ComponentFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            ComponentMethod::List => {
                let result = self.list()?;
                Ok(to_value(result)?)
            }
            ComponentMethod::Search => {
                let result = self.search(args)?;
                Ok(to_value(result)?)
            }
            ComponentMethod::Launch => {
                let result = self.launch(args).await?;
                Ok(to_value(result)?)
            }
        }
    }
}
