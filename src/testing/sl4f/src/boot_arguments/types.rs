// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;

/// Enum for supported arguments related commands.
#[derive(Debug)]
pub(super) enum Method {
    GetString,
}

impl std::str::FromStr for Method {
    type Err = Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "GetString" => Ok(Method::GetString),
            _ => return Err(format_err!("Invalid arguments facade method: {}", method)),
        }
    }
}
