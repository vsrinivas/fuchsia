// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        cloud_action_provider::get_cloud_actions,
        mod_manager::ModManager,
        story_context_store::StoryContextStore,
        story_manager::StoryManager,
        story_storage::{LedgerStorage, MemoryStorage, StoryStorage},
        suggestion_providers::{
            ActionSuggestionsProvider, ContextualSuggestionsProvider, PackageSuggestionsProvider,
            StorySuggestionsProvider,
        },
        suggestions_manager::SuggestionsManager,
        suggestions_service::SuggestionsService,
    },
    anyhow::{Context as _, Error},
    fidl_fuchsia_app_discover::{
        DiscoverRegistryRequestStream, SessionDiscoverContextRequestStream,
        SuggestionsRequestStream,
    },
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
mod constants;
mod discover_registry;
mod indexing;
mod local_action_provider;
mod mod_manager;
mod models;
mod session_context;
mod story_context_store;
mod story_graph;
mod story_manager;
mod story_module;
mod story_storage;
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
    SessionDiscoverContext(SessionDiscoverContextRequestStream),
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
    story_manager: Arc<Mutex<StoryManager>>,
    suggestions_manager: Arc<Mutex<SuggestionsManager>>,
    mod_manager: Arc<Mutex<ModManager<StoryContextStore>>>,
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
        IncomingServices::SessionDiscoverContext(stream) => {
            session_context::run_server(stream, story_manager).await
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["discovermgr"])?;

    let entity_resolver = connect_to_service::<EntityResolverMarker>()
        .context("failed to connect to entity resolver")?;
    let story_context_store = Arc::new(Mutex::new(StoryContextStore::new(entity_resolver)));

    let puppet_master =
        connect_to_service::<PuppetMasterMarker>().context("failed to connect to puppet master")?;

    let cloud_actions = get_cloud_actions().await.unwrap_or_else(|e| {
        fx_log_err!("Error fetching cloud actions index: {}", e);
        vec![]
    });
    let actions_arc = Arc::new(cloud_actions);

    let storage =
        LedgerStorage::new().map(|s| Box::new(s) as Box<dyn StoryStorage>).unwrap_or_else(|_| {
            fx_log_err!("Error in creating LedgerStorage, Use MemoryStorage instead");
            Box::new(MemoryStorage::new()) as Box<dyn StoryStorage>
        });
    let story_manager = Arc::new(Mutex::new(StoryManager::new(storage)));
    let mod_manager = Arc::new(Mutex::new(ModManager::new(
        story_context_store.clone(),
        puppet_master,
        story_manager.clone(),
        actions_arc.clone(),
    )));

    let mut suggestions_manager = SuggestionsManager::new(mod_manager.clone());
    suggestions_manager.register_suggestions_provider(Box::new(
        ContextualSuggestionsProvider::new(actions_arc.clone()),
    ));
    suggestions_manager
        .register_suggestions_provider(Box::new(ActionSuggestionsProvider::new(actions_arc)));
    suggestions_manager.register_suggestions_provider(Box::new(StorySuggestionsProvider::new(
        story_manager.clone(),
    )));
    suggestions_manager.register_suggestions_provider(Box::new(PackageSuggestionsProvider::new()));

    let suggestions_manager_ref = Arc::new(Mutex::new(suggestions_manager));

    let mut fs = ServiceFs::new_local();
    fs.dir(SERVICE_DIRECTORY)
        .add_fidl_service(IncomingServices::DiscoverRegistry)
        .add_fidl_service(IncomingServices::Suggestions)
        .add_fidl_service(IncomingServices::Lifecycle)
        .add_fidl_service(IncomingServices::SessionDiscoverContext);

    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |incoming_service_stream| {
        run_fidl_service(
            story_context_store.clone(),
            story_manager.clone(),
            suggestions_manager_ref.clone(),
            mod_manager.clone(),
            incoming_service_stream,
        )
        .unwrap_or_else(|e| fx_log_err!("{:?}", e))
    });

    fut.await;
    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::models::Action,
        fuchsia_async as fasync,
        std::{collections::HashSet, iter::FromIterator},
    };

    // Verify the logic for removing duplicates
    #[fasync::run_singlethreaded(test)]
    async fn test_duplicates() -> Result<(), Error> {
        let cloud_actions: Vec<Action> =
            serde_json::from_str(include_str!("../test_data/test_actions.json")).unwrap();
        // test_actions_dupes contains 1 duplicate and 1 new
        let local_actions: Vec<Action> =
            serde_json::from_str(include_str!("../test_data/test_actions_dupes.json")).unwrap();

        let cloud_actions_len = cloud_actions.len();
        // This is the logic used above
        let actions: Vec<Action> = HashSet::<Action>::from_iter(
            cloud_actions.into_iter().chain(local_actions.into_iter()),
        )
        .into_iter()
        .collect::<Vec<Action>>();

        // check if the new and duplicated are added/filtered
        assert_eq!(cloud_actions_len + 1, actions.len());

        Ok(())
    }
}
