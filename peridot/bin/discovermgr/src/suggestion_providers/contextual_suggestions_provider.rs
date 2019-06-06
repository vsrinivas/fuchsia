// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::{Action, AddMod, DisplayInfo, Intent, Suggestion},
        story_context_store::{ContextEntity, Contributor},
        suggestions_manager::SearchSuggestionsProvider,
    },
    failure::Error,
    futures::future::LocalFutureObj,
    itertools::Itertools,
    std::collections::{HashMap, HashSet},
};

pub struct ContextualSuggestionsProvider {
    actions: Vec<Action>,
}

impl ContextualSuggestionsProvider {
    pub fn new(actions: Vec<Action>) -> Self {
        ContextualSuggestionsProvider { actions }
    }
}

impl SearchSuggestionsProvider for ContextualSuggestionsProvider {
    fn request<'a>(
        &'a self,
        _query: &'a str,
        // TODO(miguelfrde): this should probably receive *only* the types and
        // (maybe) some opaque ID not the full ContextEntity.
        context: &'a Vec<&'a ContextEntity>,
    ) -> LocalFutureObj<'a, Result<Vec<Suggestion>, Error>> {
        LocalFutureObj::new(Box::new(async move {
            Ok(self
                .actions
                .iter()
                .filter_map(|action| ActionMatching::try_to_match(action.clone(), context))
                .flat_map(|matching| matching.suggestions())
                .collect::<Vec<Suggestion>>())
        }))
    }
}

#[derive(Debug, Clone)]
struct EntityMatching<'a> {
    context_entity: &'a ContextEntity,
    matching_type: String,
}

struct ActionMatching<'a> {
    action: Action,
    parameters: HashMap<String, Vec<EntityMatching<'a>>>,
}

impl<'a> ActionMatching<'a> {
    pub fn try_to_match(action: Action, context: &Vec<&'a ContextEntity>) -> Option<Self> {
        let mut parameters = HashMap::<String, Vec<EntityMatching>>::new();
        for parameter in &action.parameters {
            for context_entity in context {
                if let Some(ref t) =
                    context_entity.types.iter().find(|&t| *t == parameter.parameter_type)
                {
                    parameters.entry(parameter.name.to_string()).or_insert(vec![]).push(
                        EntityMatching {
                            context_entity: &context_entity,
                            matching_type: t.to_string(),
                        },
                    );
                }
            }
            if !parameters.contains_key(&parameter.name) {
                return None;
            }
        }
        Some(ActionMatching { action, parameters })
    }

    pub fn suggestions(self) -> impl Iterator<Item = Suggestion> + 'a {
        let action = self.action;
        all_matches(self.parameters).filter_map(move |parameters| {
            let mut intent = Intent::new().with_action(&action.name);
            if let Some(ref fulfillment) = action.fuchsia_fulfillment {
                intent = intent.with_handler(&fulfillment.component_url);
            }
            let references = parameters.iter().map(|(p, e)| (p, &e.context_entity.reference));
            let intent = references.into_iter().fold(intent, |intent, (param_name, reference)| {
                intent.add_parameter(&param_name, reference)
            });
            // TODO: make this cleaner.The matching could contain the
            // story id for example if we gurantee all entities are from the same
            // story.
            // TODO: the filling of the story should happen in the suggestions
            // manager, not here. Eventually we expect SuggestionProviders to not
            // live here, but in shells, agents, etc.
            let story_name = parameters.values().next().and_then(|first_param| {
                match first_param.context_entity.contributors.iter().next().unwrap() {
                    Contributor::ModuleContributor { story_id, .. } => Some(story_id.to_string()),
                }
            });
            let add_mod = AddMod::new(intent, story_name, None);
            filled_display_info(&action, &parameters)
                .map(|display_info| Suggestion::new(add_mod, display_info))
        })
    }
}

/// Computes the cartesion product of parameter matchings.
/// Example: {p1: [a], p2: [b, c], p3: [d, e]}
/// Results in: [{p1: a, p2: b, p3: d}, {p1: a, p2: b, p3: e},
///              {p1: a, p2: c, p3: d}, {p1: a, p2: c, p3: e}]
fn all_matches(
    matchings: HashMap<String, Vec<EntityMatching>>,
) -> impl Iterator<Item = HashMap<String, EntityMatching>> {
    // First transform |matchings| into an iterator of (key, value_i) iterators.
    // In the example above, this would be: [[(p1, a)], [(p2, b), (p2, c)], [(p3, d), (p3, e)]]
    let pairs =
        matchings.into_iter().map(|(p, refs)| refs.into_iter().map(move |r| (p.clone(), r)));
    // Now fetch the cartesian product of those pairs and transform each of
    // them into a map.
    pairs
        .multi_cartesian_product()
        .map(|x| x.into_iter().collect::<HashMap<_, _>>())
        // Filter out entries like {p1: e1, p2: e1} where two parameters are
        // fulfilled by the same entity.
        // This fixes cases like "directions from $a to $b" where a == b."
        .filter(|map| {
            map.values().map(|v| &v.context_entity.reference).collect::<HashSet<_>>().len()
                == map.len()
        })
    // TODO: ensure all entities returned as a single mapping belong to the same
    // story. This would be simplified if we didn't aggregate entities from
    // different stories...
}

fn filled_display_info(
    action: &Action,
    _parameters: &HashMap<String, EntityMatching>,
) -> Option<DisplayInfo> {
    // TODO: fill with parameters
    match action.action_display {
        Some(ref action_display) => action_display.display_info.clone(),
        None => None,
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, maplit::hashset};

    #[fasync::run_singlethreaded(test)]
    async fn get_suggestions() -> Result<(), Error> {
        let contextual_suggestions_provider =
            ContextualSuggestionsProvider::new(test_action_index());
        let context = test_context();
        let context_refs = context.iter().collect::<Vec<_>>();
        let results = await!(contextual_suggestions_provider.request("", &context_refs))?;

        let expected_results = vec![
            suggestion!(
                action = "SHOW_WEATHER",
                title = "Weather in $place",
                icon = "https://example.com/weather-icon",
                parameters = [(name = "Location", entity_reference = "foo-reference")],
                story = "story1"
            ),
            suggestion!(
                action = "SHOW_WEATHER",
                title = "Weather in $place",
                icon = "https://example.com/weather-icon",
                parameters = [(name = "Location", entity_reference = "bar-reference")],
                story = "story1"
            ),
            suggestion!(
                action = "SHOW_DIRECTIONS",
                title = "Directions from $origin to $destination",
                icon = "https://example.com/map-icon",
                parameters = [
                    (name = "origin", entity_reference = "foo-reference"),
                    (name = "destination", entity_reference = "bar-reference")
                ],
                story = "story1"
            ),
            suggestion!(
                action = "SHOW_DIRECTIONS",
                title = "Directions from $origin to $destination",
                icon = "https://example.com/map-icon",
                parameters = [
                    (name = "origin", entity_reference = "bar-reference"),
                    (name = "destination", entity_reference = "foo-reference")
                ],
                story = "story1"
            ),
        ];

        // Ensure all IDs are distinct.
        assert_eq!(results.len(), 4);
        assert_eq!(
            results.iter().map(|s| s.id().to_string()).collect::<HashSet<String>>().len(),
            4
        );

        // Ensure all results are what we expected
        assert!(results.into_iter().all(|actual| expected_results.iter().any(|expected| {
            actual.action().intent == expected.action().intent
                && actual.action().story_name() == expected.action().story_name()
                && actual.display_info() == expected.display_info()
        })));
        Ok(())
    }

    fn test_context() -> Vec<ContextEntity> {
        vec![
            ContextEntity::new_test(
                "foo-reference",
                hashset!("https://schema.org/Place".to_string()),
                hashset!(Contributor::module_new("story1", "mod-a", "param-foo")),
            ),
            ContextEntity::new_test(
                "baz-reference",
                hashset!("https://schema.org/Person".to_string()),
                hashset!(Contributor::module_new("story1", "mod-a", "param-baz")),
            ),
            ContextEntity::new_test(
                "bar-reference",
                hashset!("https://schema.org/Place".to_string()),
                hashset!(Contributor::module_new("story1", "mod-b", "param-bar")),
            ),
        ]
    }

    fn test_action_index() -> Vec<Action> {
        serde_json::from_str(include_str!("../../test_data/test_actions.json")).unwrap()
    }
}
