// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        constants::{GRAPH_KEY, STATE_KEY, TITLE_KEY},
        models::{AddModInfo, OutputConsumer},
        story_context_store::Contributor,
        story_graph::{Module, StoryGraph},
        story_storage::{StoryName, StoryStorage, StoryTitle},
    },
    chrono::{Datelike, Timelike, Utc},
    failure::{bail, Error},
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
        story_name: &StoryName,
        key: &str,
        value: String,
    ) -> Result<(), Error> {
        match key {
            // Writing to story graph and instance state is not allowed.
            GRAPH_KEY | STATE_KEY => bail!("Key for set_property is now allowed"),
            _ => self.story_storage.set_property(story_name, key, value).await,
        }
    }

    // Get property of given story with key.
    pub async fn get_property(&self, story_name: &StoryName, key: String) -> Result<String, Error> {
        self.story_storage.get_property(story_name, &key).await
    }

    // Set instance state of mods given story_name, module_name and name of state.
    pub async fn set_instance_state(
        &mut self,
        story_name: &str,
        module_name: &str,
        state_name: &str,
        value: String,
    ) -> Result<(), Error> {
        let identity_path = format!("{}/{}/{}", story_name, module_name, state_name);
        self.story_storage.set_property(&identity_path, STATE_KEY, value).await
    }

    // Get instance state of mods given story_name, module_name and name of state.
    pub async fn get_instance_state(
        &self,
        story_name: &str,
        module_name: &str,
        state_name: &str,
    ) -> Result<String, Error> {
        let identity_path = format!("{}/{}/{}", story_name, module_name, state_name);
        self.story_storage.get_property(&identity_path, STATE_KEY).await
    }

    // Restore the story in story_manager by returning a vector of its modules.
    pub async fn restore_story_graph(
        &mut self,
        target_story_name: StoryName,
    ) -> Result<Vec<Module>, Error> {
        let story_graph = serde_json::from_str(
            &self.story_storage.get_property(&target_story_name, GRAPH_KEY).await?,
        )
        .unwrap_or(StoryGraph::new());
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
                    self.story_storage.set_property(&story_id, GRAPH_KEY, string_content).await?;
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
            self.story_storage.set_property(action.story_name(), GRAPH_KEY, string_content).await?;
        }

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
                story_manager.add_to_story_graph(&action, vec![]).await?;
            }
            _ => assert!(false),
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
                story_manager.add_to_story_graph(&action, vec![]).await?;
            }
            _ => assert!(false),
        }
        // story_name_1 & 2 already saved
        assert_eq!(story_manager.story_storage.get_story_count().await?, 2);
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
        assert_eq!(module_output.entity_reference, "peridot-ref".to_string());
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
            .set_instance_state("some-story", "some-mod", "some-state", "value".to_string())
            .await?;
        let instance_state =
            story_manager.get_instance_state("some-story", "some-mod", "some-state").await?;
        assert_eq!(instance_state, "value".to_string());
        Ok(())
    }
}
