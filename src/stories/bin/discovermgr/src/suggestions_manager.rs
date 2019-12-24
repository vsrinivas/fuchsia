// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        mod_manager::ModManager,
        models::Suggestion,
        story_context_store::{ContextEntity, StoryContextStore},
    },
    anyhow::Error,
    fuchsia_syslog::macros::*,
    futures::future::{join_all, LocalFutureObj},
    parking_lot::Mutex,
    std::{collections::HashMap, pin::Pin, sync::Arc},
};

/// Clients who wish to provide suggestions should implement this.
pub trait SearchSuggestionsProvider: Send + Sync {
    fn request<'a>(
        &'a self,
        query: &'a str,
        context: &'a Vec<&'a ContextEntity>,
    ) -> LocalFutureObj<'a, Result<Vec<Suggestion>, Error>>;
}

/// The suggestions manager is in charge of storing suggestions after a given query
/// and executing them when requested.
pub struct SuggestionsManager {
    suggestions: HashMap<String, usize>,
    providers: Vec<Box<dyn SearchSuggestionsProvider>>,
    mod_manager: Arc<Mutex<ModManager<StoryContextStore>>>,
    ordered_suggestions: Vec<Suggestion>,
}

impl SuggestionsManager {
    pub fn new(mod_manager: Arc<Mutex<ModManager<StoryContextStore>>>) -> Self {
        SuggestionsManager {
            suggestions: HashMap::new(),
            providers: vec![],
            mod_manager,
            ordered_suggestions: vec![],
        }
    }

    /// Registers a suggestion provider.
    pub fn register_suggestions_provider(&mut self, provider: Box<dyn SearchSuggestionsProvider>) {
        self.providers.push(provider);
    }

    /// Requests suggestions from the providers and returns them.
    pub async fn get_suggestions<'a>(
        &'a mut self,
        query: &'a str,
        context: &'a Vec<&'a ContextEntity>,
    ) -> impl Iterator<Item = &'a Suggestion> {
        let futs = self
            .providers
            .iter()
            .map(|p| p.request(query, context))
            .map(|fut| Pin::<Box<_>>::from(Box::new(fut)));
        let results = join_all(futs).await;
        let query_lower = query.to_lowercase();
        self.ordered_suggestions = results
            .into_iter()
            .filter(|result| result.is_ok())
            .map(|result| result.unwrap())
            .flat_map(|result| result.into_iter())
            .filter(|s| {
                if let Some(ref title) = &s.display_info().title {
                    title.to_lowercase().contains(&query_lower)
                } else {
                    true
                }
            })
            .collect::<Vec<Suggestion>>();
        self.suggestions = self
            .ordered_suggestions
            .iter()
            .zip(0..self.ordered_suggestions.len())
            .map(|(s, idx)| (s.id().to_string(), idx))
            .collect::<HashMap<String, usize>>();
        self.ordered_suggestions.iter()
    }

    /// Executes the suggestion intent and removes it from the stored suggestions.
    pub async fn execute<'a>(&'a mut self, id: &'a str) -> Result<(), Error> {
        // TODO: propagate meaningful errors.
        match self.suggestions.remove(id) {
            None => {
                fx_log_err!("Suggestion not found");
                Ok(())
            }
            Some(mut index) => {
                let mut issuer = self.mod_manager.lock();
                for (_, v) in self.suggestions.iter_mut() {
                    if v > &mut index {
                        *v -= 1;
                    }
                }
                issuer.execute_suggestion(self.ordered_suggestions.remove(index)).await
            }
        }
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        crate::{
            models::{AddModInfo, DisplayInfo, Intent, Suggestion},
            testing::{
                init_state, puppet_master_fake::PuppetMasterFake,
                suggestion_providers::TestSuggestionsProvider,
            },
        },
        anyhow::Context as _,
        fidl_fuchsia_modular::{
            EntityResolverMarker, IntentParameter, IntentParameterData, PuppetMasterMarker,
            StoryCommand,
        },
        fuchsia_async as fasync,
        fuchsia_component::client::connect_to_service,
        maplit::{hashmap, hashset},
        std::collections::HashSet,
    };

    #[fasync::run_singlethreaded(test)]
    async fn get_suggestions_and_execute() -> Result<(), Error> {
        // Set up the fake puppet master client that is dependency. Even if we
        // don't using it in this test.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();
        puppet_master_fake.set_on_execute("story_name", |_commands| {
            // Puppet master shouldn't be called.
            assert!(false);
        });

        puppet_master_fake.spawn(request_stream);

        // Set up our test suggestions provider. This will provide two suggestions
        // all the time.
        let entity_resolver = connect_to_service::<EntityResolverMarker>()
            .context("failed to connect to entity resolver")?;
        let (_, _, mod_manager) = init_state(puppet_master_client, entity_resolver);
        let mut suggestions_manager = SuggestionsManager::new(mod_manager);
        suggestions_manager.register_suggestions_provider(Box::new(TestSuggestionsProvider::new()));

        // Get suggestions and ensure the right ones are received.
        let context = vec![];
        let suggestions = suggestions_manager
            .get_suggestions("garnet", &context)
            .await
            .collect::<Vec<&Suggestion>>();
        assert_eq!(suggestions.len(), 2);
        let titles = suggestions
            .iter()
            .map(|s| s.display_info().title.as_ref().unwrap().as_ref())
            .collect::<HashSet<&str>>();
        assert_eq!(titles, hashset!("Listen to Garnet", "See concerts for Garnet"));

        // There should be two suggestions stored.
        assert_eq!(suggestions_manager.suggestions.len(), 2);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn execute() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called when the suggestion is executed.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 3);
            if let (
                StoryCommand::AddMod(add_mod),
                StoryCommand::SetFocusState(set_focus),
                StoryCommand::FocusMod(focus_mod),
            ) = (&commands[0], &commands[1], &commands[2])
            {
                assert_eq!(add_mod.intent.action, Some("SEE_CONCERTS".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("abcdefgh".to_string()),
                    },])
                );
                assert!(set_focus.focused);
                assert_eq!(add_mod.mod_name_transitional, focus_mod.mod_name_transitional);
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(request_stream);
        let entity_resolver = connect_to_service::<EntityResolverMarker>()
            .context("failed to connect to entity resolver")?;
        let (_, _, mod_manager) = init_state(puppet_master_client, entity_resolver);
        let mut suggestions_manager = SuggestionsManager::new(mod_manager);

        // Set some fake suggestions.
        suggestions_manager.ordered_suggestions = vec![suggestion!(
            action = "SEE_CONCERTS",
            title = "See concerts for something_garnet",
            parameters = [(name = "artist", entity_reference = "abcdefgh")],
            story = "story_name"
        )];
        suggestions_manager.suggestions = hashmap!("12345".to_string() => 0);

        // Execute the suggestion
        suggestions_manager.execute("12345").await?;

        // The suggestion should be gone
        assert_eq!(suggestions_manager.suggestions.len(), 0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn provider_order_is_reflected() -> Result<(), Error> {
        // Set up the fake puppet master client that is dependency. Even if we
        // don't using it in this test.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();
        puppet_master_fake.set_on_execute("story_name", |_commands| {
            // Puppet master shouldn't be called.
            assert!(false);
        });
        puppet_master_fake.spawn(request_stream);

        // Set up our test suggestions provider.
        let entity_resolver = connect_to_service::<EntityResolverMarker>()
            .context("failed to connect to entity resolver")?;
        let (_, _, mod_manager) = init_state(puppet_master_client, entity_resolver);
        let mut suggestions_manager = SuggestionsManager::new(mod_manager);

        let mut provider1 = TestSuggestionsProvider::new();
        provider1.suggestions = vec![
            suggestion!(action = "A", title = "a", parameters = [], story = "story"),
            suggestion!(action = "B", title = "b", parameters = [], story = "story"),
        ];
        let mut provider2 = TestSuggestionsProvider::new();
        provider2.suggestions = vec![
            suggestion!(action = "C", title = "c", parameters = [], story = "story"),
            suggestion!(action = "D", title = "d", parameters = [], story = "story"),
        ];

        suggestions_manager.register_suggestions_provider(Box::new(provider1));
        suggestions_manager.register_suggestions_provider(Box::new(provider2));

        // Get suggestions and ensure the right ones are received.
        let context = vec![];
        let suggestions =
            suggestions_manager.get_suggestions("", &context).await.collect::<Vec<&Suggestion>>();
        assert_eq!(suggestions.len(), 4);
        let titles = suggestions
            .iter()
            .map(|s| s.display_info().title.as_ref().unwrap().as_ref())
            .collect::<Vec<&str>>();

        // In lack of actual ranking we respect the order in which the providers
        // were registered.
        assert_eq!(titles, vec!["a", "b", "c", "d"]);

        Ok(())
    }
}
