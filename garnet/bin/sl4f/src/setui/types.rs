// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//use serde::{Deserialize, Deserializer};
use anyhow::format_err;
use serde_derive::{Deserialize, Serialize};

/// Supported setUi commands.
pub enum SetUiMethod {
    Mutate,
}

impl std::str::FromStr for SetUiMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Mutate" => Ok(SetUiMethod::Mutate),
            _ => return Err(format_err!("invalid SetUi FIDL method: {}", method)),
        }
    }
}

#[derive(Serialize, Deserialize, Debug)]
pub enum SetUiResult {
    Success,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum LoginOverrideMode {
    None,
    AutologinGuest,
    AuthProvider,
}

/// Possible Mutate operations on Account. Right now only SET_LOGIN_OVERRIDE is supported.
#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum AccountOperation {
    SetLoginOverride,
}

/// Encapsulates a SettingType+Mutation. For now only ACCOUNT type is supported.
#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum JsonMutation {
    Account { operation: AccountOperation, login_override: LoginOverrideMode },
}
