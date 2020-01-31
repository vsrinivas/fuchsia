// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use serde_derive::{Deserialize, Serialize};

/// Enum for supported BaseManager commands.
pub enum BaseManagerMethod {
    RestartSession,
    StartBasemgr,
}

impl std::str::FromStr for BaseManagerMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "RestartSession" => Ok(BaseManagerMethod::RestartSession),
            "StartBasemgr" => Ok(BaseManagerMethod::StartBasemgr),
            _ => return Err(format_err!("invalid BaseManager Facade method: {}", method)),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum RestartSessionResult {
    Success,
    NoSessionToRestart,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum StartBasemgrResult {
    Success,
    Fail,
}
