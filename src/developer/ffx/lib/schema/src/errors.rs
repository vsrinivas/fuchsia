// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::anyhow;

#[derive(Debug)]
pub enum Error {
    OverDepthLimit(usize),
    Error(anyhow::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

impl serde::de::Error for Error {
    fn custom<T: std::fmt::Display>(msg: T) -> Self {
        Error::Error(anyhow!("Failed to deserialize value: \"{}\"", msg))
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::OverDepthLimit(limit) => {
                write!(f, "Hit recursion limit of {} trying to resolve type", limit)
            }
            Error::Error(e) => write!(f, "{}", e),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Error::Error(e) => Some(e.root_cause()),
            _ => None,
        }
    }
}

impl From<anyhow::Error> for Error {
    fn from(err: anyhow::Error) -> Self {
        Self::Error(err)
    }
}
