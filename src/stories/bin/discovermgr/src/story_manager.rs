// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        models::AddModInfo,
        story_graph::{ModuleData, StoryGraph},
    },
    chrono::{Datelike, Timelike, Utc},
    fuchsia_syslog::macros::*,
    std::collections::HashMap,
    uuid::Uuid,
};

pub type StoryName = String;
pub type StoryTitle = String;

trait StoryStorage {
    // save a story graph with its name to storage
    fn insert_graph(&mut self, story_name: StoryName, story_graph: StoryGraph);
    // load a story graph from storage according to its name
    fn get_graph(&self, story_name: &StoryName) -> Option<&StoryGraph>;
    // return the number of saved stories
    fn get_story_num(&self) -> usize;
    // save the name and title of a story
    fn insert_name_title(&mut self, story_name: StoryName, story_title: StoryTitle);
    // return names and stories of all stories
    fn get_name_titles(&self) -> Vec<(&StoryName, &StoryTitle)>;
}

struct MemoryStorage {
    graph: HashMap<StoryName, StoryGraph>,
    story_name_index: HashMap<StoryName, StoryTitle>,
}

impl StoryStorage for MemoryStorage {
    fn insert_graph(&mut self, story_name: StoryName, story_graph: StoryGraph) {
        self.graph.insert(story_name, story_graph);
    }

    fn get_graph(&self, story_name: &StoryName) -> Option<&StoryGraph> {
        if self.graph.contains_key(story_name) {
            Some(&self.graph[story_name])
        } else {
            None
        }
    }

    fn get_story_num(&self) -> usize {
        self.graph.len()
    }

    fn insert_name_title(&mut self, story_name: StoryName, story_title: StoryTitle) {
        self.story_name_index.insert(story_name, story_title);
    }

    fn get_name_titles(&self) -> Vec<(&StoryName, &StoryTitle)> {
        self.story_name_index.iter().collect()
    }
}

impl MemoryStorage {
    fn new() -> Self {
        MemoryStorage { graph: HashMap::new(), story_name_index: HashMap::new() }
    }
}

/// Manage multiple story graphs to support restoring stories.
pub struct StoryManager {
    // save stories to memory, which can be replaced by storage to disk or ledger
    story_storage: MemoryStorage,
    current_story_name: StoryName,
    current_story_graph: StoryGraph,
}

impl StoryManager {
    pub fn new() -> Self {
        StoryManager {
            story_storage: MemoryStorage::new(),
            current_story_name: String::from(""),
            current_story_graph: StoryGraph::new(),
        }
    }

    // Save the current story graph to storage
    fn save_current_story_graph(&mut self) {
        if self.current_story_graph.get_module_count() > 0 {
            self.story_storage
                .insert_graph(self.current_story_name.clone(), self.current_story_graph.clone());
            let now = Utc::now();
            let story_title = format!(
                "a story from {:?} {:02}:{:02}:{:02}",
                now.weekday(),
                now.hour(),
                now.minute(),
                now.second(),
            ); // we will allow user to name their story in the future
            self.story_storage
                .insert_name_title(self.current_story_name.clone(), story_title.clone());
            // update the story_name_index
        }
    }

    // Restore the story in story_manager and return an iterator of modules in it
    pub fn restore_story_graph(
        &mut self,
        target_story_name: StoryName,
    ) -> impl Iterator<Item = &ModuleData> {
        if self.current_story_name != target_story_name {
            self.save_current_story_graph();
            self.current_story_name = target_story_name;
            self.current_story_graph =
                self.story_storage.get_graph(&self.current_story_name).cloned().unwrap_or_else(
                    || {
                        fx_log_err!("Unable to load story named {}", &self.current_story_name);
                        StoryGraph::new()
                    },
                );
        }
        self.current_story_graph.get_all_modules()
    }

    // Add the suggestion to the story graph and if starting a new story
    // then first save the story graph.
    pub fn add_to_story_graph(&mut self, action: &AddModInfo) {
        let ongoing_story_name = action.story_name().to_string();
        if self.current_story_name != ongoing_story_name {
            // changed to another story: save the previous one to storage
            // and load/create the next one
            // no need to save the story if its id is the default one
            self.save_current_story_graph();
            self.current_story_name = ongoing_story_name;
            self.current_story_graph = self
                .story_storage
                .get_graph(&self.current_story_name)
                .unwrap_or(&StoryGraph::new())
                .clone();
        }
        self.current_story_graph.add_module(Uuid::new_v4().to_string(), action.intent().clone());
    }

    // Return names and titles of saved stories.
    pub fn get_name_titles(&self) -> Vec<(&StoryName, &StoryTitle)> {
        self.story_storage.get_name_titles()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::models::{DisplayInfo, Intent, SuggestedAction, Suggestion},
    };
    #[test]
    fn add_to_story_graph_and_restore() {
        let mut story_manager = StoryManager::new();
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
                story_manager.add_to_story_graph(&action);
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }
        assert_eq!(story_manager.current_story_name, "story_name_1".to_string());
        assert_eq!(story_manager.current_story_graph.get_module_count(), 1);

        // changed to a new story_name_2
        match suggestion_2.action() {
            SuggestedAction::AddMod(action) => {
                story_manager.add_to_story_graph(&action);
            }
            SuggestedAction::RestoreStory(_) => {
                assert!(false);
            }
        }
        assert_eq!(story_manager.current_story_name, "story_name_2".to_string());
        // previous story_name_1 already saved
        assert_eq!(story_manager.story_storage.get_story_num(), 1);

        // restore the story_name_1
        let _modules = story_manager.restore_story_graph("story_name_1".to_string());
        drop(_modules);
        assert_eq!(story_manager.current_story_name, "story_name_1".to_string());
        // story_name_2 is also saved
        assert_eq!(story_manager.story_storage.get_story_num(), 2);
        assert_eq!(story_manager.current_story_graph.get_module_count(), 1);
    }
}
