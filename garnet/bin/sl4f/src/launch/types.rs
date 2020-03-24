// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

pub enum LaunchMethod {
    Launch,
}

impl std::str::FromStr for LaunchMethod {
    type Err = anyhow::Error;
    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Launch" => Ok(LaunchMethod::Launch),
            _ => return Err(format_err!("Invalid Launch Facade method: {}", method)),
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct LaunchRequest {
    pub url: Option<String>,
    pub arguments: Option<Vec<String>>,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum LaunchResult {
    Success,
    Fail,
}
