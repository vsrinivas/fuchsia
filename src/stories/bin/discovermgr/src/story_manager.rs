// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::{GRAPH_KEY, TITLE_KEY},
        models::AddModInfo,
        story_graph::{ModuleData, StoryGraph},
        story_storage::{StoryName, StoryStorage, StoryTitle},
        utils,
    },
    chrono::{Datelike, Timelike, Utc},
    failure::Error,
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon as zx,
};

/// Manage multiple story graphs to support restoring stories.
pub struct StoryManager {
    // save stories to Ledger
    story_storage: Box<dyn StoryStorage>,
}

impl StoryManager {
    pub fn new(story_storage: Box<dyn StoryStorage>) -> Self {
        StoryManager { story_storage }
    }

    pub async fn get_story_graph(&self, story_name: &str) -> Option<StoryGraph> {
        self.story_storage
            .get_property(story_name, GRAPH_KEY)
            .await
            .map(|s| serde_json::from_str(&s).ok())
            .unwrap_or(None)
    }

    // Set property of given story with key & value.
    pub async fn set_property(
        &mut self,
        story_name: &StoryName,
        key: &str,
        value: Buffer,
    ) -> Result<(), Error> {
        self.story_storage
            .set_property(story_name, key, utils::vmo_buffer_to_string(Box::new(value))?)
            .await
    }

    // Get property of given story with key.
    pub async fn get_property(&self, story_name: &StoryName, key: String) -> Result<Buffer, Error> {
        let value = self.story_storage.get_property(story_name, &key).await?;
        let data_to_write = value.as_bytes();
        let vmo = zx::Vmo::create(data_to_write.len() as u64)?;
        vmo.write(&data_to_write, 0)?;
        Ok(Buffer { vmo, size: data_to_write.len() as u64 })
    }

    // Restore the story in story_manager by returning a vector of its modules
    pub async fn restore_story_graph(
        &mut self,
        target_story_name: StoryName,
    ) -> Result<Vec<ModuleData>, Error> {
        let story_graph = serde_json::from_str(
            &self.story_storage.get_property(&target_story_name, GRAPH_KEY).await?,
        )
        .unwrap_or(StoryGraph::new());

        let modules = story_graph.get_all_modules().map(|module| module.clone()).collect();

        Ok(modules)
    }

    // Add the module to the story graph by loading it from storage,
    // update it and save it to storage.
    pub async fn add_to_story_graph(&mut self, action: &AddModInfo) -> Result<(), Error> {
        let mut story_graph = self
            .story_storage
            .get_property(action.story_name(), GRAPH_KEY)
            .await
            .map(|s| serde_json::from_str(&s).unwrap_or(StoryGraph::new()))
            .unwrap_or(StoryGraph::new());

        let mut intent = action.intent().clone();
        if intent.action.is_none() {
            intent.action = Some("NONE".to_string());
        }
        story_graph.add_module(action.mod_name(), intent);
        self.story_storage
            .set_property(
                action.story_name(),
                GRAPH_KEY,
                serde_json::to_string(&story_graph).unwrap(),
            )
            .await?;

        let story_title = self.story_storage.get_property(action.story_name(), TITLE_KEY).await;
        if story_title.is_ok() {
            return Ok(());
        }
        let now = Utc::now();
        self.story_storage
            .set_property(
                action.story_name(),
                TITLE_KEY,
                format!(
                    "a story from {:?} {:02}:{:02}:{:02}",
                    now.weekday(),
                    now.hour(),
                    now.minute(),
                    now.second(),
                ),
            )
            .await
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

        let story_graph = serde_json::from_str(
            &story_manager.story_storage.get_property("story_name_1", GRAPH_KEY).await?,
        )
        .unwrap_or(StoryGraph::new());
        assert_eq!(story_graph.get_module_count(), 1);

        // story_name_1 already saved
        assert_eq!(story_manager.story_storage.get_story_count().await?, 1);
        // changed to a new story_name_2
        match suggestion_2.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action).await?;
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }
        // story_name_1 & 2 already saved
        assert_eq!(story_manager.story_storage.get_story_count().await?, 2);
        // restore the story_name_1
        let modules = story_manager.restore_story_graph("story_name_1".to_string()).await?;
        assert_eq!(modules.len(), 1);
        Ok(())
    }
}
