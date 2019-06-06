// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Deserialization of Actions.
use {
    fidl_fuchsia_app_discover::Suggestion as FidlSuggestion,
    fidl_fuchsia_modular::{
        AddMod as FidlAddMod, DisplayInfo as FidlDisplayInfo, Intent as FidlIntent,
        IntentParameter as FidlIntentParameter, IntentParameterData as FidlIntentParameterData,
        SurfaceArrangement, SurfaceDependency, SurfaceRelation,
    },
    serde_derive::Deserialize,
    uuid::Uuid,
};

#[derive(Deserialize, Debug)]
pub struct Action {
    name: String,
    #[serde(default)]
    pub parameters: Vec<Parameter>,
    pub display_info: ActionDisplayInfo,
    web_fulfillment: Option<WebFulfillment>,
    fuchsia_fulfillment: Option<FuchsiaFulfillment>,
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
    #[serde(default)]
    pub parameter_mapping: Vec<ParameterMapping>,
}

#[derive(Clone, Deserialize, Debug, Eq, PartialEq)]
pub struct DisplayInfo {
    pub icon: Option<String>,
    pub title: Option<String>,
    pub subtitle: Option<String>,
}

#[derive(Clone, Deserialize, Debug)]
pub struct ParameterMapping {
    name: String,
    parameter_property: String,
}

#[derive(Deserialize, Debug)]
pub struct WebFulfillment {
    url_template: String,
    #[serde(default)]
    parameter_mapping: Vec<ParameterMapping>,
}

#[derive(Debug, Clone, Eq, PartialEq)]
// TODO: Suggestions at this point should contain ActionDisplayInfo
pub struct Suggestion {
    id: String,
    display_info: DisplayInfo,
    action: AddMod,
}

#[derive(Debug, Clone, Eq, Hash, PartialEq)]
pub struct AddMod {
    pub mod_name: String,
    story_name: String,
    pub intent: Intent,
}

#[derive(Debug, Clone, Eq, Hash, PartialEq)]
pub struct Intent {
    pub handler: Option<String>,
    pub action: Option<String>,
    pub parameters: Option<Vec<IntentParameter>>,
}

#[derive(Debug, Clone, Eq, Hash, PartialEq)]
pub struct IntentParameter {
    pub name: String,
    pub entity_reference: String,
}

impl Suggestion {
    pub fn new(action: AddMod, display_info: DisplayInfo) -> Self {
        Suggestion { id: Uuid::new_v4().to_string(), action, display_info }
    }

    pub fn action(&self) -> &AddMod {
        &self.action
    }

    pub fn id(&self) -> &str {
        &self.id
    }

    pub fn display_info(&self) -> &DisplayInfo {
        &self.display_info
    }

    // TODO: implement and test
    #[allow(dead_code)]
    fn filled_display_info(&self) -> FidlDisplayInfo {
        // TODO: this will fill the template from the data in the suggestion intent
        FidlDisplayInfo {
            title: self.display_info.title.clone(),
            subtitle: self.display_info.subtitle.clone(),
            icon: self.display_info.icon.clone(),
        }
    }
}

impl IntentParameter {
    pub fn entity_reference(&self) -> &str {
        &self.entity_reference
    }
}

impl DisplayInfo {
    pub fn new() -> Self {
        DisplayInfo { title: None, icon: None, subtitle: None }
    }

    #[cfg(test)]
    pub fn with_title(mut self, title: &str) -> Self {
        self.title = Some(title.to_string());
        self
    }
}

impl AddMod {
    pub fn new_raw(
        component_url: &str,
        story_name: Option<String>,
        mod_name: Option<String>,
    ) -> Self {
        AddMod {
            story_name: story_name.unwrap_or(Uuid::new_v4().to_string()),
            mod_name: mod_name.unwrap_or(Uuid::new_v4().to_string()),
            intent: Intent::new().with_handler(component_url),
        }
    }

    #[cfg(test)]
    pub fn new(intent: Intent, story_name: Option<String>, mod_name: Option<String>) -> Self {
        AddMod {
            story_name: story_name.unwrap_or(Uuid::new_v4().to_string()),
            mod_name: mod_name.unwrap_or(Uuid::new_v4().to_string()),
            intent: intent,
        }
    }

    pub fn story_name(&self) -> &str {
        &self.story_name
    }

    pub fn intent(&self) -> &Intent {
        &self.intent
    }

    pub fn replace_reference_in_parameters(self, old: &str, new: &str) -> Self {
        AddMod {
            story_name: self.story_name,
            mod_name: self.mod_name,
            intent: Intent {
                handler: self.intent.handler,
                action: self.intent.action,
                parameters: self.intent.parameters.map(|parameters| {
                    parameters
                        .into_iter()
                        .map(|p| {
                            if p.entity_reference == old {
                                IntentParameter { name: p.name, entity_reference: new.to_string() }
                            } else {
                                p
                            }
                        })
                        .collect::<Vec<IntentParameter>>()
                }),
            },
        }
    }
}

impl Intent {
    pub fn new() -> Self {
        Intent { handler: None, action: None, parameters: None }
    }

    pub fn parameters(&self) -> &Option<Vec<IntentParameter>> {
        &self.parameters
    }

    pub fn with_handler(mut self, handler: &str) -> Self {
        self.handler = Some(handler.to_string());
        self
    }

    #[cfg(test)]
    pub fn with_action(mut self, action: &str) -> Self {
        self.action = Some(action.to_string());
        self
    }

    #[cfg(test)]
    pub fn add_parameter(mut self, name: &str, entity_reference: &str) -> Self {
        self.parameters.get_or_insert(vec![]).push(IntentParameter {
            name: name.to_string(),
            entity_reference: entity_reference.to_string(),
        });
        self
    }
}

#[derive(Deserialize, Debug)]
pub struct FuchsiaFulfillment {
    component_url: String,
}

impl Into<FidlDisplayInfo> for DisplayInfo {
    fn into(self) -> FidlDisplayInfo {
        FidlDisplayInfo { title: self.title, subtitle: self.subtitle, icon: self.icon }
    }
}

impl Into<FidlSuggestion> for Suggestion {
    fn into(self) -> FidlSuggestion {
        let display_info = self.filled_display_info();
        FidlSuggestion { id: Some(self.id), display_info: Some(display_info) }
    }
}

impl Into<FidlIntent> for Intent {
    fn into(self) -> FidlIntent {
        FidlIntent {
            handler: self.handler,
            action: self.action,
            parameters: self.parameters.map(|params| {
                params.into_iter().map(|p| p.into()).collect::<Vec<FidlIntentParameter>>()
            }),
        }
    }
}

impl Into<FidlIntentParameter> for IntentParameter {
    fn into(self) -> FidlIntentParameter {
        FidlIntentParameter {
            name: Some(self.name),
            data: FidlIntentParameterData::EntityReference(self.entity_reference),
        }
    }
}

impl Into<FidlAddMod> for AddMod {
    fn into(self) -> FidlAddMod {
        FidlAddMod {
            mod_name: vec![],
            mod_name_transitional: Some(self.mod_name),
            intent: self.intent.into(),
            surface_parent_mod_name: None,
            surface_relation: SurfaceRelation {
                arrangement: SurfaceArrangement::None,
                dependency: SurfaceDependency::None,
                emphasis: 1.0,
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_assets() {
        let data: Vec<Action> =
            serde_json::from_str(include_str!("../test_data/test_actions.json")).unwrap();
        assert_eq!(data.len(), 3);
        assert_eq!(data[0].name, "PLAY_MUSIC");
        assert_eq!(data[1].name, "SHOW_WEATHER");
        assert_eq!(data[2].name, "VIEW_COLLECTION");

        let fulfillment = data[2].fuchsia_fulfillment.as_ref().unwrap();
        assert_eq!(
            fulfillment.component_url,
            "fuchsia-pkg://fuchsia.com/collections#meta/collections.cmx"
        );
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
            action: AddMod {
                mod_name: "mod_name".to_string(),
                intent: Intent { handler: None, action: None, parameters: None },
                story_name: "story_name".to_string(),
            },
        };

        let suggestion_fidl: FidlSuggestion = suggestion.clone().into();
        assert_eq!(suggestion_fidl.id, Some(suggestion.id));
        let display_info_fidl = suggestion_fidl.display_info.unwrap();
        assert_eq!(display_info_fidl.title, suggestion.display_info.title);
        assert_eq!(display_info_fidl.subtitle, suggestion.display_info.subtitle);
        assert_eq!(display_info_fidl.icon, suggestion.display_info.icon);
    }

    #[test]
    fn add_mod_into() {
        let intent = Intent {
            handler: Some("handler".to_string()),
            action: Some("action".to_string()),
            parameters: Some(vec![IntentParameter {
                name: "param_name".to_string(),
                entity_reference: "ref".to_string(),
            }]),
        };
        let add_mod = AddMod {
            story_name: "story_name".to_string(),
            mod_name: "mod_name".to_string(),
            intent: intent,
        };
        let add_mod_fidl: FidlAddMod = add_mod.clone().into();

        assert_eq!(add_mod_fidl.mod_name_transitional, Some(add_mod.mod_name));
        assert_eq!(add_mod_fidl.intent.handler, add_mod.intent.handler);
        assert_eq!(add_mod_fidl.intent.action, add_mod.intent.action);
        assert!(add_mod_fidl
            .intent
            .parameters
            .unwrap()
            .into_iter()
            .zip(add_mod.intent.parameters.unwrap().into_iter())
            .all(|(param_fidl, param)| {
                param_fidl.name.unwrap() == param.name
                    && param_fidl.data
                        == FidlIntentParameterData::EntityReference(param.entity_reference.clone())
            }));
    }

    #[test]
    fn replace_reference_in_parameters() {
        let mut intent = Intent {
            handler: Some("handler".to_string()),
            action: Some("action".to_string()),
            parameters: Some(vec![
                IntentParameter {
                    name: "param_name".to_string(),
                    entity_reference: "ref".to_string(),
                },
                IntentParameter {
                    name: "other_param".to_string(),
                    entity_reference: "some-other-ref".to_string(),
                },
                IntentParameter {
                    name: "another_param".to_string(),
                    entity_reference: "ref".to_string(),
                },
            ]),
        };
        let add_mod = AddMod {
            story_name: "story_name".to_string(),
            mod_name: "mod_name".to_string(),
            intent: intent.clone(),
        };
        let add_mod = add_mod.replace_reference_in_parameters("ref", "new-ref");
        intent.parameters.as_mut().map(|params| params[0].entity_reference = "new-ref".to_string());
        intent.parameters.as_mut().map(|params| params[2].entity_reference = "new-ref".to_string());
        assert_eq!(add_mod.intent, intent);
    }
}
