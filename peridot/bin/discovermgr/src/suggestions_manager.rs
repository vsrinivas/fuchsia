// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        action_match::query_text_match, models::Suggestion, story_context_store::ContextEntity,
    },
    failure::Error,
    fidl_fuchsia_modular::{
        ExecuteStatus, PuppetMasterProxy, StoryCommand, StoryPuppetMasterMarker,
    },
    fuchsia_syslog::macros::*,
    futures::future::{join_all, LocalFutureObj},
    std::{collections::HashMap, pin::Pin},
};

/// Clients who wish to provide suggestions should implement this.
pub trait SearchSuggestionsProvider: Send + Sync {
    fn request<'a>(
        &'a self,
        query: &'a str,
        context: &'a Vec<ContextEntity>,
    ) -> LocalFutureObj<'a, Result<Vec<Suggestion>, Error>>;
}

/// The suggestions manager is in charge of storing suggestions after a given query
/// and executing them when requested.
pub struct SuggestionsManager {
    suggestions: HashMap<String, Suggestion>,
    providers: Vec<Box<SearchSuggestionsProvider>>,
    puppet_master: PuppetMasterProxy,
}

impl SuggestionsManager {
    pub fn new(puppet_master: PuppetMasterProxy) -> Self {
        SuggestionsManager { suggestions: HashMap::new(), puppet_master, providers: vec![] }
    }

    /// Registers a suggestion provider.
    pub fn register_suggestions_provider(&mut self, provider: Box<SearchSuggestionsProvider>) {
        self.providers.push(provider);
    }

    /// Requests suggestions from the providers and returns them.
    pub async fn get_suggestions<'a>(
        &'a mut self,
        query: &'a str,
        context: Vec<ContextEntity>,
    ) -> impl Iterator<Item = &'a Suggestion> {
        let futs = self
            .providers
            .iter()
            .map(|p| p.request(query, &context))
            .map(|fut| Pin::<Box<_>>::from(Box::new(fut)));
        let results = await!(join_all(futs));
        self.suggestions = results
            .into_iter()
            .filter(|result| result.is_ok())
            .map(|result| result.unwrap())
            .flat_map(|result| result.into_iter())
            .filter(|s| {
                if let Some(ref title) = &s.display_info().title {
                    query_text_match(query, &title)
                } else {
                    true
                }
            })
            .map(|s| (s.id().to_string(), s))
            .collect::<HashMap<String, Suggestion>>();
        self.suggestions.values()
    }

    /// Executes the suggestion intent and removes it from the stored suggestions.
    pub async fn execute<'a>(&'a mut self, id: &'a str) -> Result<(), Error> {
        // TODO: propagate meaningful errors.
        match self.suggestions.remove(id) {
            None => {
                fx_log_err!("Suggestion not found");
                Ok(())
            }
            Some(suggestion) => {
                let (story_puppet_master, server_end) =
                    fidl::endpoints::create_proxy::<StoryPuppetMasterMarker>()?;
                self.puppet_master.control_story(suggestion.story_name(), server_end)?;
                let command = StoryCommand::AddMod(suggestion.action().clone().into());
                story_puppet_master.enqueue(&mut vec![command].iter_mut())?;
                let result = await!(story_puppet_master.execute())?;
                if result.status != ExecuteStatus::Ok {
                    fx_log_err!(
                        "Modular error status:{:?} message:{}",
                        result.status,
                        result.error_message.unwrap_or("none".to_string())
                    )
                }
                Ok(())
            }
        }
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        crate::{
            models::{AddMod, DisplayInfo, Intent, Suggestion},
            testing::{
                puppet_master_fake::PuppetMasterFake, suggestion_providers::TestSuggestionsProvider,
            },
        },
        fidl_fuchsia_modular::{IntentParameter, IntentParameterData, PuppetMasterMarker},
        fuchsia_async as fasync,
        maplit::{hashmap, hashset},
        std::collections::HashSet,
    };

    #[fasync::run_singlethreaded(test)]
    async fn get_suggestions_and_execute() -> Result<(), Error> {
        // Set up the fake puppet master client that is dependency. Even if we
        // don't using it in this test.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();
        puppet_master_fake.set_on_execute("story_name", |_commands| {
            // Puppet master shouldn't be called.
            assert!(false);
        });

        puppet_master_fake.spawn(request_stream);

        // Set up our test suggestions provider. This will provide two suggestions
        // all the time.
        let mut suggestions_manager = SuggestionsManager::new(client);
        suggestions_manager.register_suggestions_provider(Box::new(TestSuggestionsProvider::new()));

        // Get suggestions and ensure the right ones are received.
        let suggestions = await!(suggestions_manager.get_suggestions("garnet", vec![]))
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
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called when the suggestion is executed.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 1);
            if let StoryCommand::AddMod(add_mod) = &commands[0] {
                assert_eq!(add_mod.intent.action, Some("SEE_CONCERTS".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("abcdefgh".to_string()),
                    },])
                );
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(request_stream);
        let mut suggestions_manager = SuggestionsManager::new(client);

        // Set some fake suggestions.
        suggestions_manager.suggestions = hashmap!(
        "12345".to_string() => suggestion!(
            action = "SEE_CONCERTS",
            title = "See concerts for Garnet",
            parameters = [(name = "artist", entity_reference = "abcdefgh")]
        ));

        // Execute the suggestion
        await!(suggestions_manager.execute("12345"))?;

        // The suggestion should be gone
        assert_eq!(suggestions_manager.suggestions.len(), 0);

        Ok(())
    }
}
