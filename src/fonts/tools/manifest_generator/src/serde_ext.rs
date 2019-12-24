// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Useful utilities for Serde JSON deserialization.

use {
    serde::de::DeserializeOwned,
    serde_json,
    std::{
        fs, io,
        path::{Path, PathBuf},
    },
    thiserror::Error,
};

/// Loads and deserializes a JSON-serialized value of type `T` from a file path.
pub(crate) fn load_from_path<T: DeserializeOwned, P: AsRef<Path>>(path: P) -> Result<T, LoadError> {
    let path = path.as_ref();
    match fs::File::open(&path) {
        Ok(file) => {
            let reader = io::BufReader::new(file);
            load_from_reader(reader, path)
        }
        Err(err) => Err(LoadError::Io { path: path.into(), error: err }),
    }
}

/// Loads and deserializes a JSON-serialized value of type `T` from a `Read` implementer. The file
/// path is passed in for error context.
pub(crate) fn load_from_reader<T: DeserializeOwned, R: io::Read, P: AsRef<Path>>(
    reader: R,
    path: P,
) -> Result<T, LoadError> {
    match serde_json::from_reader(reader) {
        Ok(parsed) => Ok(parsed),
        Err(err) => Err(LoadError::Parse { path: path.as_ref().into(), error: err }),
    }
}

/// Serde JSON load/deserialization errors.
#[derive(Debug, Error)]
pub(crate) enum LoadError {
    #[error("File {:?} failed to load: {:?}", path, error)]
    Io { path: PathBuf, error: io::Error },

    #[error("File {:?} failed to parse: {:?}", path, error)]
    Parse { path: PathBuf, error: serde_json::Error },
}
