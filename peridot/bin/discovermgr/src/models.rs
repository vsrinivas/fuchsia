// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Deserialization of Actions.
use {
    crate::story_context_store::ContextEntity,
    fidl_fuchsia_app_discover::Suggestion as FidlSuggestion,
    fidl_fuchsia_modular::{
        AddMod as FidlAddMod, DisplayInfo as FidlDisplayInfo, Intent as FidlIntent,
        IntentParameter as FidlIntentParameter, IntentParameterData as FidlIntentParameterData,
        SurfaceArrangement, SurfaceDependency, SurfaceRelation,
    },
    maplit::btreeset,
    serde_derive::Deserialize,
    std::collections::{BTreeSet, HashMap},
    uuid::Uuid,
};

#[derive(Clone, Deserialize, Debug)]
pub struct Action {
    pub name: String,
    #[serde(default)]
    pub parameters: Vec<Parameter>,
    pub action_display: Option<ActionDisplayInfo>,
    web_fulfillment: Option<WebFulfillment>,
    pub fuchsia_fulfillment: Option<FuchsiaFulfillment>,
}

#[derive(Clone, Deserialize, Debug)]
pub struct Parameter {
    #[serde(rename = "type")]
    pub parameter_type: String,
    pub name: String,
}

#[derive(Clone, Deserialize, Debug)]
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

#[derive(Clone, Deserialize, Debug)]
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
    pub parameters: BTreeSet<IntentParameter>,
}

#[derive(Debug, Clone, Eq, Hash, PartialEq, PartialOrd, Ord)]
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
}

impl IntentParameter {
    pub fn entity_reference(&self) -> &str {
        &self.entity_reference
    }
}

impl Action {
    pub async fn load_display_info<'a>(
        &'a self,
        parameters: HashMap<String, EntityMatching<'a>>,
    ) -> Option<DisplayInfo> {
        match self.action_display {
            None => None,
            Some(ref action_display) => await!(action_display.load_display_info(parameters)),
        }
    }
}

impl ActionDisplayInfo {
    pub async fn load_display_info<'a>(
        &'a self,
        parameters: HashMap<String, EntityMatching<'a>>,
    ) -> Option<DisplayInfo> {
        match self.display_info {
            None => None,
            Some(ref display_info) => Some(DisplayInfo {
                title: await!(self.interpolate(&display_info.title, &parameters)),
                subtitle: await!(self.interpolate(&display_info.subtitle, &parameters)),
                icon: await!(self.interpolate(&display_info.icon, &parameters)),
            }),
        }
    }

    async fn interpolate<'a>(
        &'a self,
        template: &'a Option<String>,
        parameters: &'a HashMap<String, EntityMatching<'a>>,
    ) -> Option<String> {
        match template {
            None => None,
            Some(ref template_str) => {
                let mut result = template_str.clone();
                for parameter_mapping in &self.parameter_mapping {
                    // Matches {name}. {{ is escaped {
                    let template_part = format!("{{{name}}}", name = parameter_mapping.name);
                    if !result.contains(&template_part)
                        || parameter_mapping.parameter_property.is_empty()
                    {
                        continue;
                    }
                    let mut parts = parameter_mapping.parameter_property.split('.');
                    let intent_param = parts.next().unwrap();
                    let subfield_path = parts.collect::<Vec<&str>>();
                    if let Some(matching) = parameters.get(intent_param) {
                        if let Some(data) = await!(matching.get_data(subfield_path)) {
                            result = result.replace(&template_part, &data);
                        }
                    }
                }
                Some(result)
            }
        }
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

    #[cfg(test)]
    pub fn with_subtitle(mut self, subtitle: &str) -> Self {
        self.subtitle = Some(subtitle.to_string());
        self
    }

    #[cfg(test)]
    pub fn with_icon(mut self, icon: &str) -> Self {
        self.icon = Some(icon.to_string());
        self
    }
}

#[derive(Debug, Clone)]
pub struct EntityMatching<'a> {
    pub context_entity: &'a ContextEntity,
    pub matching_type: String,
}

impl<'a> EntityMatching<'a> {
    async fn get_data(&'a self, path: Vec<&'a str>) -> Option<String> {
        await!(self.context_entity.get_string_data(path, &self.matching_type))
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
                parameters: self
                    .intent
                    .parameters
                    .into_iter()
                    .map(|p| {
                        if p.entity_reference == old {
                            IntentParameter { name: p.name, entity_reference: new.to_string() }
                        } else {
                            p
                        }
                    })
                    .collect::<BTreeSet<IntentParameter>>(),
            },
        }
    }
}

impl Intent {
    pub fn new() -> Self {
        Intent { handler: None, action: None, parameters: btreeset!() }
    }

    pub fn parameters(&self) -> &BTreeSet<IntentParameter> {
        &self.parameters
    }

    pub fn with_handler(mut self, handler: &str) -> Self {
        self.handler = Some(handler.to_string());
        self
    }

    pub fn with_action(mut self, action: &str) -> Self {
        self.action = Some(action.to_string());
        self
    }

    pub fn add_parameter(mut self, name: &str, entity_reference: &str) -> Self {
        self.parameters.insert(IntentParameter {
            name: name.to_string(),
            entity_reference: entity_reference.to_string(),
        });
        self
    }
}

#[derive(Clone, Deserialize, Debug)]
pub struct FuchsiaFulfillment {
    pub component_url: String,
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

impl Into<FidlIntent> for Intent {
    fn into(self) -> FidlIntent {
        FidlIntent {
            handler: self.handler,
            action: self.action,
            parameters: Some(
                self.parameters.into_iter().map(|p| p.into()).collect::<Vec<FidlIntentParameter>>(),
            ),
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
    use {
        super::*,
        crate::testing::{FakeEntityData, FakeEntityResolver},
        fidl_fuchsia_modular::{EntityMarker, EntityResolverMarker},
        fuchsia_async as fasync,
        futures::future::join_all,
        maplit::{hashmap, hashset},
        std::iter::FromIterator,
    };

    #[test]
    fn test_from_assets() {
        let data: Vec<Action> =
            serde_json::from_str(include_str!("../test_data/test_actions.json")).unwrap();
        assert_eq!(data.len(), 4);
        assert_eq!(data[0].name, "PLAY_MUSIC");
        assert_eq!(data[1].name, "SHOW_WEATHER");
        assert_eq!(data[2].name, "SHOW_DIRECTIONS");
        assert_eq!(data[3].name, "VIEW_COLLECTION");

        let fulfillment = data[3].fuchsia_fulfillment.as_ref().unwrap();
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
                intent: Intent { handler: None, action: None, parameters: btreeset!() },
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
            parameters: btreeset!(IntentParameter {
                name: "param_name".to_string(),
                entity_reference: "ref".to_string(),
            }),
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
            .zip(add_mod.intent.parameters.into_iter())
            .all(|(param_fidl, param)| {
                param_fidl.name.unwrap() == param.name
                    && param_fidl.data
                        == FidlIntentParameterData::EntityReference(param.entity_reference.clone())
            }));
    }

    #[test]
    fn replace_reference_in_parameters() {
        let mut params = vec![
            IntentParameter { name: "param_name".to_string(), entity_reference: "ref".to_string() },
            IntentParameter {
                name: "other_param".to_string(),
                entity_reference: "some-other-ref".to_string(),
            },
            IntentParameter {
                name: "another_param".to_string(),
                entity_reference: "ref".to_string(),
            },
        ];
        let mut intent = Intent {
            handler: Some("handler".to_string()),
            action: Some("action".to_string()),
            parameters: BTreeSet::from_iter(params.clone().into_iter()),
        };
        let add_mod = AddMod {
            story_name: "story_name".to_string(),
            mod_name: "mod_name".to_string(),
            intent: intent.clone(),
        };
        let add_mod = add_mod.replace_reference_in_parameters("ref", "new-ref");
        params[0].entity_reference = "new-ref".to_string();
        params[2].entity_reference = "new-ref".to_string();
        intent.parameters = BTreeSet::from_iter(params.into_iter());
        assert_eq!(add_mod.intent, intent);
    }

    #[fasync::run_singlethreaded(test)]
    async fn load_display_info() {
        let (entity_resolver, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver.register_entity(
            "foo",
            FakeEntityData::new(
                vec!["foo-type".into()],
                include_str!("../test_data/nested-field.json"),
            ),
        );
        fake_entity_resolver.register_entity(
            "bar",
            FakeEntityData::new(
                vec!["bar-type".into()],
                include_str!("../test_data/nested-field.json"),
            ),
        );
        fake_entity_resolver.spawn(request_stream);

        let futs = vec!["foo", "bar"].into_iter().map(|reference| {
            let (entity_proxy, server_end) =
                fidl::endpoints::create_proxy::<EntityMarker>().unwrap();
            entity_resolver.resolve_entity(reference, server_end).unwrap();
            ContextEntity::from_entity(entity_proxy, hashset!())
        });
        let entities =
            await!(join_all(futs)).into_iter().map(|e| e.unwrap()).collect::<Vec<ContextEntity>>();
        let parameters = hashmap!(
            "param1".to_string() => EntityMatching {
                context_entity: &entities[0],
                matching_type: "foo-type".to_string(),
            },
            "param2".to_string() => EntityMatching {
                context_entity: &entities[1],
                matching_type: "bar-type".to_string(),
            }
        );

        // Test successful case
        let display_info = DisplayInfo::new()
            .with_title("Hello {foo}")
            .with_subtitle("Bye {bar}")
            .with_icon("https://icon.com/{icon}");
        let parameter_mapping = vec![
            ParameterMapping {
                name: "foo".to_string(),
                parameter_property: "param1.a.b.c".to_string(),
            },
            ParameterMapping {
                name: "bar".to_string(),
                parameter_property: "param2.x.y".to_string(),
            },
            ParameterMapping {
                name: "icon".to_string(),
                parameter_property: "param1.a.b.c".to_string(),
            },
        ];
        let action_display = ActionDisplayInfo {
            display_info: Some(display_info.clone()),
            parameter_mapping: parameter_mapping.clone(),
        };
        assert_eq!(
            await!(action_display.load_display_info(parameters.clone())),
            Some(
                DisplayInfo::new()
                    .with_title("Hello 1")
                    .with_subtitle("Bye 2")
                    .with_icon("https://icon.com/1")
            )
        );

        // Without display info, nothing is returned.
        let action_display =
            ActionDisplayInfo { display_info: None, parameter_mapping: parameter_mapping.clone() };
        assert_eq!(await!(action_display.load_display_info(parameters.clone())), None);

        // Without parameter mapping, the same display info is returned
        let action_display = ActionDisplayInfo {
            display_info: Some(display_info.clone()),
            parameter_mapping: vec![],
        };
        assert_eq!(
            await!(action_display.load_display_info(parameters.clone())),
            Some(display_info.clone())
        );

        // If some field is invalid or missing, it will just not be filled.
        let parameter_mapping = vec![
            ParameterMapping {
                name: "foo".to_string(),
                parameter_property: "param1.a.b.c".to_string(),
            },
            ParameterMapping {
                name: "bar".to_string(),
                parameter_property: "param2.a.x".to_string(),
            },
        ];
        let action_display = ActionDisplayInfo {
            display_info: Some(display_info.clone()),
            parameter_mapping: parameter_mapping.clone(),
        };
        assert_eq!(
            await!(action_display.load_display_info(parameters)),
            Some(
                DisplayInfo::new()
                    .with_title("Hello 1")
                    .with_subtitle("Bye {bar}")
                    .with_icon("https://icon.com/{icon}")
            )
        );
    }
}
