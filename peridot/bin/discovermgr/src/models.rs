// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Deserialization of Actions.

#[cfg(test)]
use std::io::BufReader;

use serde_derive::Deserialize;

#[derive(Deserialize, Debug)]
pub struct Action {
    name: String,
    parameters: Option<Vec<Parameter>>,
    display_info: Option<DisplayInfo>,
    web_fulfillment: Option<WebFulfillment>,
}

#[derive(Deserialize, Debug)]
pub struct ParameterMapping {
    name: String,
    parameter_property: String,
}

#[derive(Deserialize, Debug)]
pub struct Parameter {
    #[serde(rename = "type")]
    parameter_type: String,
    name: String,
}

#[derive(Deserialize, Debug)]
pub struct DisplayInfo {
    icon: Option<String>,
    parameter_mapping: Option<Vec<ParameterMapping>>,
    title: Option<String>,
    name: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct WebFulfillment {
    url_template: Option<String>,
    parameter_mapping: Option<Vec<ParameterMapping>>,
}

#[derive(Debug)]
pub enum ModelError {
    Io(std::io::Error),
    Serde(serde_json::error::Error),
}

impl std::convert::From<serde_json::error::Error> for ModelError {
    fn from(err: serde_json::error::Error) -> ModelError {
        ModelError::Serde(err)
    }
}

impl std::convert::From<std::io::Error> for ModelError {
    fn from(err: std::io::Error) -> ModelError {
        ModelError::Io(err)
    }
}

#[cfg(test)]
type Result<T> = std::result::Result<T, ModelError>;

#[cfg(test)]
/// Returns an vector of actions from a file containing json.
///
pub fn actions_from_assets(file_name: &str) -> Result<Vec<Action>> {
    let reader = BufReader::new(std::fs::File::open(file_name)?);
    let actions: Vec<Action> = serde_json::from_reader(reader)?;
    Ok(actions)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_from_assets() {
        let data =
            actions_from_assets("/pkgfs/packages/discovermgr_tests/0/data/test_actions.json")
                .unwrap();
        assert_eq!(data.len(), 2);
        assert_eq!(data[0].name, "PLAY_MUSIC");
        assert_eq!(data[1].name, "SHOW_WEATHER");
    }
}
