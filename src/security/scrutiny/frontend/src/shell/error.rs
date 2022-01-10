// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use thiserror::Error;

#[derive(Error, Debug)]
pub enum ShellError {
    #[error("expected command none provided")]
    EmptyCommand,
    #[error("unescaped json string: {}", provided)]
    UnescapedJsonString { provided: String },
}

impl ShellError {
    pub fn empty_command() -> ShellError {
        ShellError::EmptyCommand
    }

    pub fn unescaped_json_string(provided: impl Into<String>) -> ShellError {
        ShellError::UnescapedJsonString { provided: provided.into() }
    }
}
