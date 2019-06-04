// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{mod_manager::ModManager, story_context_store::StoryContextStore, utils},
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_app_discover::{
        ModuleIdentifier, ModuleOutputWriterRequest, ModuleOutputWriterRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

/// The ModuleOutputWriter protocol implementation.
pub struct ModuleOutputWriterService {
    /// The story id to which the module belongs.
    story_id: String,

    /// The module id in story |story_id| to which the output belongs.
    module_id: String,

    /// Reference to the context store.
    story_context_store: Arc<Mutex<StoryContextStore>>,

    /// Reference to the intent re-issuing.
    mod_manager: Arc<Mutex<ModManager>>,
}

impl ModuleOutputWriterService {
    /// Create a new module writer instance from an identifier.
    pub fn new(
        story_context_store: Arc<Mutex<StoryContextStore>>,
        mod_manager: Arc<Mutex<ModManager>>,
        module: ModuleIdentifier,
    ) -> Result<Self, Error> {
        Ok(ModuleOutputWriterService {
            story_id: module.story_id.ok_or(format_err!("expected story id"))?,
            module_id: utils::encoded_module_path(
                module.module_path.ok_or(format_err!("expected mod path"))?,
            ),
            story_context_store,
            mod_manager,
        })
    }

    /// Handle a stream of ModuleOutputWriter requests.
    pub fn spawn(self, mut stream: ModuleOutputWriterRequestStream) {
        fasync::spawn_local(
            async move {
                while let Some(request) = await!(stream.try_next()).context(format!(
                    "Error running module output for {:?} {:?}",
                    self.story_id, self.module_id,
                ))? {
                    match request {
                        ModuleOutputWriterRequest::Write {
                            output_name,
                            entity_reference,
                            responder,
                        } => {
                            await!(self.handle_write(output_name, entity_reference))?;
                            responder.send(&mut Ok(()))?;
                        }
                    }
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| fx_log_err!("error serving module output {}", e)),
        )
    }

    /// Write to the given |entity_reference| to the context store and associate
    /// it to this module output |output_name|. If no entity reference is given,
    /// clear that output.
    async fn handle_write(
        &self,
        output_name: String,
        entity_reference: Option<String>,
    ) -> Result<(), Error> {
        // TODO(miguelfrde): verify the output_name matches an output in
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
                    await!(issuer_lock.replace(old_reference, &reference));
                }
                await!(context_store_lock.contribute(
                    &self.story_id,
                    &self.module_id,
                    &output_name,
                    &reference,
                ))?;
            }
            None => context_store_lock.withdraw(&self.story_id, &self.module_id, &output_name),
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{AddMod, Intent},
            story_context_store::{ContextEntity, Contributor},
            testing::{FakeEntityData, FakeEntityResolver, PuppetMasterFake},
        },
        fidl_fuchsia_app_discover::ModuleOutputWriterMarker,
        fidl_fuchsia_modular::{
            EntityResolverMarker, IntentParameter as FidlIntentParameter, IntentParameterData,
            PuppetMasterMarker, StoryCommand,
        },
        maplit::{hashmap, hashset},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_write() {
        let (puppet_master_client, _) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mod_manager = Arc::new(Mutex::new(ModManager::new(puppet_master_client)));

        let (entity_resolver, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver
            .register_entity("foo", FakeEntityData::new(vec!["some-type".into()], ""));
        fake_entity_resolver.spawn(request_stream);

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ModuleOutputWriterMarker>().unwrap();
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        let state = Arc::new(Mutex::new(StoryContextStore::new(entity_resolver)));
        ModuleOutputWriterService::new(state.clone(), mod_manager, module)
            .unwrap()
            .spawn(request_stream);

        // Write a module output.
        assert!(await!(client.write("param-foo", Some("foo"))).is_ok());

        // Verify we have one entity with the right contributor.
        {
            let context_store = state.lock();
            let result = context_store.current().collect::<Vec<&ContextEntity>>();
            let expected_entity = ContextEntity::new_test(
                "foo",
                hashset!("some-type".into()),
                hashset!(Contributor::module_new("story1", "mod-a", "param-foo",)),
            );
            assert_eq!(result.len(), 1);
            assert_eq!(result[0], &expected_entity);
        }

        // Write no entity to the same output. This should withdraw the entity.
        assert!(await!(client.write("param-foo", None)).is_ok());

        // Verify we have no values.
        let context_store = state.lock();
        let result = context_store.current().collect::<Vec<&ContextEntity>>();
        assert_eq!(result.len(), 0);
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
                assert_eq!(add_mod.mod_name, vec!["mod-a"]);
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
        let mut mod_manager = ModManager::new(puppet_master_client);
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "peridot-ref");
        let action = AddMod::new(intent, Some("story1".to_string()), Some("mod-a".to_string()));
        mod_manager.actions = hashmap!("peridot-ref".to_string() => hashset!(action));

        let mut context_store = StoryContextStore::new(entity_resolver);
        await!(context_store.contribute("story1", "mod-a", "artist", "peridot-ref"))?;
        let context_store_ref = Arc::new(Mutex::new(context_store));

        // Initialize service client and server.
        let mod_manager_ref = Arc::new(Mutex::new(mod_manager));
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ModuleOutputWriterMarker>().unwrap();
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        ModuleOutputWriterService::new(context_store_ref, mod_manager_ref, module)
            .unwrap()
            .spawn(request_stream);

        // Write a module output.
        assert!(await!(client.write("artist", Some("garnet-ref"))).is_ok());

        Ok(())
    }
}
