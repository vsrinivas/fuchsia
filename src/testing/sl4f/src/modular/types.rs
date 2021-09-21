// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use serde::{Deserialize, Serialize};

/// Enum for supported Modular commands.
pub enum ModularMethod {
    RestartSession,
    StartBasemgr,
    KillBasemgr,
    IsBasemgrRunning,
    LaunchMod,
}

impl std::str::FromStr for ModularMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "RestartSession" => Ok(ModularMethod::RestartSession),
            "StartBasemgr" => Ok(ModularMethod::StartBasemgr),
            "KillBasemgr" => Ok(ModularMethod::KillBasemgr),
            "IsBasemgrRunning" => Ok(ModularMethod::IsBasemgrRunning),
            "LaunchMod" => Ok(ModularMethod::LaunchMod),
            _ => return Err(format_err!("invalid ModularMethod: {}", method)),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum RestartSessionResult {
    Success,
    NoSessionToRestart,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum BasemgrResult {
    Success,
    Fail,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum KillBasemgrResult {
    Success,
    NoBasemgrToKill,
}

#[derive(Deserialize, Default, Debug)]
pub struct StartBasemgrRequest {
    /// A JSON object containing Modular configuration to be passed to basemgr.
    ///
    /// If provided, `config` overrides basemgr's default configuration.
    /// See [Launching Modular with custom configuration][config].
    ///
    /// If provided with `session_url`, the session will be instructed to
    /// launch sessionmgr via `fuchsia.modular.session.Launcher` with this config.
    ///
    /// [config]: https://fuchsia.dev/fuchsia-src/concepts/modular/guide/config?hl=en#launching_modular_with_custom_configuration
    pub config: Option<serde_json::Value>,

    /// `session_url` is an optional component URL for a session to launch
    /// instead of starting basemgr as a legacy component.
    pub session_url: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct LaunchModRequest {
    pub mod_url: Option<String>,
    pub mod_name: Option<String>,
    pub story_name: Option<String>,
}
