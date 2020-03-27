// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file::{facade::FileFacade, types::FileMethod};
use crate::server::Facade;
use anyhow::Error;
use async_trait::async_trait;
use serde_json::{to_value, Value};

#[async_trait(?Send)]
impl Facade for FileFacade {
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error> {
        match method.parse()? {
            FileMethod::DeleteFile => {
                let result = self.delete_file(args).await?;
                Ok(to_value(result)?)
            }
            FileMethod::MakeDir => {
                let result = self.make_dir(args).await?;
                Ok(to_value(result)?)
            }
            FileMethod::ReadFile => {
                let result = self.read_file(args).await?;
                Ok(to_value(result)?)
            }
            FileMethod::WriteFile => {
                let result = self.write_file(args).await?;
                Ok(to_value(result)?)
            }
            FileMethod::Stat => {
                let result = self.stat(args).await?;
                Ok(to_value(result)?)
            }
        }
    }
}
