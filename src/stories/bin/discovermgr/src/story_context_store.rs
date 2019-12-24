// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{models::OutputConsumer, story_graph::Module, utils},
    anyhow::Error,
    derivative::*,
    fidl_fuchsia_modular::{EntityMarker, EntityProxy, EntityResolverProxy},
    fuchsia_syslog::macros::*,
    futures::future::LocalFutureObj,
    serde_json,
    std::collections::{HashMap, HashSet},
};

type EntityReference = String;

pub trait ContextReader {
    fn get_reference(
        &self,
        story_id: &str,
        module_id: &str,
        parameter_name: &str,
    ) -> Option<&EntityReference>;

    // Compose output_consumers given consumed entity reference
    // story id of consumer and type of consumed entity.
    fn get_output_consumers(
        &self,
        entity_reference: &str,
        consumer_story_id: &str,
        consume_type: &str,
    ) -> Vec<OutputConsumer>;

    fn current<'a>(&'a self) -> Box<dyn Iterator<Item = &'a ContextEntity> + 'a>;
}

pub trait ContextWriter {
    fn contribute<'a>(
        &'a mut self,
        story_id: &'a str,
        module_id: &'a str,
        parameter_name: &'a str,
        reference: &'a str,
    ) -> LocalFutureObj<'a, Result<(), Error>>;

    fn withdraw(&mut self, story_id: &str, module_id: &str, parameter_name: &str);

    fn withdraw_all(&mut self, story_id: &str, module_id: &str);

    fn clear_contributor(&mut self, contributor: &Contributor);

    // Restore context store according to modules in restored story graph.
    fn restore_from_story<'a>(
        &'a mut self,
        modules: &'a Vec<Module>,
        story_id: &'a str,
    ) -> LocalFutureObj<'a, Result<(), Error>>;
}

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
    pub async fn from_entity(
        entity: EntityProxy,
        contributors: HashSet<Contributor>,
    ) -> Result<Self, Error> {
        use std::iter::FromIterator;
        let reference = entity.get_reference().await?;
        let types = HashSet::from_iter(entity.get_types().await?.into_iter());
        Ok(ContextEntity { reference, types, contributors, entity: Some(entity) })
    }

    #[cfg(test)]
    pub fn new_test(
        reference: &str,
        types: HashSet<String>,
        contributors: HashSet<Contributor>,
    ) -> Self {
        ContextEntity { reference: reference.to_string(), types, contributors, entity: None }
    }

    pub async fn get_string_data<'a>(
        &'a self,
        path: Vec<&'a str>,
        entity_type: &'a str,
    ) -> Option<String> {
        if self.entity.is_none() {
            return None;
        }
        let data = self.entity.as_ref().unwrap().get_data(entity_type).await.ok()?;
        data.and_then(|buffer| {
            utils::vmo_buffer_to_string(buffer)
                .map_err(|e| {
                    fx_log_err!("Failed to fetch data for entity ref:{}", self.reference);
                    e
                })
                .ok()
        })
        .and_then(|data| serde_json::from_str::<serde_json::Value>(&data).ok())
        .and_then(|json_data| {
            let result =
                path.iter().fold(Some(&json_data), |data, part| data.and_then(|d| d.get(part)));
            // TODO: support numeric types as well.
            result.and_then(|r| r.as_str()).map(|s| s.to_string())
        })
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

    fn get_contributors_by_story_id<'a>(
        &'a self,
        target_story_id: &'a str,
    ) -> Vec<&'a Contributor> {
        self.contributors
            .iter()
            .filter(|Contributor::ModuleContributor { story_id, .. }| story_id == target_story_id)
            .collect()
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
}

impl ContextReader for StoryContextStore {
    fn get_reference(
        &self,
        story_id: &str,
        module_id: &str,
        parameter_name: &str,
    ) -> Option<&EntityReference> {
        let contributor = Contributor::module_new(story_id, module_id, parameter_name);
        // A module contributor will only have one reference at a time.
        self.contributor_to_refs.get(&contributor).and_then(|references| references.iter().next())
    }

    fn get_output_consumers(
        &self,
        entity_reference: &str,
        consumer_story_id: &str,
        consume_type: &str,
    ) -> Vec<OutputConsumer> {
        self.context_entities
            .get(entity_reference)
            .map(|context_entity| {
                context_entity
                    .get_contributors_by_story_id(consumer_story_id)
                    .iter()
                    .map(
                        |Contributor::ModuleContributor {
                             story_id: _,
                             module_id,
                             parameter_name,
                         }| {
                            OutputConsumer::new(
                                entity_reference,
                                module_id,
                                parameter_name,
                                consume_type,
                            )
                        },
                    )
                    .collect()
            })
            .unwrap_or(vec![])
    }

    /// Iterate over the context entities.
    fn current<'a>(&'a self) -> Box<dyn Iterator<Item = &'a ContextEntity> + 'a> {
        Box::new(self.context_entities.values())
    }
}

impl ContextWriter for StoryContextStore {
    fn contribute<'a>(
        &'a mut self,
        story_id: &'a str,
        module_id: &'a str,
        parameter_name: &'a str,
        reference: &'a str,
    ) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            let contributor = Contributor::module_new(story_id, module_id, parameter_name);
            // Get the entity proxy client.
            let (entity_proxy, server_end) = fidl::endpoints::create_proxy::<EntityMarker>()?;
            self.entity_resolver.resolve_entity(reference, server_end)?;
            let types = entity_proxy.get_types().await?;

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
        }))
    }

    fn withdraw(&mut self, story_id: &str, module_id: &str, parameter_name: &str) {
        let contributor = Contributor::ModuleContributor {
            story_id: story_id.to_string(),
            module_id: module_id.to_string(),
            parameter_name: parameter_name.to_string(),
        };
        self.clear_contributor(&contributor);
    }

    fn withdraw_all(&mut self, story_id: &str, module_id: &str) {
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

    fn restore_from_story<'a>(
        &'a mut self,
        modules: &'a Vec<Module>,
        story_id: &'a str,
    ) -> LocalFutureObj<'a, Result<(), Error>> {
        LocalFutureObj::new(Box::new(async move {
            for module in modules.iter() {
                for (output_name, module_output) in module.module_data.outputs.iter() {
                    self.contribute(
                        story_id,
                        &module.module_id,
                        output_name,
                        &module_output.entity_reference,
                    )
                    .await?;
                }
            }
            Ok(())
        }))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::Intent,
            story_graph::{Module, ModuleData},
            testing::{FakeEntityData, FakeEntityResolver},
        },
        fidl_fuchsia_modular::{EntityResolverMarker, EntityResolverProxy},
        fuchsia_async as fasync,
        maplit::{hashmap, hashset},
    };

    #[fasync::run_singlethreaded(test)]
    async fn contribute() -> Result<(), Error> {
        let mut story_context_store = StoryContextStore::new(test_entity_resolver());
        init_context_store(&mut story_context_store).await?;

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
        story_context_store.contribute("story1", "mod-a", "param-foo", "foo").await?;
        assert_eq!(story_context_store.context_entities, expected_context_entities);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn withdraw() -> Result<(), Error> {
        let mut story_context_store = StoryContextStore::new(test_entity_resolver());
        init_context_store(&mut story_context_store).await?;

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
        init_context_store(&mut story_context_store).await?;

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

    #[fasync::run_singlethreaded(test)]
    async fn get_string_data() -> Result<(), Error> {
        let entity_resolver = test_entity_resolver();
        let (entity_proxy, server_end) = fidl::endpoints::create_proxy::<EntityMarker>()?;
        entity_resolver.resolve_entity("foo", server_end)?;

        let context_entity = ContextEntity::from_entity(entity_proxy, hashset!()).await?;
        // Verify successful case
        assert_eq!(
            context_entity.get_string_data(vec!["a", "b", "c"], "foo-type").await,
            Some("1".to_string())
        );

        // Verify case with path that leads to an object, not a string node.
        assert_eq!(context_entity.get_string_data(vec!["a", "b"], "foo-type").await, None);

        // Verify case with unknown path
        assert_eq!(context_entity.get_string_data(vec!["a", "x", "c"], "foo-type").await, None);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn restore_from_story() -> Result<(), Error> {
        let mut story_context_store = StoryContextStore::new(test_entity_resolver());
        init_context_store(&mut story_context_store).await?;
        assert_eq!(story_context_store.context_entities.get("foo").unwrap().contributors.len(), 2);

        let mut module_data = ModuleData::new(Intent::new());
        module_data.update_output("param-foo", Some("foo".to_string()));
        let module = Module::new("some-mod", module_data);
        let modules = vec![module];
        story_context_store.restore_from_story(&modules, "some-story").await?;

        // Verify that a new contributor is added.
        assert_eq!(story_context_store.context_entities.get("foo").unwrap().contributors.len(), 3);

        Ok(())
    }

    fn test_entity_resolver() -> EntityResolverProxy {
        let (entity_resolver_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver.register_entity(
            "foo",
            FakeEntityData::new(
                vec!["foo-type".into()],
                include_str!("../test_data/nested-field.json"),
            ),
        );
        fake_entity_resolver
            .register_entity("bar", FakeEntityData::new(vec!["bar-type".into()], ""));
        fake_entity_resolver.spawn(request_stream);
        entity_resolver_client
    }

    async fn init_context_store(story_context_store: &mut StoryContextStore) -> Result<(), Error> {
        story_context_store.contribute("story1", "mod-a", "param-foo", "foo").await?;
        story_context_store.contribute("story2", "mod-b", "param-baz", "foo").await?;
        story_context_store.contribute("story1", "mod-a", "param-bar", "bar").await?;
        Ok(())
    }
}
