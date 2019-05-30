// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use base64;
use failure::Error;
use serde_json::Value;
use std::fs::File;
use std::io::prelude::*;

use crate::file::types::WriteFileResult;

/// Facade providing access to session testing interfaces.
#[derive(Debug)]
pub struct FileFacade {}

impl FileFacade {
    pub fn new() -> FileFacade {
        FileFacade {}
    }

    /// Given data and the destination, it creates a new file and
    /// puts it in the corresponding path (given by the destination).
    pub async fn write_file(&self, args: Value) -> Result<WriteFileResult, Error> {
        let data = args.get("data").ok_or(format_err!("WriteFile failed, no data"))?;
        let data = data.as_str().ok_or(format_err!("WriteFile failed, data not string"))?;

        let contents = base64::decode(data)?;

        let destination =
            args.get("dst").ok_or(format_err!("WriteFile failed, no destination path given"))?;
        let destination =
            destination.as_str().ok_or(format_err!("WriteFile failed, destination not string"))?;

        let mut file = File::create(destination)?;
        file.write_all(&contents)?;
        Ok(WriteFileResult::Success)
    }
}
