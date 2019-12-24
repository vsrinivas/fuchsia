// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        mod_manager::ModManager, story_context_store::StoryContextStore,
        story_module::StoryModuleService,
    },
    anyhow::{Context as _, Error},
    fidl_fuchsia_app_discover::{DiscoverRegistryRequest, DiscoverRegistryRequestStream},
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

/// Handle DiscoveryRegistry requests.
pub async fn run_server(
    story_context_store: Arc<Mutex<StoryContextStore>>,
    mod_manager: Arc<Mutex<ModManager<StoryContextStore>>>,
    mut stream: DiscoverRegistryRequestStream,
) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("Error running discover registry")? {
        match request {
            DiscoverRegistryRequest::RegisterStoryModule { module, request, .. } => {
                let story_module_stream = request.into_stream()?;
                StoryModuleService::new(story_context_store.clone(), mod_manager.clone(), module)?
                    .spawn(story_module_stream);
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            story_context_store::{ContextEntity, ContextReader, Contributor},
            testing::{init_state, FakeEntityData, FakeEntityResolver},
        },
        fidl_fuchsia_app_discover::{DiscoverRegistryMarker, ModuleIdentifier, StoryModuleMarker},
        fidl_fuchsia_modular::{EntityResolverMarker, PuppetMasterMarker},
        fuchsia_async as fasync,
        maplit::hashset,
    };

    #[fasync::run_until_stalled(test)]
    async fn test_register_story_module() -> Result<(), Error> {
        // Initialize the fake entity resolver.
        let (entity_resolver, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<EntityResolverMarker>().unwrap();
        let mut fake_entity_resolver = FakeEntityResolver::new();
        fake_entity_resolver
            .register_entity("foo", FakeEntityData::new(vec!["some-type".into()], ""));
        fake_entity_resolver.spawn(request_stream);

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<DiscoverRegistryMarker>().unwrap();

        let (puppet_master_client, _) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();

        let (state, _, mod_manager) = init_state(puppet_master_client, entity_resolver);

        let story_context = state.clone();
        fasync::spawn_local(
            async move { run_server(story_context, mod_manager, request_stream).await }
                .unwrap_or_else(|e: Error| eprintln!("error running server {}", e)),
        );

        // Get the StoryModule connection.
        let (story_module_proxy, server_end) =
            fidl::endpoints::create_proxy::<StoryModuleMarker>()?;
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        assert!(client.register_story_module(module, server_end).is_ok());

        // Exercise the module output we got
        assert!(story_module_proxy.write_output("param-foo", Some("foo")).await.is_ok());

        // Verify state.
        let expected_entity = ContextEntity::new_test(
            "foo",
            hashset!["some-type".to_string()],
            hashset!(Contributor::module_new("story1", "mod-a", "param-foo")),
        );

        let context_store = state.lock();
        let result = context_store.current().collect::<Vec<&ContextEntity>>();
        assert_eq!(result.len(), 1);
        assert_eq!(result[0], &expected_entity);
        Ok(())
    }
}
