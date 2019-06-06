// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    derivative::*,
    failure::Error,
    fidl_fuchsia_modular::{EntityMarker, EntityProxy, EntityResolverProxy},
    std::collections::{HashMap, HashSet},
};

type EntityReference = String;

pub struct StoryContextStore {
    context_entities: HashMap<EntityReference, ContextEntity>,
    contributor_to_refs: HashMap<Contributor, HashSet<EntityReference>>,
    entity_resolver: EntityResolverProxy,
}

#[derive(Derivative)]
#[derivative(Debug, Eq, PartialEq)]
pub struct ContextEntity {
    pub reference: EntityReference,
    pub contributors: HashSet<Contributor>,
    pub types: HashSet<String>,

    #[derivative(PartialEq = "ignore")]
    // Optional purely for testing purposes.
    entity: Option<EntityProxy>,
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
    fn new(reference: &str, entity: EntityProxy) -> Self {
        ContextEntity {
            reference: reference.into(),
            types: HashSet::new(),
            contributors: HashSet::new(),
            entity: Some(entity),
        }
    }

    #[cfg(test)]
    pub fn new_test(
        reference: &str,
        types: HashSet<String>,
        contributors: HashSet<Contributor>,
    ) -> Self {
        ContextEntity { reference: reference.to_string(), types, contributors, entity: None }
    }

    pub fn add_contributor(&mut self, contributor: Contributor) {
        self.contributors.insert(contributor);
    }

    fn merge_types(&mut self, types: Vec<String>) {
        self.types.extend(types.into_iter())
    }

    fn remove_contributor(&mut self, contributor: &Contributor) {
        self.contributors.remove(contributor);
    }
}

impl StoryContextStore {
    pub fn new(entity_resolver: EntityResolverProxy) -> Self {
        StoryContextStore {
            context_entities: HashMap::new(),
            contributor_to_refs: HashMap::new(),
            entity_resolver,
        }
    }

    pub fn get_reference(
        &self,
        story_id: &str,
        module_id: &str,
        parameter_name: &str,
    ) -> Option<&EntityReference> {
        let contributor = Contributor::module_new(story_id, module_id, parameter_name);
        // A module contributor will only have one reference at a time.
        self.contributor_to_refs.get(&contributor).and_then(|references| references.iter().next())
    }

    pub async fn contribute<'a>(
        &'a mut self,
        story_id: &'a str,
        module_id: &'a str,
        parameter_name: &'a str,
        reference: &'a str,
    ) -> Result<(), Error> {
        let contributor = Contributor::module_new(story_id, module_id, parameter_name);
        // Get the entity proxy client.
        let (entity_proxy, server_end) = fidl::endpoints::create_proxy::<EntityMarker>()?;
        self.entity_resolver.resolve_entity(reference, server_end)?;
        let types = await!(entity_proxy.get_types())?;

        // Remove previous contributions for this contributor.
        self.clear_contributor(&contributor);

        // Track new entity
        let context_entity = self
            .context_entities
            .entry(reference.to_string())
            .or_insert(ContextEntity::new(reference, entity_proxy));
        context_entity.add_contributor(contributor.clone());
        context_entity.merge_types(types);

        // Keep track of contributor => references removal.
        self.contributor_to_refs
            .entry(contributor)
            .or_insert(HashSet::new())
            .insert(reference.to_string());
        Ok(())
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
    pub fn current<'a>(&'a self) -> impl Iterator<Item = &'a ContextEntity> {
        self.context_entities.values()
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
    use {
        super::*,
        crate::testing::{FakeEntityData, FakeEntityResolver},
        fidl_fuchsia_modular::{EntityResolverMarker, EntityResolverProxy},
        fuchsia_async as fasync,
        maplit::{hashmap, hashset},
    };

    #[fasync::run_singlethreaded(test)]
    async fn contribute() -> Result<(), Error> {
        let mut story_context_store = StoryContextStore::new(test_entity_resolver());
        await!(init_context_store(&mut story_context_store))?;

        // Verify context store state
        let expected_context_entities = hashmap! {
            "foo".to_string() => ContextEntity::new_test("foo",
                hashset!("foo-type".to_string()),
                hashset!(
                    Contributor::module_new("story1", "mod-a", "param-foo"),
                    Contributor::module_new("story2", "mod-b", "param-baz"),
                )),
            "bar".to_string() => ContextEntity::new_test("bar",
                hashset!("bar-type".to_string()),
                hashset!(
                    Contributor::module_new("story1", "mod-a", "param-bar")
                )),
        };
        assert_eq!(story_context_store.context_entities, expected_context_entities);

        // Contributing the same entity shouldn't have an effect.
        await!(story_context_store.contribute("story1", "mod-a", "param-foo", "foo"))?;
        assert_eq!(story_context_store.context_entities, expected_context_entities);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn withdraw() -> Result<(), Error> {
        let mut story_context_store = StoryContextStore::new(test_entity_resolver());
        await!(init_context_store(&mut story_context_store))?;

        // Remove a few of them
        story_context_store.withdraw("story2", "mod-b", "param-baz");
        story_context_store.withdraw("story1", "mod-a", "param-bar");

        // Verify context store state
        let expected_context_entities = hashmap! {
            "foo".to_string() => ContextEntity::new_test(
                "foo",
                hashset!("foo-type".to_string()),
                hashset!(Contributor::module_new("story1", "mod-a", "param-foo")),
            ),
        };
        assert_eq!(story_context_store.context_entities, expected_context_entities);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn withdraw_all() -> Result<(), Error> {
        let mut story_context_store = StoryContextStore::new(test_entity_resolver());
        await!(init_context_store(&mut story_context_store))?;

        // Withdraw all context for one of the mods
        story_context_store.withdraw_all("story1", "mod-a");

        // Verify context store state
        let expected_context_entities = hashmap! {
            "foo".to_string() =>
            ContextEntity::new_test(
                "foo",
                hashset!("foo-type".to_string()),
                hashset!(Contributor::module_new("story2", "mod-b", "param-baz")),
            ),
        };
        assert_eq!(story_context_store.context_entities, expected_context_entities);
        Ok(())
    }

    fn test_entity_resolver() -> EntityResolverProxy {
        let (entity_resolver_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver
            .register_entity("foo", FakeEntityData::new(vec!["foo-type".into()], ""));
        fake_entity_resolver
            .register_entity("bar", FakeEntityData::new(vec!["bar-type".into()], ""));
        fake_entity_resolver.spawn(request_stream);
        entity_resolver_client
    }

    async fn init_context_store(story_context_store: &mut StoryContextStore) -> Result<(), Error> {
        await!(story_context_store.contribute("story1", "mod-a", "param-foo", "foo"))?;
        await!(story_context_store.contribute("story2", "mod-b", "param-baz", "foo"))?;
        await!(story_context_store.contribute("story1", "mod-a", "param-bar", "bar"))?;
        Ok(())
    }

}
