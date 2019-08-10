// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    crate::{
        cloud_action_provider::get_cloud_actions,
        local_action_provider::get_local_actions,
        mod_manager::ModManager,
        story_context_store::StoryContextStore,
        story_manager::StoryManager,
        suggestion_providers::{
            ContextualSuggestionsProvider, PackageSuggestionsProvider, StorySuggestionsProvider,
        },
        suggestions_manager::SuggestionsManager,
        suggestions_service::SuggestionsService,
    },
    failure::{Error, ResultExt},
    fidl_fuchsia_app_discover::{DiscoverRegistryRequestStream, SuggestionsRequestStream},
    fidl_fuchsia_modular::{
        EntityResolverMarker, LifecycleRequest, LifecycleRequestStream, PuppetMasterMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    fuchsia_syslog::{self as syslog, macros::*},
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

#[macro_use]
mod testing;
mod action_match;
mod cloud_action_provider;
mod discover_registry;
mod indexing;
mod local_action_provider;
mod mod_manager;
mod models;
mod module_output;
mod story_context_store;
mod story_graph;
mod story_manager;
mod suggestion_providers;
mod suggestions_manager;
mod suggestions_service;
mod utils;

// The directory name where the discovermgr FIDL services are exposed.
static SERVICE_DIRECTORY: &str = "svc";

enum IncomingServices {
    DiscoverRegistry(DiscoverRegistryRequestStream),
    Suggestions(SuggestionsRequestStream),
    Lifecycle(LifecycleRequestStream),
}

async fn run_lifecycle_server(mut stream: LifecycleRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await.context("Error running lifecycle")? {
        match request {
            LifecycleRequest::Terminate { .. } => {
                std::process::exit(0);
            }
        }
    }
    Ok(())
}

/// Handle incoming service requests
async fn run_fidl_service(
    story_context_store: Arc<Mutex<StoryContextStore>>,
    suggestions_manager: Arc<Mutex<SuggestionsManager>>,
    mod_manager: Arc<Mutex<ModManager>>,
    incoming_service_stream: IncomingServices,
) -> Result<(), Error> {
    match incoming_service_stream {
        IncomingServices::DiscoverRegistry(stream) => {
            discover_registry::run_server(story_context_store, mod_manager, stream).await
        }
        IncomingServices::Suggestions(stream) => {
            let mut service = SuggestionsService::new(story_context_store, suggestions_manager);
            service.handle_client(stream).await
        }
        IncomingServices::Lifecycle(stream) => run_lifecycle_server(stream).await,
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["discovermgr"])?;

    let puppet_master =
        connect_to_service::<PuppetMasterMarker>().context("failed to connect to puppet master")?;
    let story_manager = Arc::new(Mutex::new(StoryManager::new()));
    let mod_manager = Arc::new(Mutex::new(ModManager::new(puppet_master, story_manager.clone())));

    let mut suggestions_manager = SuggestionsManager::new(mod_manager.clone());
    suggestions_manager.register_suggestions_provider(Box::new(StorySuggestionsProvider::new(
        story_manager.clone(),
    )));
    suggestions_manager.register_suggestions_provider(Box::new(PackageSuggestionsProvider::new()));

    let mut actions = get_cloud_actions().await.unwrap_or_else(|e| {
        fx_log_err!("Error fetching cloud actions index: {}", e);
        vec![]
    });
    // If no cloud action, use local action.
    // Note: can't await! in the closure above because async_block! isn't yet ported
    if actions.is_empty() {
        actions = get_local_actions().await.unwrap_or_else(|e| {
            fx_log_err!("Error fetching local actions index: {}", e);
            vec![]
        });
    }
    suggestions_manager
        .register_suggestions_provider(Box::new(ContextualSuggestionsProvider::new(actions)));

    let suggestions_manager_ref = Arc::new(Mutex::new(suggestions_manager));

    let entity_resolver = connect_to_service::<EntityResolverMarker>()
        .context("failed to connect to entity resolver")?;
    let story_context_store = Arc::new(Mutex::new(StoryContextStore::new(entity_resolver)));

    let mut fs = ServiceFs::new_local();
    fs.dir(SERVICE_DIRECTORY)
        .add_fidl_service(IncomingServices::DiscoverRegistry)
        .add_fidl_service(IncomingServices::Suggestions)
        .add_fidl_service(IncomingServices::Lifecycle);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |incoming_service_stream| {
        run_fidl_service(
            story_context_store.clone(),
            suggestions_manager_ref.clone(),
            mod_manager.clone(),
            incoming_service_stream,
        )
        .unwrap_or_else(|e| fx_log_err!("{:?}", e))
    });

    fut.await;
    Ok(())
}
