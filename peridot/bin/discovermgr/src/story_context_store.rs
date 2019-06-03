// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::{HashMap, HashSet};

type EntityReference = String;

pub struct StoryContextStore {
    context_entities: HashMap<EntityReference, ContextEntity>,
    contributor_to_refs: HashMap<Contributor, HashSet<EntityReference>>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct ContextEntity {
    reference: EntityReference,
    contributors: HashSet<Contributor>,
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub enum Contributor {
    ModuleContributor { story_id: String, module_id: String, parameter_name: String },
    // TODO(miguelfrde): Add group scoped contributors
}

impl Contributor {
    pub fn module_new(story_id: &str, module_id: &str, parameter_name: &str) -> Self {
        Contributor::ModuleContributor {
            story_id: story_id.to_string(),
            module_id: module_id.to_string(),
            parameter_name: parameter_name.to_string(),
        }
    }
}

impl ContextEntity {
    pub fn new(reference: &str, contributors: HashSet<Contributor>) -> Self {
        ContextEntity { reference: reference.to_string(), contributors: contributors }
    }

    pub fn add_contributor(&mut self, contributor: Contributor) {
        self.contributors.insert(contributor);
    }

    fn remove_contributor(&mut self, contributor: &Contributor) {
        self.contributors.remove(contributor);
    }
}

impl StoryContextStore {
    pub fn new() -> Self {
        StoryContextStore { context_entities: HashMap::new(), contributor_to_refs: HashMap::new() }
    }

    pub fn contribute(
        &mut self,
        story_id: &str,
        module_id: &str,
        parameter_name: &str,
        reference: &str,
    ) {
        let contributor = Contributor::module_new(story_id, module_id, parameter_name);
        self.clear_contributor(&contributor);
        self.context_entities
            .entry(reference.to_string())
            .or_insert(ContextEntity::new(reference, HashSet::new()))
            .add_contributor(contributor.clone());
        // Keep track of contributor => references removal.
        self.contributor_to_refs
            .entry(contributor)
            .or_insert(HashSet::new())
            .insert(reference.to_string());
    }

    pub fn withdraw(&mut self, story_id: &str, module_id: &str, parameter_name: &str) {
        let contributor = Contributor::ModuleContributor {
            story_id: story_id.to_string(),
            module_id: module_id.to_string(),
            parameter_name: parameter_name.to_string(),
        };
        self.clear_contributor(&contributor);
    }

    #[allow(dead_code)] // Will be used through the ContextManager
    pub fn withdraw_all(&mut self, story_id: &str, module_id: &str) {
        let contributors: Vec<Contributor> = self
            .contributor_to_refs
            .keys()
            .filter(|c| match c {
                Contributor::ModuleContributor { story_id: s, module_id: m, .. } => {
                    s == story_id && m == module_id
                }
            })
            .cloned()
            .collect();
        for contributor in contributors {
            self.clear_contributor(&contributor);
        }
    }

    /// Iterate over the context entities.
    #[allow(dead_code)] // Will be used through the SuggestionsManager. Currently in tests.
    pub fn current<'a>(&'a self) -> impl Iterator<Item = &'a ContextEntity> {
        self.context_entities.values().into_iter()
    }

    fn clear_contributor(&mut self, contributor: &Contributor) {
        self.contributor_to_refs.remove_entry(contributor).map(|(c, references)| {
            for reference in references {
                match self.context_entities.get_mut(&reference) {
                    None => continue,
                    Some(context_entity) => {
                        context_entity.remove_contributor(&c);
                        if context_entity.contributors.is_empty() {
                            self.context_entities.remove(&reference);
                        }
                    }
                }
            }
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use maplit::{hashmap, hashset};

    #[test]
    fn contribute() {
        let mut story_context_store = StoryContextStore::new();

        // Add a few references
        story_context_store.contribute("story1", "mod-a", "param-foo", "foo");
        story_context_store.contribute("story2", "mod-b", "param-baz", "foo");
        story_context_store.contribute("story1", "mod-a", "param-bar", "bar");

        // Verify context store state
        let expected_context_entities = hashmap! {
            "foo".to_string() => ContextEntity::new("foo", hashset!(
                    Contributor::module_new("story1", "mod-a", "param-foo"),
                    Contributor::module_new("story2", "mod-b", "param-baz")
            )),
            "bar".to_string() => ContextEntity::new("bar", hashset!(
                    Contributor::module_new("story1", "mod-a", "param-bar")
            )),
        };
        assert_eq!(story_context_store.context_entities, expected_context_entities);

        // Contributing the same entity shouldn't have an effect.
        story_context_store.contribute("story1", "mod-a", "param-foo", "foo");
        assert_eq!(story_context_store.context_entities, expected_context_entities);
    }

    #[test]
    fn withdraw() {
        let mut story_context_store = StoryContextStore::new();

        // Add a few references
        story_context_store.contribute("story1", "mod-a", "param-foo", "foo");
        story_context_store.contribute("story2", "mod-b", "param-baz", "foo");
        story_context_store.contribute("story1", "mod-a", "param-bar", "bar");

        // Remove a few of them
        story_context_store.withdraw("story2", "mod-b", "param-baz");
        story_context_store.withdraw("story1", "mod-a", "param-bar");

        // Verify context store state
        let expected_context_entities = hashmap! {
            "foo".to_string() => ContextEntity::new("foo", hashset!(
            Contributor::module_new("story1", "mod-a", "param-foo"),
                )),
        };
        assert_eq!(story_context_store.context_entities, expected_context_entities);
    }

    #[test]
    fn withdraw_all() {
        let mut story_context_store = StoryContextStore::new();

        // Add a few references
        story_context_store.contribute("story1", "mod-a", "param-foo", "foo");
        story_context_store.contribute("story2", "mod-b", "param-baz", "foo");
        story_context_store.contribute("story1", "mod-a", "param-bar", "bar");

        // Withdraw all context for one of the mods
        story_context_store.withdraw_all("story1", "mod-a");

        // Verify context store state
        let expected_context_entities = hashmap! {
            "foo".to_string() =>
            ContextEntity::new("foo", hashset!(
            Contributor::module_new("story2", "mod-b", "param-baz"))
                ),
        };
        assert_eq!(story_context_store.context_entities, expected_context_entities);
    }
}
