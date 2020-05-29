// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

pub enum ComponentSearchMethod {
    List,
    Search,
}

impl std::str::FromStr for ComponentSearchMethod {
    type Err = anyhow::Error;
    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "List" => Ok(ComponentSearchMethod::List),
            "Search" => Ok(ComponentSearchMethod::Search),
            _ => return Err(format_err!("Invalid Component Search Facade method: {}", method)),
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct ComponentSearchRequest {
    pub name: Option<String>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ComponentSearchResult {
    Success,
    NotFound,
}
