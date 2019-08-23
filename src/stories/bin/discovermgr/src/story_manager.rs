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
    last_modified_story_name: Option<StoryName>,
    last_modified_story_title: Option<StoryTitle>,
    last_modified_story_graph: StoryGraph,
}

impl StoryManager {
    pub fn new(story_storage: Box<dyn StoryStorage>) -> Self {
        StoryManager {
            story_storage,
            last_modified_story_name: None,
            last_modified_story_title: None,
            last_modified_story_graph: StoryGraph::new(),
        }
    }

    pub async fn get_story_graph(&self, story_name: &str) -> Option<StoryGraph> {
        self.story_storage
            .get_property(story_name, GRAPH_KEY)
            .await
            .map(|s| serde_json::from_str(&s).ok())
            .unwrap_or(None)
    }

    // Reload last_modified_story_title in case it is changed
    async fn update_story_info(&mut self, key: &str) -> Result<(), Error> {
        match key {
            TITLE_KEY => {
                if self.last_modified_story_name.is_some() {
                    self.last_modified_story_title = Some(
                        self.story_storage
                            .get_property(
                                self.last_modified_story_name.as_ref().unwrap(),
                                TITLE_KEY,
                            )
                            .await?,
                    );
                }
            }
            _ => {}
        }
        Ok(())
    }

    // Set property of given story with key & value;
    pub async fn serve_set_property(
        &mut self,
        story_name: &StoryName,
        key: &str,
        value: Buffer,
    ) -> Result<(), Error> {
        self.story_storage
            .set_property(story_name, key, utils::vmo_buffer_to_string(Box::new(value))?)
            .await?;
        self.update_story_info(key).await
    }

    // Get property of given story with key;
    pub async fn serve_get_property(
        &self,
        story_name: &StoryName,
        key: String,
    ) -> Result<Buffer, Error> {
        let value = self.story_storage.get_property(story_name, &key).await?;
        let data_to_write = value.as_bytes();
        let vmo = zx::Vmo::create(data_to_write.len() as u64)?;
        vmo.write(&data_to_write, 0)?;
        Ok(Buffer { vmo, size: data_to_write.len() as u64 })
    }

    // Save the last modified story graph to storage
    async fn save_last_modified_story_graph(&mut self) -> Result<(), Error> {
        if self.last_modified_story_graph.get_module_count() > 0
            && self.last_modified_story_name.is_some()
        {
            self.story_storage
                .set_property(
                    &self.last_modified_story_name.as_ref().unwrap(),
                    GRAPH_KEY,
                    serde_json::to_string(&self.last_modified_story_graph).unwrap(),
                )
                .await?;
            // save its title using timestamp if user doesnot provide that
            if self.last_modified_story_title.is_none() {
                let now = Utc::now();
                let story_title = format!(
                    "a story from {:?} {:02}:{:02}:{:02}",
                    now.weekday(),
                    now.hour(),
                    now.minute(),
                    now.second(),
                );
                self.story_storage
                    .set_property(
                        &self.last_modified_story_name.as_ref().unwrap(),
                        TITLE_KEY,
                        story_title,
                    )
                    .await?;
            }
        }
        Ok(())
    }

    // Restore the story in story_manager and return an iterator of modules in it
    pub async fn restore_story_graph(
        &mut self,
        target_story_name: StoryName,
    ) -> Result<impl Iterator<Item = &ModuleData>, Error> {
        if self.last_modified_story_name != Some(target_story_name.clone()) {
            self.last_modified_story_name = Some(target_story_name);
            self.last_modified_story_title = Some(
                self.story_storage
                    .get_property(self.last_modified_story_name.as_ref().unwrap(), TITLE_KEY)
                    .await?,
            );
            self.last_modified_story_graph = serde_json::from_str(
                &self
                    .story_storage
                    .get_property(self.last_modified_story_name.as_ref().unwrap(), GRAPH_KEY)
                    .await?,
            )
            .unwrap_or(StoryGraph::new())
        }
        Ok(self.last_modified_story_graph.get_all_modules())
    }

    // Add the suggestion to the story graph and if starting a new story
    // then first save the story graph.
    pub async fn add_to_story_graph(&mut self, action: &AddModInfo) -> Result<(), Error> {
        let ongoing_story_name = action.story_name().to_string();
        match &self.last_modified_story_name {
            Some(story_name) => {
                if story_name != &ongoing_story_name {
                    // changed to another story: save the previous one to storage
                    // and load/create the next one
                    self.last_modified_story_name = Some(ongoing_story_name);
                    self.last_modified_story_title = self
                        .story_storage
                        .get_property(self.last_modified_story_name.as_ref().unwrap(), TITLE_KEY)
                        .await
                        .ok(); // ok is allowed here as this could be a new story
                    self.last_modified_story_graph = self
                        .story_storage
                        .get_property(self.last_modified_story_name.as_ref().unwrap(), GRAPH_KEY)
                        .await
                        .map(|s| serde_json::from_str(&s).unwrap_or(StoryGraph::new()))
                        .unwrap_or(StoryGraph::new());
                }
            }
            None => {
                // clear the storage when system reboots
                // should be removed if we really want to save all histories
                self.story_storage.clear().await?;
                self.last_modified_story_name = Some(ongoing_story_name);
                self.last_modified_story_graph = self
                    .story_storage
                    .get_property(self.last_modified_story_name.as_ref().unwrap(), GRAPH_KEY)
                    .await
                    .map(|s| serde_json::from_str(&s).unwrap_or(StoryGraph::new()))
                    .unwrap_or(StoryGraph::new());
            }
        }
        let mut intent = action.intent().clone();
        if intent.action.is_none() {
            intent.action = Some("NONE".to_string());
        }
        self.last_modified_story_graph.add_module(action.mod_name(), intent);
        self.save_last_modified_story_graph().await
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
        assert_eq!(story_manager.last_modified_story_name.as_ref().unwrap(), "story_name_1");
        assert_eq!(story_manager.last_modified_story_graph.get_module_count(), 1);
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
        assert_eq!(story_manager.last_modified_story_name.as_ref().unwrap(), "story_name_2");
        // story_name_1 & 2 already saved
        assert_eq!(story_manager.story_storage.get_story_count().await?, 2);

        // restore the story_name_1
        let _modules = story_manager.restore_story_graph("story_name_1".to_string()).await?;
        drop(_modules);
        assert_eq!(story_manager.last_modified_story_name.as_ref().unwrap(), "story_name_1");
        assert_eq!(story_manager.last_modified_story_graph.get_module_count(), 1);
        Ok(())
    }
}
