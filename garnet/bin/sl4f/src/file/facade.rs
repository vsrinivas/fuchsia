// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use base64;
use failure::Error;
use serde_json::{to_value, Value};
use std::fs::{create_dir, create_dir_all, remove_file, File};
use std::io::prelude::*;
use std::path::Path;

use crate::file::types::*;

/// Facade providing access to session testing interfaces.
#[derive(Debug)]
pub struct FileFacade {}

impl FileFacade {
    pub fn new() -> FileFacade {
        FileFacade {}
    }

    /// Deletes the given path, which must be a file. Returns OK(NotFound) if the file is alredy
    /// inexistent.
    pub async fn delete_file(&self, args: Value) -> Result<DeleteFileResult, Error> {
        let path = args.get("path").ok_or(format_err!("DeleteFile failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("DeleteFile failed, path not string"))?;

        if !Path::new(path).exists() {
            return Ok(DeleteFileResult::NotFound);
        }

        remove_file(path)?;
        Ok(DeleteFileResult::Success)
    }

    /// Creates a new directory. Returns OK(AlreadyExists) if the directory already exists.
    pub async fn make_dir(&self, args: Value) -> Result<MakeDirResult, Error> {
        let path = args.get("path").ok_or(format_err!("MakeDir failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("MakeDir failed, path not string"))?;
        let path = Path::new(path);

        let recurse = args["recurse"].as_bool().unwrap_or(false);

        if path.is_dir() {
            return Ok(MakeDirResult::AlreadyExists);
        }

        if recurse {
            create_dir_all(path)?;
        } else {
            create_dir(path)?;
        }
        Ok(MakeDirResult::Success)
    }

    /// Given a source file, fetches its contents.
    pub async fn read_file(&self, args: Value) -> Result<Value, Error> {
        let path = args.get("path").ok_or(format_err!("ReadFile failed, no path"))?;
        let path = path.as_str().ok_or(format_err!("ReadFile failed, path not string"))?;

        let mut file = File::open(path)?;
        let mut contents = Vec::new();
        file.read_to_end(&mut contents)?;

        let encoded_contents = base64::encode(&contents);

        Ok(to_value(encoded_contents)?)
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
