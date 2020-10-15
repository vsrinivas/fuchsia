// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

pub enum ComponentMethod {
    Launch,
    List,
    Search,
}

impl std::str::FromStr for ComponentMethod {
    type Err = anyhow::Error;
    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Launch" => Ok(ComponentMethod::Launch),
            "List" => Ok(ComponentMethod::List),
            "Search" => Ok(ComponentMethod::Search),
            _ => return Err(format_err!("Invalid Component Facade method: {}", method)),
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

#[derive(Deserialize, Debug)]
pub struct ComponentLaunchRequest {
    pub url: Option<String>,
    pub arguments: Option<Vec<String>>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum ComponentLaunchResponse {
    Success,
    Fail(i64),
}
