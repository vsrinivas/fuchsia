// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::EMPTY_STORY_TITLE,
        models::{DisplayInfo, Suggestion},
        story_context_store::ContextEntity,
        story_manager::StoryManager,
        suggestions_manager::SearchSuggestionsProvider,
    },
    anyhow::Error,
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

    fn new_story_suggestion(story_name: impl Into<String>, story_title: &str) -> Suggestion {
        Suggestion::new_story_suggestion(
            story_name.into(),
            DisplayInfo::new().with_title(&format!("Restore {}", story_title)),
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
                .filter(|story| story.story_title != EMPTY_STORY_TITLE)
                .map(|story| {
                    StorySuggestionsProvider::new_story_suggestion(
                        story.story_name,
                        &story.story_title,
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
            constants::TITLE_KEY,
            models::{AddModInfo, Intent, SuggestedAction},
            story_storage::MemoryStorage,
        },
        fuchsia_async as fasync,
    };

    #[test]
    fn new_story_suggestion() {
        let suggestion =
            StorySuggestionsProvider::new_story_suggestion("story_name", "story_title");
        assert_eq!(suggestion.display_info().title.as_ref().unwrap(), "Restore story_title");
        match suggestion.action() {
            SuggestedAction::RestoreStory(restore_story_info) => {
                assert_eq!(restore_story_info.story_name, "story_name");
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
            let add_mod_info_3 = AddModInfo::new(Intent::new(), Some("story-3".to_string()), None);
            story_manager.add_to_story_graph(&add_mod_info_1, vec![]).await?;
            story_manager.add_to_story_graph(&add_mod_info_2, vec![]).await?;
            story_manager.add_to_story_graph(&add_mod_info_3, vec![]).await?;

            // The third story won't have a title, so it shouldn't show up.
            story_manager.set_property("story-1", TITLE_KEY, "story one").await.unwrap();
            story_manager.set_property("story-2", TITLE_KEY, "story two").await.unwrap();
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
        assert_eq!(
            story_suggestions[0].display_info().title.as_ref().unwrap(),
            "Restore story two"
        );
        match story_suggestions[1].action() {
            SuggestedAction::RestoreStory(restore_story_info) => {
                assert_eq!(restore_story_info.story_name.to_string(), "story-1");
            }
            _ => assert!(false),
        }
        assert_eq!(
            story_suggestions[1].display_info().title.as_ref().unwrap(),
            "Restore story one"
        );
        Ok(())
    }
}
