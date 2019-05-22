// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Deserialization of Actions.
use serde_derive::Deserialize;

#[derive(Deserialize, Debug)]
pub struct Action {
    name: String,
    pub parameters: Vec<Parameter>,
    pub display_info: Option<DisplayInfo>,
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
    pub parameter_type: String,
    pub name: String,
}

#[derive(Deserialize, Debug)]
pub struct DisplayInfo {
    icon: Option<String>,
    parameter_mapping: Vec<ParameterMapping>,
    pub title: Option<String>,
    name: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct WebFulfillment {
    url_template: Option<String>,
    parameter_mapping: Vec<ParameterMapping>,
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
mod tests {
    use super::*;
    #[test]
    fn test_from_assets() {
        let data: Vec<Action> =
            serde_json::from_str(include_str!("../test_data/test_actions.json")).unwrap();
        print!("data[0] is {:?}", data[0]);
        assert_eq!(data.len(), 2);
        assert_eq!(data[0].name, "PLAY_MUSIC");
        assert_eq!(data[1].name, "SHOW_WEATHER");
    }
}
