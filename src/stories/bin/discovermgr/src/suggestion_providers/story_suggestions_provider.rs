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
            let result = self
                .story_manager
                .lock()
                .get_name_titles()
                .iter()
                .map(|(name, title)| {
                    StorySuggestionsProvider::new_story_suggestion(
                        name.to_string(),
                        title.to_string(),
                    )
                })
                .collect::<Vec<Suggestion>>();
            Ok(result)
        }))
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::models::SuggestedAction};

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

}
