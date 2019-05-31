// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Deserialization of Actions.
use {
    fidl_fuchsia_app_discover::{Suggestion as FidlSuggestion},
    fidl_fuchsia_modular::{DisplayInfo as FidlDisplayInfo},
    serde_derive::Deserialize,
};

#[derive(Deserialize, Debug)]
pub struct Action {
    name: String,
    pub parameters: Vec<Parameter>,
    pub display_info: ActionDisplayInfo,
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
pub struct ActionDisplayInfo {
    pub display_info: Option<DisplayInfo>,
    parameter_mapping: Option<Vec<ParameterMapping>>,
}

#[derive(Clone, Deserialize, Debug)]
pub struct DisplayInfo {
    pub icon: Option<String>,
    pub title: Option<String>,
    pub subtitle: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct WebFulfillment {
    url_template: Option<String>,
    parameter_mapping: Vec<ParameterMapping>,
}

#[derive(Clone)]
pub struct Suggestion {
    id: String,
    display_info: DisplayInfo,
}

impl Into<FidlDisplayInfo> for DisplayInfo {
    fn into(self) -> FidlDisplayInfo {
        FidlDisplayInfo { title: self.title, subtitle: self.subtitle, icon: self.icon }
    }
}

impl Into<FidlSuggestion> for Suggestion {
    fn into(self) -> FidlSuggestion {
        FidlSuggestion { id: Some(self.id), display_info: Some(self.display_info.into()) }
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

    #[test]
    fn display_info_into() {
        let display_info = DisplayInfo {
            title: Some("title".to_string()),
            subtitle: Some("subtitle".to_string()),
            icon: Some("http://example.com/icon.png".to_string()),
        };
        let display_info_fidl: FidlDisplayInfo = display_info.clone().into();
        assert_eq!(display_info_fidl.title, display_info.title);
        assert_eq!(display_info_fidl.subtitle, display_info.subtitle);
        assert_eq!(display_info_fidl.icon, display_info.icon);
    }

    #[test]
    fn suggestion_into() {
        let suggestion = Suggestion {
            id: "123".to_string(),
            display_info: DisplayInfo {
                title: Some("suggestion title".to_string()),
                icon: None,
                subtitle: Some("suggestion subtitle".to_string()),
            },
        };

        let suggestion_fidl: FidlSuggestion = suggestion.clone().into();
        assert_eq!(suggestion_fidl.id, Some(suggestion.id));
        let display_info_fidl = suggestion_fidl.display_info.unwrap();
        assert_eq!(display_info_fidl.title, suggestion.display_info.title);
        assert_eq!(display_info_fidl.subtitle, suggestion.display_info.subtitle);
        assert_eq!(display_info_fidl.icon, suggestion.display_info.icon);
    }
}
