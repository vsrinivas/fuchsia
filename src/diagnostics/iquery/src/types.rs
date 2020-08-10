// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow, serde_json, std::str::FromStr, thiserror::Error};

#[derive(Error, Debug)]
pub enum Error {
    #[error("Error while fetching data: {}", _0)]
    Fetch(anyhow::Error),

    #[error("Invalid format: {}", _0)]
    InvalidFormat(String),

    #[error("Invalid arguments: {}", _0)]
    InvalidArguments(String),

    #[error("Failed formatting the command response: {}", _0)]
    InvalidCommandResponse(serde_json::Error),

    #[error("Failed parsing glob {}: {}", _0, _1)]
    ParsePath(String, anyhow::Error),

    #[error("Failed to list locations on {} {}", _0, _1)]
    ListLocations(String, anyhow::Error),

    #[error("Failed to find inspect data in location {}: {}", _0, _1)]
    ReadLocation(String, anyhow::Error),
}

impl Error {
    pub fn invalid_format(format: impl Into<String>) -> Error {
        Error::InvalidFormat(format.into())
    }

    pub fn invalid_arguments(msg: impl Into<String>) -> Error {
        Error::InvalidArguments(msg.into())
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Format {
    Text,
    Json,
}

impl FromStr for Format {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_ref() {
            "json" => Ok(Format::Json),
            "text" => Ok(Format::Text),
            f => Err(Error::invalid_format(f)),
        }
    }
}

pub trait ToText {
    fn to_text(self) -> String;
}

impl ToText for Vec<String> {
    fn to_text(self) -> String {
        self.join("\n")
    }
}

impl ToText for String {
    fn to_text(self) -> String {
        self
    }
}
