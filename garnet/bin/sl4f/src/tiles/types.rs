// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use serde::{Deserialize, Serialize};

/// Supported Tiles commands.
pub enum TilesMethod {
    Start,
    Stop,
    List,
    Remove,
    AddFromUrl,
}

impl std::str::FromStr for TilesMethod {
    type Err = anyhow::Error;

    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Start" => Ok(TilesMethod::Start),
            "Stop" => Ok(TilesMethod::Stop),
            "List" => Ok(TilesMethod::List),
            "Remove" => Ok(TilesMethod::Remove),
            "AddFromUrl" => Ok(TilesMethod::AddFromUrl),
            _ => return Err(format_err!("Invalid Tiles Facade SL4F method: {}", method)),
        }
    }
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub struct AddTileInput {
    pub url: String,
    pub allow_focus: Option<bool>,
    pub args: Option<Vec<String>>,
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub struct RemoveTileInput {
    pub key: String,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum TileOutput {
    Success,
}

#[derive(Serialize, Debug)]
#[serde(rename_all = "snake_case")]
pub struct AddTileOutput {
    pub key: u32,
}

impl AddTileOutput {
    pub fn new(key: u32) -> AddTileOutput {
        AddTileOutput { key: key }
    }
}

#[derive(Serialize, Debug)]
#[serde(rename_all = "snake_case")]
pub struct ListTileOutput {
    pub keys: Vec<u32>,
    pub urls: Vec<String>,
    pub focus: Vec<bool>,
}

impl ListTileOutput {
    pub fn new(keys: &Vec<u32>, urls: &Vec<String>, focus: &Vec<bool>) -> ListTileOutput {
        ListTileOutput { keys: keys.to_vec(), urls: urls.to_vec(), focus: focus.to_vec() }
    }
}
