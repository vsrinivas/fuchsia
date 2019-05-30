// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::file::{facade::FileFacade, types::FileMethod};
use failure::Error;
use serde_json::{to_value, Value};
use std::sync::Arc;

// Takes SL4F method command and executes corresponding file facade method.
pub async fn file_method_to_fidl(
    method_name: String,
    args: Value,
    facade: Arc<FileFacade>,
) -> Result<Value, Error> {
    match method_name.parse()? {
        FileMethod::WriteFile => {
            let result = await!(facade.write_file(args))?;
            Ok(to_value(result)?)
        }
    }
}
