// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::AddModInfo,
        story_graph::{ModuleData, StoryGraph},
        story_storage::{StoryName, StoryStorage, StoryTitle},
    },
    chrono::{Datelike, Timelike, Utc},
    failure::Error,
    fuchsia_syslog::macros::*,
    uuid::Uuid,
};

/// Manage multiple story graphs to support restoring stories.
pub struct StoryManager {
    // save stories to Ledger
    story_storage: Box<dyn StoryStorage>,
    current_story_name: Option<StoryName>,
    current_story_graph: StoryGraph,
}

impl StoryManager {
    pub fn new(story_storage: Box<dyn StoryStorage>) -> Self {
        StoryManager {
            story_storage,
            current_story_name: None,
            current_story_graph: StoryGraph::new(),
        }
    }

    // Save the current story graph to storage
    async fn save_current_story_graph(&mut self) -> Result<(), Error> {
        if self.current_story_graph.get_module_count() > 0 && self.current_story_name.is_some() {
            self.story_storage
                .insert_graph(
                    self.current_story_name.clone().unwrap(),
                    self.current_story_graph.clone(),
                )
                .await?;
            let now = Utc::now();
            let story_title = format!(
                "a story from {:?} {:02}:{:02}:{:02}",
                now.weekday(),
                now.hour(),
                now.minute(),
                now.second(),
            ); // we will allow user to name their story in the future
            self.story_storage
                .insert_name_title(self.current_story_name.as_ref().unwrap().clone(), story_title)
                .await?;
            // update the story_name_index
        }
        Ok(())
    }

    // Restore the story in story_manager and return an iterator of modules in it
    pub async fn restore_story_graph(
        &mut self,
        target_story_name: StoryName,
    ) -> Result<impl Iterator<Item = &ModuleData>, Error> {
        if self.current_story_name != Some(target_story_name.clone()) {
            self.save_current_story_graph().await?;
            self.current_story_name = Some(target_story_name);
            let loaded_graph =
                self.story_storage.get_graph(self.current_story_name.as_ref().unwrap()).await;
            self.current_story_graph =
                loaded_graph.map(|story_graph| story_graph.clone()).unwrap_or_else(|| {
                    fx_log_err!(
                        "Unable to load story named {}",
                        self.current_story_name.as_ref().unwrap()
                    );
                    StoryGraph::new()
                })
        }
        Ok(self.current_story_graph.get_all_modules())
    }

    // Add the suggestion to the story graph and if starting a new story
    // then first save the story graph.
    pub async fn add_to_story_graph(&mut self, action: &AddModInfo) -> Result<(), Error> {
        let ongoing_story_name = action.story_name().to_string();
        match &self.current_story_name {
            Some(story_name) => {
                if story_name != &ongoing_story_name {
                    // changed to another story: save the previous one to storage
                    // and load/create the next one
                    self.save_current_story_graph().await?;
                    self.current_story_name = Some(ongoing_story_name);
                    let loaded_graph = self
                        .story_storage
                        .get_graph(self.current_story_name.as_ref().unwrap())
                        .await;
                    self.current_story_graph = loaded_graph.unwrap_or(StoryGraph::new());
                }
            }
            None => {
                // clear the storage when system reboots
                // should be removed if we really want to save all histories
                self.story_storage.clear().await?;
                // current story is empty. directly load the target without saving current one
                self.current_story_name = Some(ongoing_story_name);
                let loaded_graph =
                    self.story_storage.get_graph(self.current_story_name.as_ref().unwrap()).await;
                self.current_story_graph = loaded_graph.unwrap_or(StoryGraph::new());
            }
        }
        self.current_story_graph.add_module(Uuid::new_v4().to_string(), action.intent().clone());
        Ok(())
    }

    // Return names and titles of saved stories.
    pub async fn get_name_titles(&self) -> Result<Vec<(StoryName, StoryTitle)>, Error> {
        self.story_storage.get_name_titles().await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{DisplayInfo, Intent, SuggestedAction, Suggestion},
            story_storage::MemoryStorage,
        },
        failure::Error,
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn add_to_story_graph_and_restore() -> Result<(), Error> {
        let mut story_manager = StoryManager::new(Box::new(MemoryStorage::new()));
        let suggestion_1 = suggestion!(
            action = "PLAY_MUSIC",
            title = "Play music",
            parameters = [(name = "artist", entity_reference = "peridot-ref")],
            story = "story_name_1"
        );

        let suggestion_2 = suggestion!(
            action = "PLAY_MUSIC",
            title = "Play music",
            parameters = [(name = "artist", entity_reference = "peridot-ref")],
            story = "story_name_2"
        );
        match suggestion_1.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action).await?;
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }
        assert_eq!(story_manager.current_story_name.as_ref().unwrap(), "story_name_1");
        assert_eq!(story_manager.current_story_graph.get_module_count(), 1);
        // changed to a new story_name_2
        match suggestion_2.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action).await?;
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }
        assert_eq!(story_manager.current_story_name.as_ref().unwrap(), "story_name_2");
        // previous story_name_1 already saved
        assert_eq!(story_manager.story_storage.get_story_count().await?, 1);

        // restore the story_name_1
        let _modules = story_manager.restore_story_graph("story_name_1".to_string()).await?;
        drop(_modules);
        assert_eq!(story_manager.current_story_name.as_ref().unwrap(), "story_name_1");
        // story_name_2 is also saved
        assert_eq!(story_manager.story_storage.get_story_count().await?, 2);
        assert_eq!(story_manager.current_story_graph.get_module_count(), 1);
        Ok(())
    }
}
