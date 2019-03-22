// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::de::Error;
use serde::{Deserialize, Deserializer};
use serde_derive::{Deserialize, Serialize};

use fidl_fuchsia_auth::UserProfileInfo;

#[derive(Serialize, Deserialize, Debug)]
pub enum InjectAuthTokenResult {
    NotReady,
    Success,
}

#[derive(Deserialize)]
#[serde(remote = "UserProfileInfo")]
pub struct UserProfileInfoDef {
    #[serde(deserialize_with = "string_ensure_nonempty")]
    pub id: String,
    pub display_name: Option<String>,
    pub url: Option<String>,
    pub image_url: Option<String>,
}

#[derive(Deserialize)]
pub struct InjectAuthTokenRequest {
    #[serde(default, deserialize_with = "option_user_profile_info")]
    pub user_profile_info: Option<UserProfileInfo>,
    #[serde(deserialize_with = "string_ensure_nonempty")]
    pub credential: String,
}

// https://github.com/serde-rs/serde/issues/723
fn option_user_profile_info<'de, D>(deserializer: D) -> Result<Option<UserProfileInfo>, D::Error>
where
    D: Deserializer<'de>,
{
    #[derive(Deserialize)]
    struct Wrapper(#[serde(with = "UserProfileInfoDef")] UserProfileInfo);

    let v = Option::deserialize(deserializer)?;
    Ok(v.map(|Wrapper(a)| a))
}

fn string_ensure_nonempty<'de, D>(deserializer: D) -> Result<String, D::Error>
where
    D: Deserializer<'de>,
{
    let string = String::deserialize(deserializer)?;
    if string.is_empty() {
        Err(Error::custom("Empty string not allowed in InjectAuthTokenRequest"))
    } else {
        Ok(string)
    }
}
