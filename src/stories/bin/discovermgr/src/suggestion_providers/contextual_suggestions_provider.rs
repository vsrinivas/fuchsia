// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::{Action, AddModInfo, EntityMatching, Intent, Suggestion},
        story_context_store::{ContextEntity, Contributor},
        suggestions_manager::SearchSuggestionsProvider,
    },
    anyhow::Error,
    futures::future::{join_all, LocalFutureObj},
    itertools::Itertools,
    std::collections::{HashMap, HashSet},
    std::sync::Arc,
};

pub struct ContextualSuggestionsProvider {
    actions: Arc<Vec<Action>>,
}

impl ContextualSuggestionsProvider {
    pub fn new(actions: Arc<Vec<Action>>) -> Self {
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
            let futs = self
                .actions
                .iter()
                .filter_map(|action| ActionMatching::try_to_match(action.clone(), context))
                .map(|matching| matching.suggestions());
            let result = join_all(futs)
                .await
                .into_iter()
                .flat_map(|suggestions| suggestions.into_iter())
                .collect::<Vec<Suggestion>>();
            Ok(result)
        }))
    }
}

struct ActionMatching<'a> {
    action: Action,
    parameters: HashMap<String, Vec<EntityMatching<'a>>>,
}

impl<'a> ActionMatching<'a> {
    pub fn try_to_match(action: Action, context: &Vec<&'a ContextEntity>) -> Option<Self> {
        let mut parameters = HashMap::<String, Vec<EntityMatching<'_>>>::new();
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

    pub async fn suggestions(self) -> impl Iterator<Item = Suggestion> + 'a {
        let action = &self.action;
        let futs = all_matches(self.parameters).map(|parameters| {
            async move {
                let mut intent = Intent::new().with_action(&action.name);
                if let Some(ref fulfillment) = action.fuchsia_fulfillment {
                    intent = intent.with_handler(&fulfillment.component_url);
                }
                let references = parameters.iter().map(|(p, e)| (p, &e.context_entity.reference));
                let intent =
                    references.into_iter().fold(intent, |intent, (param_name, reference)| {
                        intent.add_parameter(&param_name, reference)
                    });
                // TODO: make this cleaner.The matching could contain the
                // story id for example if we gurantee all entities are from the same
                // story.
                // TODO: the filling of the story should happen in the suggestions
                // manager, not here. Eventually we expect SuggestionProviders to not
                // live here, but in shells, agents, etc.
                let story_name =
                    parameters.values().next().and_then(|first_param| {
                        match first_param.context_entity.contributors.iter().next().unwrap() {
                            Contributor::ModuleContributor { story_id, .. } => {
                                Some(story_id.to_string())
                            }
                        }
                    });
                let add_mod = AddModInfo::new(intent, story_name, None);
                action
                    .load_display_info(parameters)
                    .await
                    .map(|display_info| Suggestion::new(add_mod, display_info))
            }
        });
        join_all(futs).await.into_iter().filter_map(|result| result)
    }
}

/// Computes the cartesion product of parameter matchings.
/// Example: {p1: [a], p2: [b, c], p3: [d, e]}
/// Results in: [{p1: a, p2: b, p3: d}, {p1: a, p2: b, p3: e},
///              {p1: a, p2: c, p3: d}, {p1: a, p2: c, p3: e}]
fn all_matches(
    matchings: HashMap<String, Vec<EntityMatching<'_>>>,
) -> impl Iterator<Item = HashMap<String, EntityMatching<'_>>> {
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{DisplayInfo, SuggestedAction},
            testing::{FakeEntityData, FakeEntityResolver},
        },
        fidl_fuchsia_modular::{EntityMarker, EntityResolverMarker},
        fuchsia_async as fasync,
        maplit::hashset,
    };

    #[fasync::run_singlethreaded(test)]
    async fn get_suggestions() -> Result<(), Error> {
        let actions = test_action_index();
        let contextual_suggestions_provider = ContextualSuggestionsProvider::new(actions.clone());
        let context = test_context().await?;
        let context_refs = context.iter().collect::<Vec<_>>();
        let results = contextual_suggestions_provider.request("", &context_refs).await?;

        let expected_results = vec![
            suggestion!(
                action = "SHOW_WEATHER",
                title = "Weather in San Francisco",
                icon = "https://example.com/weather-icon",
                parameters = [(name = "Location", entity_reference = "foo-reference")],
                story = "story1"
            ),
            suggestion!(
                action = "SHOW_WEATHER",
                title = "Weather in Mountain View",
                icon = "https://example.com/weather-icon",
                parameters = [(name = "Location", entity_reference = "bar-reference")],
                story = "story1"
            ),
            suggestion!(
                action = "SHOW_DIRECTIONS",
                title = "Directions from San Francisco to Mountain View",
                icon = "https://example.com/map-icon",
                parameters = [
                    (name = "origin", entity_reference = "foo-reference"),
                    (name = "destination", entity_reference = "bar-reference")
                ],
                story = "story1"
            ),
            suggestion!(
                action = "SHOW_DIRECTIONS",
                title = "Directions from Mountain View to San Francisco",
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
            match actual.action() {
                SuggestedAction::AddMod(actual_action) => match expected.action() {
                    SuggestedAction::AddMod(expected_action) => {
                        actual_action.intent == expected_action.intent
                            && actual_action.story_name() == expected_action.story_name()
                            && actual.display_info() == expected.display_info()
                    }
                    SuggestedAction::RestoreStory(_) => false,
                },
                SuggestedAction::RestoreStory(_) => false,
            }
        })));
        Ok(())
    }

    // Creates a test context and spawns the fake entity resolver.
    async fn test_context() -> Result<Vec<ContextEntity>, Error> {
        let (entity_resolver, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver.register_entity(
            "foo-reference",
            FakeEntityData::new(
                vec!["https://schema.org/Place".into()],
                include_str!("../../test_data/place1.json"),
            ),
        );
        fake_entity_resolver.register_entity(
            "baz-reference",
            FakeEntityData::new(
                vec!["https://schema.org/Person".into()],
                include_str!("../../test_data/person.json"),
            ),
        );
        fake_entity_resolver.register_entity(
            "bar-reference",
            FakeEntityData::new(
                vec!["https://schema.org/Place".into()],
                include_str!("../../test_data/place2.json"),
            ),
        );
        fake_entity_resolver.spawn(request_stream);

        let (entity_proxy1, server_end) = fidl::endpoints::create_proxy::<EntityMarker>()?;
        entity_resolver.resolve_entity("foo-reference", server_end)?;
        let (entity_proxy2, server_end) = fidl::endpoints::create_proxy::<EntityMarker>()?;
        entity_resolver.resolve_entity("baz-reference", server_end)?;
        let (entity_proxy3, server_end) = fidl::endpoints::create_proxy::<EntityMarker>()?;
        entity_resolver.resolve_entity("bar-reference", server_end)?;

        let futs = vec![
            ContextEntity::from_entity(
                entity_proxy1,
                hashset!(Contributor::module_new("story1", "mod-a", "param-foo")),
            ),
            ContextEntity::from_entity(
                entity_proxy2,
                hashset!(Contributor::module_new("story1", "mod-a", "param-baz")),
            ),
            ContextEntity::from_entity(
                entity_proxy3,
                hashset!(Contributor::module_new("story1", "mod-b", "param-bar")),
            ),
        ];
        Ok(join_all(futs).await.into_iter().map(|e| e.unwrap()).collect::<Vec<ContextEntity>>())
    }

    fn test_action_index() -> Arc<Vec<Action>> {
        Arc::new(serde_json::from_str(include_str!("../../test_data/test_actions.json")).unwrap())
    }
}
