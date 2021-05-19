// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow, serde_json, std::str::FromStr, thiserror::Error};

#[cfg(target_os = "fuchsia")]
use diagnostics_reader as reader;

#[derive(Error, Debug)]
pub enum Error {
    #[cfg(target_os = "fuchsia")]
    #[error("Error while fetching data: {0}")]
    Fetch(reader::Error),

    #[error("Invalid format: {0}")]
    InvalidFormat(String),

    #[error("Invalid arguments: {0}")]
    InvalidArguments(String),

    #[error("Failed formatting the command response: {0}")]
    InvalidCommandResponse(serde_json::Error),

    #[error("Failed parsing glob {0}: {1}")]
    ParsePath(String, anyhow::Error),

    #[error("Failed to list archive accessors on {0} {1}")]
    ListAccessors(String, anyhow::Error),

    #[error("Failed to list locations on {0} {1}")]
    ListLocations(String, anyhow::Error),

    #[error("Failed to find inspect data in location {0}: {1}")]
    ReadLocation(String, anyhow::Error),

    #[error("Failed to connect to archivst: {0}")]
    ConnectToArchivist(#[source] anyhow::Error),

    #[error("Unknown archive path")]
    UnknownArchivePath,

    #[error("IO error. Failed to {0}: {1}")]
    IOError(String, #[source] anyhow::Error),

    #[error("No running component was found whose URL contains the given string: {0}")]
    ManifestNotFound(String),
}

impl Error {
    pub fn invalid_format(format: impl Into<String>) -> Error {
        Error::InvalidFormat(format.into())
    }

    pub fn invalid_arguments(msg: impl Into<String>) -> Error {
        Error::InvalidArguments(msg.into())
    }

    pub fn io_error(msg: impl Into<String>, e: anyhow::Error) -> Error {
        Error::IOError(msg.into(), e)
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
