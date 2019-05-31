// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::module_output::ModuleOutputWriterService,
    crate::story_context_store::StoryContextStore,
    failure::{Error, ResultExt},
    fidl_fuchsia_app_discover::{DiscoverRegistryRequest, DiscoverRegistryRequestStream},
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

/// Handle DiscoveryRegistry requests.
pub async fn run_server(
    story_context_store: Arc<Mutex<StoryContextStore>>,
    mut stream: DiscoverRegistryRequestStream,
) -> Result<(), Error> {
    while let Some(request) =
        await!(stream.try_next()).context("Error running discover registry")?
    {
        match request {
            DiscoverRegistryRequest::RegisterModuleOutputWriter { module, request, .. } => {
                let module_output_stream = request.into_stream()?;
                ModuleOutputWriterService::new(story_context_store.clone(), module)?
                    .spawn(module_output_stream);
            }
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::story_context_store::{ContextEntity, Contributor};
    use fidl_fuchsia_app_discover::{
        DiscoverRegistryMarker, ModuleIdentifier, ModuleOutputWriterMarker,
    };
    use fuchsia_async as fasync;
    use maplit::hashset;

    #[fasync::run_until_stalled(test)]
    async fn test_register_module_output() -> Result<(), Error> {
        let state = Arc::new(Mutex::new(StoryContextStore::new()));

        // Initialize service client and server.
        let (client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<DiscoverRegistryMarker>().unwrap();
        let story_context = state.clone();
        fasync::spawn(
            async move { await!(run_server(story_context, request_stream)) }
                .unwrap_or_else(|e: Error| eprintln!("error running server {}", e)),
        );

        // Get the ModuleOutputWriter connection.
        let (module_output_proxy, server_end) =
            fidl::endpoints::create_proxy::<ModuleOutputWriterMarker>()?;
        let module = ModuleIdentifier {
            story_id: Some("story1".to_string()),
            module_path: Some(vec!["mod-a".to_string()]),
        };
        assert!(client.register_module_output_writer(module, server_end).is_ok());

        // Exercise the module output we got
        assert!(await!(module_output_proxy.write("param-foo", Some("foo"))).is_ok());

        // Verify state.
        let context_store = state.lock();
        let result = context_store.current().collect::<Vec<&ContextEntity>>();
        let expected_entity = ContextEntity::new(
            "foo",
            hashset!(Contributor::module_new("story1", "mod-a", "param-foo")),
        );
        assert_eq!(result.len(), 1);
        assert_eq!(result[0], &expected_entity);
        Ok(())
    }
}
