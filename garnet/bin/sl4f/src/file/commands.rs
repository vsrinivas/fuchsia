// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file::{facade::FileFacade, types::FileMethod};
use crate::server::Facade;
use failure::Error;
use futures::future::{FutureExt, LocalBoxFuture};
use serde_json::{to_value, Value};

impl Facade for FileFacade {
    fn handle_request(
        &self,
        method: String,
        args: Value,
    ) -> LocalBoxFuture<'_, Result<Value, Error>> {
        file_method_to_fidl(method, args, self).boxed_local()
    }
}

// Takes SL4F method command and executes corresponding file facade method.
async fn file_method_to_fidl(
    method_name: String,
    args: Value,
    facade: &FileFacade,
) -> Result<Value, Error> {
    match method_name.parse()? {
        FileMethod::DeleteFile => {
            let result = facade.delete_file(args).await?;
            Ok(to_value(result)?)
        }
        FileMethod::MakeDir => {
            let result = facade.make_dir(args).await?;
            Ok(to_value(result)?)
        }
        FileMethod::ReadFile => {
            let result = facade.read_file(args).await?;
            Ok(to_value(result)?)
        }
        FileMethod::WriteFile => {
            let result = facade.write_file(args).await?;
            Ok(to_value(result)?)
        }
        FileMethod::Stat => {
            let result = facade.stat(args).await?;
            Ok(to_value(result)?)
        }
    }
}
