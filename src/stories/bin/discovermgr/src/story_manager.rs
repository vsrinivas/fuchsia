// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::{EMPTY_STORY_TITLE, GRAPH_KEY, STATE_KEY, TIME_KEY, TITLE_KEY},
        models::{AddModInfo, OutputConsumer, StoryMetadata},
        story_context_store::Contributor,
        story_graph::{Module, StoryGraph},
        story_storage::{StoryName, StoryStorage},
    },
    anyhow::{format_err, Error},
    fidl_fuchsia_app_discover::StoryDiscoverError,
    std::{
        collections::HashMap,
        time::{SystemTime, UNIX_EPOCH},
    },
};

/// Manage multiple story graphs to support restoring stories.
pub struct StoryManager {
    // Save stories to Ledger.
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
        story_name: &str,
        key: &str,
        value: impl Into<String>,
    ) -> Result<(), StoryDiscoverError> {
        match key {
            // Writing to story graph and instance state is not allowed.
            GRAPH_KEY | STATE_KEY => Err(StoryDiscoverError::InvalidKey),
            _ => self.story_storage.set_property(story_name, key, value.into()).await,
        }
    }

    // Get property of given story with key.
    pub async fn get_property(
        &self,
        story_name: &str,
        key: &str,
    ) -> Result<String, StoryDiscoverError> {
        self.story_storage.get_property(story_name, &key).await
    }

    // Set instance state of mods given story_name, module_name and name of state.
    pub async fn set_instance_state(
        &mut self,
        story_name: &str,
        module_name: &str,
        state_name: &str,
        value: impl Into<String>,
    ) -> Result<(), StoryDiscoverError> {
        let identity_path = format!("{}/{}/{}", story_name, module_name, state_name);
        self.story_storage.set_property(&identity_path, STATE_KEY, value.into()).await
    }

    // Get instance state of mods given story_name, module_name and name of state.
    pub async fn get_instance_state(
        &self,
        story_name: &str,
        module_name: &str,
        state_name: &str,
    ) -> Result<String, StoryDiscoverError> {
        let identity_path = format!("{}/{}/{}", story_name, module_name, state_name);
        self.story_storage.get_property(&identity_path, STATE_KEY).await
    }

    // Update the time-stamp that a story is executed last time.
    pub async fn update_timestamp(&mut self, story_name: &str) -> Result<(), Error> {
        let timestamp =
            SystemTime::now().duration_since(UNIX_EPOCH).expect("time went backwards").as_nanos();
        self.story_storage
            .set_property(story_name, TIME_KEY, timestamp.to_string())
            .await
            .map_err(StoryManager::error_mapping)
    }

    // Restore the story in story_manager by returning a vector of its modules
    pub async fn restore_story_graph(
        &mut self,
        target_story_name: StoryName,
    ) -> Result<Vec<Module>, Error> {
        let story_graph = serde_json::from_str(
            &self
                .story_storage
                .get_property(&target_story_name, GRAPH_KEY)
                .await
                .map_err(StoryManager::error_mapping)?,
        )
        .unwrap_or(StoryGraph::new());

        self.update_timestamp(&target_story_name).await?;
        Ok(story_graph.get_all_modules().map(|(k, v)| Module::new(k.clone(), v.clone())).collect())
    }

    // Update the graph when a contributor changes its output.
    pub async fn update_graph_for_replace(
        &mut self,
        old_reference: &str,
        new_reference: &str,
        contributor: Contributor,
    ) -> Result<(), Error> {
        match contributor {
            Contributor::ModuleContributor { story_id, module_id, parameter_name } => {
                let mut story_graph = self
                    .story_storage
                    .get_property(&story_id, GRAPH_KEY)
                    .await
                    .map(|s| serde_json::from_str(&s).unwrap_or(StoryGraph::new()))
                    .unwrap_or(StoryGraph::new());

                let consumer_ids = match story_graph.get_module_data_mut(&module_id) {
                    Some(module_data) => {
                        // Update the provider.
                        module_data.update_output(&parameter_name, Some(new_reference.to_string()));
                        module_data.outputs[&parameter_name]
                            .consumers
                            .iter()
                            .map(|(id, _)| id.to_string())
                            .collect()
                    }
                    None => vec![],
                };
                // Update the intent of each consumer module.
                for consumer_module_id in consumer_ids {
                    if let Some(consumer_module_data) =
                        story_graph.get_module_data_mut(&consumer_module_id)
                    {
                        let new_intent = consumer_module_data
                            .last_intent
                            .clone_with_new_reference(old_reference, new_reference);
                        consumer_module_data.update_intent(new_intent);
                    }
                }

                if let Ok(string_content) = serde_json::to_string(&story_graph) {
                    self.story_storage
                        .set_property(&story_id, GRAPH_KEY, string_content)
                        .await
                        .map_err(StoryManager::error_mapping)?;
                }
                Ok(())
            }
        }
    }

    // Add the mod to the story graph by loading it from storage,
    // update it and save it to storage.
    pub async fn add_to_story_graph(
        &mut self,
        action: &AddModInfo,
        output_consumers: Vec<OutputConsumer>,
    ) -> Result<(), Error> {
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

        for output_consumer in output_consumers {
            match story_graph.get_module_data_mut(&output_consumer.module_id) {
                Some(module_data) => {
                    module_data.add_child(action.mod_name());
                    module_data.add_output_consumer(
                        &output_consumer.output_name,
                        output_consumer.entity_reference,
                        action.mod_name(),
                        output_consumer.consume_type,
                    );
                }
                None => {}
            }
        }

        if let Ok(string_content) = serde_json::to_string(&story_graph) {
            self.story_storage
                .set_property(action.story_name(), GRAPH_KEY, string_content)
                .await
                .map_err(StoryManager::error_mapping)?;
        }

        let story_title = self.story_storage.get_property(action.story_name(), TITLE_KEY).await;
        if story_title.is_ok() {
            return Ok(());
        }
        self.story_storage
            .set_property(action.story_name(), TITLE_KEY, EMPTY_STORY_TITLE.to_string())
            .await
            .map_err(StoryManager::error_mapping)?;
        self.update_timestamp(action.story_name()).await
    }

    // Return saved story metadata to generate suggestions.
    pub async fn get_story_metadata(&self) -> Result<Vec<StoryMetadata>, Error> {
        let mut time_map = self
            .story_storage
            .get_entries(TIME_KEY)
            .await
            .map_err(StoryManager::error_mapping)?
            .into_iter()
            .map(|(name, time)| {
                (name.split_at(TIME_KEY.len() + 1).1.to_string(), time.parse::<u128>().unwrap_or(0))
            })
            .collect::<HashMap<String, u128>>();
        Ok(self
            .story_storage
            .get_name_titles()
            .await
            .map_err(StoryManager::error_mapping)?
            .iter()
            .map(|(name, title)| {
                (StoryMetadata::new(name, title, time_map.remove(name).unwrap_or(0)))
            })
            .collect())
    }

    pub fn error_mapping(error: StoryDiscoverError) -> Error {
        match error {
            StoryDiscoverError::Storage => format_err!("StoryDicoverError : Storage"),
            StoryDiscoverError::VmoStringConversion => {
                format_err!("StoryDiscoverError: VmoStringConversion")
            }
            StoryDiscoverError::InvalidKey => format_err!("StoryDiscoverError : InvalidKey"),
        }
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
        anyhow::Error,
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
                story_manager.add_to_story_graph(&action, vec![]).await?;
            }
            _ => assert!(false),
        }

        let story_graph = serde_json::from_str(
            &story_manager
                .story_storage
                .get_property("story_name_1", GRAPH_KEY)
                .await
                .map_err(StoryManager::error_mapping)?,
        )
        .unwrap_or(StoryGraph::new());
        assert_eq!(story_graph.get_module_count(), 1);
        assert_eq!(
            story_manager.story_storage.get_property("story_name_1", TITLE_KEY).await.unwrap(),
            EMPTY_STORY_TITLE
        );

        // story_name_1 already saved
        assert_eq!(
            story_manager
                .story_storage
                .get_story_count()
                .await
                .map_err(StoryManager::error_mapping)?,
            1
        );
        // changed to a new story_name_2
        match suggestion_2.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action, vec![]).await?;
            }
            _ => assert!(false),
        }
        // story_name_1 & 2 already saved
        assert_eq!(
            story_manager
                .story_storage
                .get_story_count()
                .await
                .map_err(StoryManager::error_mapping)?,
            2
        );
        // restore the story_name_1
        let modules = story_manager.restore_story_graph("story_name_1".to_string()).await?;
        assert_eq!(modules.len(), 1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn save_links() -> Result<(), Error> {
        let mut story_manager = StoryManager::new(Box::new(MemoryStorage::new()));
        let suggestion_1 = suggestion!(
            action = "NOUNS_OF_WORLD",
            title = "Nouns of world",
            parameters = [],
            story = "story_name_1"
        );

        let mod_name_1 = match suggestion_1.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action, vec![]).await?;
                Some(action.mod_name())
            }
            _ => None,
        }
        .unwrap();

        let contributors = vec![OutputConsumer::new(
            "peridot-ref",
            mod_name_1,
            "selected",
            "https://schema.org/MusicGroup",
        )];
        let suggestion_2 = suggestion!(
            action = "PLAY_MUSIC",
            title = "Play music",
            parameters = [(name = "artist", entity_reference = "peridot-ref")],
            story = "story_name_1"
        );

        let mod_name_2 = match suggestion_2.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action, contributors).await?;
                Some(action.mod_name())
            }
            _ => None,
        }
        .unwrap();

        let story_graph = story_manager
            .story_storage
            .get_property("story_name_1", GRAPH_KEY)
            .await
            .map(|s| serde_json::from_str(&s).unwrap_or(StoryGraph::new()))
            .unwrap_or(StoryGraph::new());

        let module_data_1 = story_graph.get_module_data(mod_name_1).unwrap();
        assert_eq!(module_data_1.outputs.len(), 1);
        let module_output = &module_data_1.outputs["selected"];
        assert_eq!(&module_output.entity_reference, "peridot-ref");
        assert_eq!(module_output.consumers.len(), 1);
        assert_eq!(
            module_output
                .consumers
                .iter()
                .filter(|(module_name, type_name)| module_name == mod_name_2
                    && type_name == "https://schema.org/MusicGroup")
                .count(),
            1
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_and_get_instance_state() -> Result<(), Error> {
        let mut story_manager = StoryManager::new(Box::new(MemoryStorage::new()));
        story_manager
            .set_instance_state("some-story", "some-mod", "some-state", "value")
            .await
            .map_err(StoryManager::error_mapping)?;
        let instance_state = story_manager
            .get_instance_state("some-story", "some-mod", "some-state")
            .await
            .map_err(StoryManager::error_mapping)?;
        assert_eq!(&instance_state, "value");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn update_timestamp() -> Result<(), Error> {
        let mut story_manager = StoryManager::new(Box::new(MemoryStorage::new()));
        let story_name = "story_1".to_string();
        story_manager.update_timestamp(&story_name).await?;
        let timestamp_1 = story_manager
            .get_property(&story_name, TIME_KEY)
            .await
            .map_err(StoryManager::error_mapping)?
            .parse::<u128>()
            .unwrap_or(0);
        story_manager.update_timestamp(&story_name).await?;
        let timestamp_2 = story_manager
            .get_property(&story_name, TIME_KEY)
            .await
            .map_err(StoryManager::error_mapping)?
            .parse::<u128>()
            .unwrap_or(0);
        assert!(timestamp_2 > timestamp_1);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn maintain_story_recency() -> Result<(), Error> {
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

        // Execute two addmod suggestions one by one.
        match suggestion_1.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action, vec![]).await?;
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }
        match suggestion_2.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action, vec![]).await?;
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }

        // Ensure that the most recent story is ranked first.
        let mut stories = story_manager.get_story_metadata().await?;
        assert_eq!(stories.len(), 2);
        stories.sort_by(|a, b| b.last_executed_timestamp.cmp(&a.last_executed_timestamp));
        assert_eq!(&stories[0].story_name, "story_name_2");
        assert_eq!(&stories[1].story_name, "story_name_1");

        // Restore one story and see if the recency ranking results change.
        let _ = story_manager.restore_story_graph("story_name_1".to_string()).await?;
        let mut stories = story_manager.get_story_metadata().await?;
        assert_eq!(stories.len(), 2);
        stories.sort_by(|a, b| b.last_executed_timestamp.cmp(&a.last_executed_timestamp));
        assert_eq!(&stories[0].story_name, "story_name_1");
        assert_eq!(&stories[1].story_name, "story_name_2");
        Ok(())
    }
}
