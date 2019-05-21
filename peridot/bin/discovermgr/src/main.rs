// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    crate::module_output::ModuleOutputWriterService,
    crate::story_context_store::StoryContextStore,
    failure::{Error, ResultExt},
    fidl_fuchsia_app_discover::{DiscoverRegistryRequest, DiscoverRegistryRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog as syslog,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

mod models;
mod module_output;
mod story_context_store;
mod utils;

// The directory name where the discovermgr FIDL services are exposed.
static SERVICE_DIRECTORY: &str = "public";

enum IncomingServices {
    DiscoverRegistry(DiscoverRegistryRequestStream),
    // TODO: add additional services
}

/// Handle DiscoveryRegistry requests.
async fn run_discover_registry_server(
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

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["discovermgr"])?;

    let story_context_store = Arc::new(Mutex::new(StoryContextStore::new()));

    let mut fs = ServiceFs::new_local();
    fs.dir(SERVICE_DIRECTORY).add_fidl_service(IncomingServices::DiscoverRegistry);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut =
        fs.for_each_concurrent(MAX_CONCURRENT, |IncomingServices::DiscoverRegistry(stream)| {
            run_discover_registry_server(story_context_store.clone(), stream)
                .unwrap_or_else(|e| syslog::fx_log_err!("{:?}", e))
        });

    await!(fut);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use failure::ResultExt;
    use fidl_fuchsia_app_discover::{
        DiscoverRegistryMarker, ModuleIdentifier, ModuleOutputWriterMarker,
    };
    use fuchsia_component::client;

    static COMPONENT_URL: &str = "fuchsia-pkg://fuchsia.com/discovermgr#meta/discovermgr.cmx";

    #[fasync::run_singlethreaded(test)]
    async fn test_module_output() -> Result<(), Error> {
        let launcher = client::launcher().context("Failed to open launcher service")?;
        let app = client::launch(&launcher, COMPONENT_URL.to_string(), None /* arguments */)
            .context("Failed to launch discovermgr")?;
        let discover_manager = app
            .connect_to_service::<DiscoverRegistryMarker>()
            .context("Failed to connect to DiscoverRegistry")?;

        let mod_scope = ModuleIdentifier {
            story_id: Some("test-story".to_string()),
            module_path: Some(vec!["test-mod".to_string()]),
        };
        let (module_output_proxy, server_end) =
            fidl::endpoints::create_proxy::<ModuleOutputWriterMarker>()?;
        assert!(discover_manager.register_module_output_writer(mod_scope, server_end).is_ok());

        let result = await!(module_output_proxy.write("test-param", Some("test-ref")))?;
        assert!(result.is_ok());
        Ok(())
    }
}
