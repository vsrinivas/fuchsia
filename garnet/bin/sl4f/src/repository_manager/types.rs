// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use serde::{Deserialize, Serialize};

/// Supported RepositoryManagerFacade commands.
pub enum RepositoryManagerMethod {
    Add,
    List,
}

impl std::str::FromStr for RepositoryManagerMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Add" => Ok(RepositoryManagerMethod::Add),
            "List" => Ok(RepositoryManagerMethod::List),
            _ => return Err(format_err!("Invalid RepositoryManager Facade method: {}", method)),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum RepositoryOutput {
    Success,
}
