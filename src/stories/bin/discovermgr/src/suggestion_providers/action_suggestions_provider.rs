// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        action_match::query_action_match,
        models::{Action, AddModInfo, Intent, Suggestion},
        story_context_store::ContextEntity,
        suggestions_manager::SearchSuggestionsProvider,
    },
    anyhow::Error,
    futures::future::LocalFutureObj,
    std::sync::Arc,
};

pub struct ActionSuggestionsProvider {
    actions: Arc<Vec<Action>>,
}

impl ActionSuggestionsProvider {
    pub fn new(actions: Arc<Vec<Action>>) -> Self {
        ActionSuggestionsProvider { actions }
    }

    /// Generate a suggestion. Return None when there is no
    /// display_info for the suggestion.
    fn action_to_suggestion(action: &Action) -> Option<Suggestion> {
        let mut intent = Intent::new().with_action(&action.name);
        if let Some(ref fulfillment) = action.fuchsia_fulfillment {
            intent = intent.with_handler(&fulfillment.component_url);
        }
        action
            .action_display
            .as_ref()
            .and_then(|action_display| action_display.display_info.as_ref())
            // requires a display_info and a title
            .filter(|display_info| display_info.title.is_some())
            .map(|display_info| {
                Suggestion::new(AddModInfo::new_intent(intent), display_info.clone())
            })
    }
}

impl SearchSuggestionsProvider for ActionSuggestionsProvider {
    fn request<'a>(
        &'a self,
        query: &'a str,
        _context: &'a Vec<&'a ContextEntity>,
    ) -> LocalFutureObj<'a, Result<Vec<Suggestion>, Error>> {
        LocalFutureObj::new(Box::new(async move {
            let result = self
                .actions
                .iter()
                // keep actions that have no parameters
                .filter(|action| action.parameters.is_empty())
                // keep actions that match the query
                .filter(|action| query_action_match(&action, query))
                // convert to Option<Suggestion>
                .filter_map(|action| ActionSuggestionsProvider::action_to_suggestion(action))
                .collect::<Vec<Suggestion>>();
            Ok(result)
        }))
    }
}
#[cfg(test)]
mod tests {
    use {super::*, crate::models::DisplayInfo, fuchsia_async as fasync};

    fn suggestion(action: &str, handler: &str, icon: &str, title: &str) -> Suggestion {
        Suggestion::new(
            AddModInfo::new_intent(
                Intent::new().with_action(&action.to_string()).with_handler(&handler.to_string()),
            ),
            DisplayInfo::new().with_icon(&icon.to_string()).with_title(&title.to_string()),
        )
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_suggestions() -> Result<(), Error> {
        let actions = test_actions();
        let action_suggestions_provider = ActionSuggestionsProvider::new(actions);
        let context = vec![];
        let results = action_suggestions_provider.request("n", &context).await?;

        let expected_results = vec![suggestion(
            "ACTION_MAIN",
            "fuchsia-pkg://fuchsia.com/collections#meta/collections.cmx",
            "https://example.com/weather-icon",
            "Nouns of the world",
        )];

        // Ensure the suggestion matches.
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].display_info(), expected_results[0].display_info());
        Ok(())
    }

    fn test_actions() -> Arc<Vec<Action>> {
        Arc::new(serde_json::from_str(include_str!("../../test_data/test_actions.json")).unwrap())
    }
}
