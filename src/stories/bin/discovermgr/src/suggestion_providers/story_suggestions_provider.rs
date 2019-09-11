// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::{DisplayInfo, Suggestion},
        story_context_store::ContextEntity,
        story_manager::StoryManager,
        suggestions_manager::SearchSuggestionsProvider,
    },
    failure::Error,
    futures::future::LocalFutureObj,
    parking_lot::Mutex,
    std::sync::Arc,
};

pub struct StorySuggestionsProvider {
    story_manager: Arc<Mutex<StoryManager>>,
}

impl StorySuggestionsProvider {
    pub fn new(story_manager: Arc<Mutex<StoryManager>>) -> Self {
        StorySuggestionsProvider { story_manager }
    }

    fn new_story_suggestion(story_name: String, story_title: String) -> Suggestion {
        Suggestion::new_story_suggestion(
            story_name,
            DisplayInfo::new().with_title(format!("Restore {}", story_title).as_str()),
        )
    }
}

impl SearchSuggestionsProvider for StorySuggestionsProvider {
    fn request<'a>(
        &'a self,
        _query: &'a str,
        _context: &'a Vec<&'a ContextEntity>,
    ) -> LocalFutureObj<'a, Result<Vec<Suggestion>, Error>> {
        LocalFutureObj::new(Box::new(async move {
            let story_manager = self.story_manager.lock();
            let mut stories = story_manager.get_story_metadata().await?;
            stories.sort_by(|a, b| b.last_executed_timestamp.cmp(&a.last_executed_timestamp));
            let result = stories
                .into_iter()
                .map(|story| {
                    StorySuggestionsProvider::new_story_suggestion(
                        story.story_name,
                        story.story_title,
                    )
                })
                .collect::<Vec<Suggestion>>();
            Ok(result)
        }))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{AddModInfo, Intent, SuggestedAction},
            story_storage::MemoryStorage,
        },
        fuchsia_async as fasync,
    };

    #[test]
    fn new_story_suggestion() {
        let suggestion = StorySuggestionsProvider::new_story_suggestion(
            "story_name".to_string(),
            "story_title".to_string(),
        );
        assert_eq!(
            suggestion.display_info().title.clone().unwrap(),
            "Restore story_title".to_string()
        );
        match suggestion.action() {
            SuggestedAction::RestoreStory(restore_story_info) => {
                assert_eq!(restore_story_info.story_name.to_string(), "story_name");
            }
            SuggestedAction::AddMod(_) => {
                assert!(false);
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_story_suggestions() -> Result<(), Error> {
        let story_manager_arc =
            Arc::new(Mutex::new(StoryManager::new(Box::new(MemoryStorage::new()))));
        {
            let mut story_manager = story_manager_arc.lock();
            let add_mod_info_1 = AddModInfo::new(Intent::new(), Some("story-1".to_string()), None);
            let add_mod_info_2 = AddModInfo::new(Intent::new(), Some("story-2".to_string()), None);
            story_manager.add_to_story_graph(&add_mod_info_1, vec![]).await?;
            story_manager.add_to_story_graph(&add_mod_info_2, vec![]).await?;
        }

        let story_suggestions_provider = StorySuggestionsProvider::new(story_manager_arc);
        let context_entities = vec![];
        let story_suggestions = story_suggestions_provider.request("", &context_entities).await?;
        assert_eq!(story_suggestions.len(), 2);

        // Ensure that the recent story comes first.
        match story_suggestions[0].action() {
            SuggestedAction::RestoreStory(restore_story_info) => {
                assert_eq!(restore_story_info.story_name.to_string(), "story-2");
            }
            _ => assert!(false),
        }
        match story_suggestions[1].action() {
            SuggestedAction::RestoreStory(restore_story_info) => {
                assert_eq!(restore_story_info.story_name.to_string(), "story-1");
            }
            _ => assert!(false),
        }
        Ok(())
    }
}
