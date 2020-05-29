// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::component_search::{facade::ComponentSearchFacade, types::ComponentSearchMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for ComponentSearchFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            ComponentSearchMethod::List => {
                let result = self.list()?;
                Ok(to_value(result)?)
            }
            ComponentSearchMethod::Search => {
                let result = self.search(args)?;
                Ok(to_value(result)?)
            }
        }
    }
}
