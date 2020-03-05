// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Useful utilities for Serde JSON deserialization.

use {
    json5,
    serde::de::DeserializeOwned,
    std::{
        fs,
        path::{Path, PathBuf},
    },
    thiserror::Error,
};

/// Loads and deserializes a JSON-serialized value of type `T` from a file path.
pub fn load_from_path<T: DeserializeOwned, P: AsRef<Path>>(path: P) -> Result<T, LoadError> {
    let path = path.as_ref();
    let contents = fs::read_to_string(path)
        .map_err(|e| LoadError::Io { path: path.into(), error: e.into() })?;
    json5::from_str(&contents).map_err(|e| LoadError::Parse { path: path.into(), error: e.into() })
}

/// Serde JSON load/deserialization errors.
#[derive(Debug, Error)]
pub enum LoadError {
    #[error("File {:?} failed to load: {:?}", path, error)]
    Io {
        path: PathBuf,
        #[source]
        error: anyhow::Error,
    },

    #[error("File {:?} failed to parse: {:?}", path, error)]
    Parse {
        path: PathBuf,
        #[source]
        error: anyhow::Error,
    },
}
