// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        mod_manager::ModManager,
        models::AddModInfo,
        story_context_store::{ContextReader, ContextWriter, Contributor},
        utils,
    },
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_app_discover::{
        ModuleIdentifier, StoryDiscoverError, StoryModuleRequest, StoryModuleRequestStream,
    },
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_modular::Intent,
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

/// The StoryModule protocol implementation.
pub struct StoryModuleService<T> {
    /// The story id to which the module belongs.
    story_id: String,

    /// The module id in story |story_id| to which the output belongs.
    module_id: String,

    /// Reference to the context store.
    story_context_store: Arc<Mutex<T>>,

    /// Reference to the intent re-issuing.
    mod_manager: Arc<Mutex<ModManager<T>>>,
}

impl<T: ContextReader + ContextWriter + 'static> StoryModuleService<T> {
    /// Create a new module writer instance from an identifier.
    pub fn new(
        story_context_store: Arc<Mutex<T>>,
        mod_manager: Arc<Mutex<ModManager<T>>>,
        module: ModuleIdentifier,
    ) -> Result<Self, Error> {
        Ok(StoryModuleService {
            story_id: module.story_id.ok_or(format_err!("expected story id"))?,
            module_id: utils::encoded_module_path(
                module.module_path.ok_or(format_err!("expected mod path"))?,
            ),
            story_context_store,
            mod_manager,
        })
    }

    /// Handle a stream of StoryModule requests.
    pub fn spawn(self, mut stream: StoryModuleRequestStream) {
        fasync::spawn_local(
            async move {
                while let Some(request) = stream.try_next().await.context(format!(
                    "Error running module output for {:?} {:?}",
                    self.story_id, self.module_id,
                ))? {
                    match request {
                        StoryModuleRequest::WriteOutput {
                            output_name,
                            entity_reference,
                            responder,
                        } => {
                            self.handle_write(output_name, entity_reference).await?;
                            responder.send(&mut Ok(()))?;
                        }
                        StoryModuleRequest::IssueIntent { intent, mod_name, responder } => {
                            self.handle_add_to_story(intent, mod_name).await?;
                            responder.send()?;
                            // TODO: bind controller.
                        }
                        StoryModuleRequest::WriteInstanceState { key, value, responder } => {
                            let mut result = self.handle_write_instance_state(&key, value).await;
                            responder.send(&mut result)?;
                        }
                        StoryModuleRequest::ReadInstanceState { key, responder } => {
                            let mut result = self.handle_read_instance_state(&key).await;
                            responder.send(&mut result)?;
                        }
                    }
                }
                self.story_context_store.lock().withdraw_all(&self.story_id, &self.module_id);
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("error serving module output {}", e)),
        )
    }

    async fn handle_add_to_story(&self, intent: Intent, mod_name: String) -> Result<(), Error> {
        let mut mod_manager = self.mod_manager.lock();
        let action = AddModInfo::new(intent.into(), Some(self.story_id.clone()), Some(mod_name));
        mod_manager.issue_action(&action, /*focus=*/ true).await
    }

    /// Write to the given |entity_reference| to the context store and associate
    /// it to this module output |output_name|. If no entity reference is given,
    /// clear that output.
    async fn handle_write(
        &self,
        output_name: String,
        entity_reference: Option<String>,
    ) -> Result<(), Error> {
        // TODO: verify the output_name matches an output in
        // the manifest.
        fx_log_info!(
            "Got write for parameter name:{}, story:{}, mod:{:?} reference:{:?}",
            output_name,
            self.story_id,
            self.module_id,
            entity_reference,
        );
        let mut context_store_lock = self.story_context_store.lock();
        match entity_reference {
            Some(reference) => {
                if let Some(old_reference) =
                    context_store_lock.get_reference(&self.story_id, &self.module_id, &output_name)
                {
                    let mut issuer_lock = self.mod_manager.lock();
                    issuer_lock
                        .replace(
                            old_reference,
                            &reference,
                            Contributor::module_new(&self.story_id, &self.module_id, &output_name),
                        )
                        .await?;
                }
                context_store_lock
                    .contribute(&self.story_id, &self.module_id, &output_name, &reference)
                    .await?;
            }
            None => context_store_lock.withdraw(&self.story_id, &self.module_id, &output_name),
        }
        Ok(())
    }

    async fn handle_write_instance_state(
        &self,
        key: &str,
        value: Buffer,
    ) -> Result<(), StoryDiscoverError> {
        let mod_manager = self.mod_manager.lock();
        let mut story_manager = mod_manager.story_manager.lock();
        story_manager
            .set_instance_state(
                &self.story_id,
                &self.module_id,
                key,
                utils::vmo_buffer_to_string(Box::new(value))
                    .map_err(|_| StoryDiscoverError::VmoStringConversion)?,
            )
            .await
    }

    async fn handle_read_instance_state(&self, key: &str) -> Result<Buffer, StoryDiscoverError> {
        let mod_manager = self.mod_manager.lock();
        let story_manager = mod_manager.story_manager.lock();
        let state_string =
            story_manager.get_instance_state(&self.story_id, &self.module_id, &key).await?;
        utils::string_to_vmo_buffer(state_string)
            .map_err(|_| StoryDiscoverError::VmoStringConversion)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{AddModInfo, Intent},
            story_context_store::{ContextEntity, Contributor},
            testing::{init_state, FakeEntityData, FakeEntityResolver, PuppetMasterFake},
        },
        fidl_fuchsia_app_discover::StoryModuleMarker,
        fidl_fuchsia_modular::{
            EntityResolverMarker, IntentParameter as FidlIntentParameter, IntentParameterData,
            PuppetMasterMarker, StoryCommand,
        },
        maplit::{hashmap, hashset},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_write() {
        let (entity_resolver, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver
            .register_entity("foo", FakeEntityData::new(vec!["some-type".into()], ""));
        fake_entity_resolver
            .register_entity("bar", FakeEntityData::new(vec!["some-type-bar".into()], ""));
        fake_entity_resolver.spawn(request_stream);

        let (puppet_master_client, _) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let (state, _, mod_manager) = init_state(puppet_master_client, entity_resolver);

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoryModuleMarker>().unwrap();
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };

        StoryModuleService::new(state.clone(), mod_manager, module).unwrap().spawn(request_stream);

        // Write a module output.
        assert!(client.write_output("param-foo", Some("foo")).await.is_ok());
        assert!(client.write_output("param-bar", Some("bar")).await.is_ok());

        // Verify we have two entities with the right contributor.
        {
            let context_store = state.lock();
            let result = context_store.current().collect::<Vec<&ContextEntity>>();
            let expected_entities = vec![
                ContextEntity::new_test(
                    "bar",
                    hashset!("some-type-bar".into()),
                    hashset!(Contributor::module_new("story1", "mod-a", "param-bar",)),
                ),
                ContextEntity::new_test(
                    "foo",
                    hashset!("some-type".into()),
                    hashset!(Contributor::module_new("story1", "mod-a", "param-foo",)),
                ),
            ];
            assert_eq!(result.len(), 2);
            assert!(result.iter().all(|r| expected_entities.iter().any(|e| e == *r)))
        }

        // Write no entity to the same output. This should withdraw the entity.
        assert!(client.write_output("param-foo", None).await.is_ok());

        // Verify we have only one entity.
        let context_store = state.lock();
        let result = context_store.current().collect::<Vec<&ContextEntity>>();
        assert_eq!(result.len(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn reissue_intents() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, puppet_master_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        let (entity_resolver, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver
            .register_entity("garnet-ref", FakeEntityData::new(vec!["some-type".into()], ""));
        fake_entity_resolver
            .register_entity("peridot-ref", FakeEntityData::new(vec!["some-type".into()], ""));
        fake_entity_resolver.spawn(request_stream);

        // This will be called with the action of the old reference but with
        // the replaced entity reference.
        puppet_master_fake.set_on_execute("story1", |commands| {
            assert_eq!(commands.len(), 1);
            if let StoryCommand::AddMod(add_mod) = &commands[0] {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(add_mod.mod_name_transitional, Some("mod-a".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![FidlIntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("garnet-ref".to_string()),
                    },])
                );
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(puppet_master_request_stream);

        // Set initial state of connected mods. The actions here will be executed with the new
        // entity reference in the parameter.
        let (context_store_ref, _, mod_manager_ref) =
            init_state(puppet_master_client, entity_resolver);
        {
            let mut context_store = context_store_ref.lock();
            context_store.contribute("story1", "mod-a", "artist", "peridot-ref").await?;
            let mut mod_manager = mod_manager_ref.lock();
            let intent =
                Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "peridot-ref");
            let action =
                AddModInfo::new(intent, Some("story1".to_string()), Some("mod-a".to_string()));
            mod_manager.actions = hashmap!("peridot-ref".to_string() => hashset!(action));
        }

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoryModuleMarker>().unwrap();
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        StoryModuleService::new(context_store_ref, mod_manager_ref, module)
            .unwrap()
            .spawn(request_stream);

        // Write a module output.
        assert!(client.write_output("artist", Some("garnet-ref")).await.is_ok());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn issue_intent() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, puppet_master_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        let (entity_resolver, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver
            .register_entity("garnet-ref", FakeEntityData::new(vec!["some-type".into()], ""));
        fake_entity_resolver.spawn(request_stream);

        // This will be called with the action of the old reference but with
        // the replaced entity reference.
        puppet_master_fake.set_on_execute("story1", |commands| {
            assert_eq!(commands.len(), 3);
            if let (
                StoryCommand::AddMod(add_mod),
                StoryCommand::SetFocusState(set_focus),
                StoryCommand::FocusMod(focus_mod),
            ) = (&commands[0], &commands[1], &commands[2])
            {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(add_mod.mod_name_transitional, Some("mod-b".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![FidlIntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("garnet-ref".to_string()),
                    },])
                );
                assert!(set_focus.focused);
                assert_eq!(add_mod.mod_name_transitional, focus_mod.mod_name_transitional);
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(puppet_master_request_stream);

        // Initialize service client and server.
        let (context_store, _, mod_manager) = init_state(puppet_master_client, entity_resolver);
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoryModuleMarker>().unwrap();
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        StoryModuleService::new(context_store, mod_manager, module).unwrap().spawn(request_stream);

        // Write a module output.
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "garnet-ref");
        assert!(client.issue_intent(&mut intent.into(), "mod-b").await.is_ok());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_instance_state() -> Result<(), Error> {
        let (puppet_master_client, _) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let (entity_resolver, _) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoryModuleMarker>().unwrap();
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        let (state, _, mod_manager) = init_state(puppet_master_client, entity_resolver);
        StoryModuleService::new(state.clone(), mod_manager, module).unwrap().spawn(request_stream);

        // Write instance state.
        assert!(client
            .write_instance_state("query", &mut utils::string_to_vmo_buffer("cities in spain")?)
            .await
            .is_ok());

        // Read instance state.
        let state_string = utils::vmo_buffer_to_string(Box::new(
            client.read_instance_state("query").await?.unwrap(),
        ))?;
        assert_eq!(state_string, "cities in spain".to_string());
        assert!(client.read_instance_state("other_state_key").await?.is_err());
        Ok(())
    }
}
