// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    crate::story_context_store::StoryContextStore,
    crate::suggestion_providers::PackageSuggestionsProvider,
    crate::suggestions_manager::SuggestionsManager,
    crate::suggestions_service::SuggestionsService,
    failure::{Error, ResultExt},
    fidl_fuchsia_app_discover::{DiscoverRegistryRequestStream, SuggestionsRequestStream},
    fidl_fuchsia_modular::PuppetMasterMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog as syslog,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

#[macro_use]
mod testing;
mod action_match;
mod discover_registry;
mod models;
mod module_output;
mod story_context_store;
mod suggestion_providers;
mod suggestions_manager;
mod suggestions_service;
mod utils;

// The directory name where the discovermgr FIDL services are exposed.
static SERVICE_DIRECTORY: &str = "public";

enum IncomingServices {
    DiscoverRegistry(DiscoverRegistryRequestStream),
    Suggestions(SuggestionsRequestStream),
}

/// Handle incoming service requests
async fn run_fidl_service(
    story_context_store: Arc<Mutex<StoryContextStore>>,
    suggestions_manager: Arc<Mutex<SuggestionsManager>>,
    incoming_service_stream: IncomingServices,
) -> Result<(), Error> {
    match incoming_service_stream {
        IncomingServices::DiscoverRegistry(stream) => {
            await!(discover_registry::run_server(story_context_store, stream))
        }
        IncomingServices::Suggestions(stream) => {
            let mut service = SuggestionsService::new(story_context_store, suggestions_manager);
            await!(service.handle_client(stream))
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["discovermgr"])?;

    let puppet_master =
        connect_to_service::<PuppetMasterMarker>().context("failed to connect to puppet master")?;

    let package_suggestions_provider = PackageSuggestionsProvider::new();
    let mut suggestions_manager = SuggestionsManager::new(puppet_master);
    suggestions_manager.register_suggestions_provider(Box::new(package_suggestions_provider));

    let suggestions_manager_ref = Arc::new(Mutex::new(suggestions_manager));
    let story_context_store = Arc::new(Mutex::new(StoryContextStore::new()));

    let mut fs = ServiceFs::new_local();
    fs.dir(SERVICE_DIRECTORY)
        .add_fidl_service(IncomingServices::DiscoverRegistry)
        .add_fidl_service(IncomingServices::Suggestions);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |incoming_service_stream| {
        run_fidl_service(
            story_context_store.clone(),
            suggestions_manager_ref.clone(),
            incoming_service_stream,
        )
        .unwrap_or_else(|e| fx_log_err!("{:?}", e))
    });

    await!(fut);
    Ok(())
}
