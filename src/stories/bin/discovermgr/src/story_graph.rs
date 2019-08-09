// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::models::Intent,
    maplit::{hashmap, hashset},
    std::{
        collections::{HashMap, HashSet},
        time::{SystemTime, UNIX_EPOCH},
    },
};

type EntityReference = String;
type EntityType = String;
type ModuleId = String;
type OutputName = String;
#[cfg(test)]
type StoryId = String;

#[cfg(test)]
pub struct SessionGraph {
    stories: HashMap<StoryId, StoryGraph>,
}
#[cfg(test)]
impl SessionGraph {
    /// Creates a new empty session graph.
    pub fn new() -> Self {
        SessionGraph { stories: hashmap!() }
    }

    /// Creates a new story entry in the graph.
    pub fn new_story(&mut self, story_id: impl Into<String>) {
        self.stories.insert(story_id.into(), StoryGraph::new());
    }

    /// Returns the story graph for the given |story_id|.
    pub fn get_story_graph(&self, story_id: &str) -> Option<&StoryGraph> {
        self.stories.get(story_id)
    }

    /// Returns the mutable story graph for the given |story_id|.
    pub fn get_story_graph_mut(&mut self, story_id: &str) -> Option<&mut StoryGraph> {
        self.stories.get_mut(story_id)
    }
}
#[derive(Clone)]
pub struct StoryGraph {
    modules: HashMap<ModuleId, ModuleData>,
}

impl StoryGraph {
    /// Creates a new empty story graph.
    pub fn new() -> Self {
        StoryGraph { modules: hashmap!() }
    }

    /// Adds a module with the given initial intent to the graph.
    pub fn add_module(&mut self, module_id: impl Into<String>, intent: Intent) {
        let module_id_str = module_id.into();
        self.modules.insert(module_id_str.clone(), ModuleData::new(intent));
    }

    #[cfg(test)]
    /// Returns the module data associated to the given |module_id|.
    pub fn get_module_data(&self, module_id: &str) -> Option<&ModuleData> {
        self.modules.get(module_id)
    }

    #[cfg(test)]
    /// Returns the mutable module data associated to the given |module_id|.
    pub fn get_module_data_mut(&mut self, module_id: &str) -> Option<&mut ModuleData> {
        self.modules.get_mut(module_id)
    }

    /// Returns an iterator of all modules in it.
    pub fn get_all_modules(&self) -> impl Iterator<Item = &ModuleData> {
        self.modules.values()
    }

    /// Retures the number of modules in this story.
    pub fn get_module_count(&self) -> usize {
        self.modules.len()
    }
}

#[derive(Clone)]
pub struct ModuleData {
    outputs: HashMap<OutputName, ModuleOutput>,
    children: HashSet<ModuleId>,
    pub last_intent: Intent,
    created_timestamp: u128,
    last_modified_timestamp: u128,
}

impl ModuleData {
    /// Creates a new empty module data with the given |intent| as the intial one.
    fn new(intent: Intent) -> Self {
        let timestamp =
            SystemTime::now().duration_since(UNIX_EPOCH).expect("time went backwards").as_nanos();
        ModuleData {
            children: hashset!(),
            outputs: hashmap!(),
            last_intent: intent,
            created_timestamp: timestamp,
            last_modified_timestamp: timestamp,
        }
    }

    #[cfg(test)]
    /// Updates an output with the given reference. If no reference is given, the
    /// output is removed.
    pub fn update_output(&mut self, output_name: &str, new_reference: Option<String>) {
        match new_reference {
            Some(reference) => {
                let output = self
                    .outputs
                    .entry(output_name.to_string())
                    .or_insert(ModuleOutput::new(reference.clone()));
                output.update_reference(reference);
            }
            None => {
                self.outputs.remove(output_name);
            }
        }
        self.update_timestamp();
    }

    #[cfg(test)]
    /// Updates the last intent issued to the module with |new_intent|.
    pub fn update_intent(&mut self, new_intent: Intent) {
        self.last_intent = new_intent;
        self.update_timestamp();
    }

    #[cfg(test)]
    /// Links two mods through intents. This means this module issued an intent to
    /// the module with id |child_module_id|.
    pub fn add_child(&mut self, child_module_id: impl Into<String>) {
        self.children.insert(child_module_id.into());
        self.update_timestamp();
    }

    #[cfg(test)]
    /// Unlinks two mods through linked through intent issuing.
    pub fn remove_child(&mut self, child_module_id: &str) {
        self.children.remove(child_module_id);
        self.update_timestamp();
    }

    #[cfg(test)]
    fn update_timestamp(&mut self) {
        self.last_modified_timestamp =
            SystemTime::now().duration_since(UNIX_EPOCH).expect("time went backwards").as_nanos();
    }
}
#[derive(Clone)]
pub struct ModuleOutput {
    entity_reference: EntityReference,
    consumers: HashSet<(ModuleId, EntityType)>,
}
#[cfg(test)]
impl ModuleOutput {
    fn new(entity_reference: impl Into<String>) -> Self {
        ModuleOutput { entity_reference: entity_reference.into(), consumers: hashset!() }
    }

    /// Links the mod outputing this output to the given mod with id |module_id|.
    pub fn add_consumer(&mut self, module_id: impl Into<String>, entity_type: impl Into<String>) {
        self.consumers.insert((module_id.into(), entity_type.into()));
    }

    /// Unlinks the mod outputing this output and the mod with id |module_id|.
    pub fn remove_consumer(&mut self, module_id: &str) {
        self.consumers.retain(|(m, _)| m != module_id);
    }

    fn update_reference(&mut self, new_reference: impl Into<String>) {
        self.entity_reference = new_reference.into();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn session_graph() {
        let mut session_graph = SessionGraph::new();
        assert!(session_graph.stories.is_empty());
        assert!(session_graph.get_story_graph("story_x").is_none());
        assert!(session_graph.get_story_graph_mut("story_x").is_none());
        assert_eq!(session_graph.stories.len(), 0);

        session_graph.new_story("story_x");
        assert!(session_graph.get_story_graph("story_x").is_some());
        assert!(session_graph.get_story_graph_mut("story_x").is_some());
        assert_eq!(session_graph.stories.len(), 1);
    }

    #[test]
    fn story_graph() {
        let mut story_graph = StoryGraph::new();
        assert!(story_graph.modules.is_empty());

        let intent = Intent::new().with_action("SOME_ACTION");
        story_graph.add_module("mod-id", intent.clone());
        assert_eq!(story_graph.get_module_count(), 1);
        assert!(story_graph.modules.contains_key("mod-id"));
        assert!(story_graph.get_module_data_mut("mod-id").is_some());

        let module_data = story_graph.get_module_data("mod-id").unwrap();
        assert_eq!(module_data.last_intent, intent);
    }

    #[test]
    fn module_data() {
        let intent = Intent::new().with_action("SOME_ACTION");
        let mut module_data = ModuleData::new(intent.clone());
        assert_eq!(module_data.last_intent, intent);
        assert_eq!(module_data.created_timestamp, module_data.last_modified_timestamp);
        assert!(module_data.children.is_empty());
        assert!(module_data.outputs.is_empty());
        let created_timestamp = module_data.created_timestamp;
        let mut timestamps = vec![module_data.last_modified_timestamp];

        // Verify intents
        let new_intent = Intent::new().with_action("SOME_OTHER_ACTION");
        module_data.update_intent(new_intent.clone());
        assert_eq!(module_data.last_intent, new_intent);
        timestamps.push(module_data.last_modified_timestamp);

        // Verify children
        module_data.add_child("other-mod");
        assert!(module_data.children.contains("other-mod"));
        timestamps.push(module_data.last_modified_timestamp);

        module_data.remove_child("other-mod");
        assert!(module_data.children.is_empty());
        timestamps.push(module_data.last_modified_timestamp);

        // Verify outputs
        module_data.update_output("some-output", Some("some-ref".to_string()));
        let output = module_data.outputs.get("some-output").unwrap();
        assert_eq!(output.entity_reference, "some-ref");
        assert!(output.consumers.is_empty());
        timestamps.push(module_data.last_modified_timestamp);

        module_data.update_output("some-output", None);
        assert!(module_data.outputs.is_empty());
        timestamps.push(module_data.last_modified_timestamp);

        // Verify all timestamp changes are incremental and created timestamp wasn't updated.
        for i in 1..timestamps.len() {
            assert!(timestamps[i] > timestamps[i - 1]);
        }
        assert_eq!(module_data.created_timestamp, created_timestamp);
    }

    #[test]
    fn module_output() {
        let mut module_output = ModuleOutput::new("some-ref");
        assert!(module_output.consumers.is_empty());
        assert_eq!(module_output.entity_reference, "some-ref");

        module_output.add_consumer("some-consumer", "some-type");
        module_output.add_consumer("other-consumer", "some-type");
        assert_eq!(
            module_output.consumers,
            hashset!(
                ("some-consumer".to_string(), "some-type".to_string()),
                ("other-consumer".to_string(), "some-type".to_string())
            )
        );
        module_output.remove_consumer("some-consumer");
        assert_eq!(
            module_output.consumers,
            hashset!(("other-consumer".to_string(), "some-type".to_string()))
        );
    }
}
