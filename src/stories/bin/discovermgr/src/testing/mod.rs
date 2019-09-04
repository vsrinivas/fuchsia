// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
pub use {
    crate::{
        mod_manager::ModManager, story_context_store::StoryContextStore,
        story_manager::StoryManager, story_storage::MemoryStorage,
    },
    fake_entity_resolver::*,
    fidl_fuchsia_modular::{EntityResolverProxy, PuppetMasterProxy},
    macros::*,
    parking_lot::Mutex,
    puppet_master_fake::*,
    std::sync::Arc,
    suggestion_providers::*,
};

#[macro_use]
pub mod macros;
pub mod fake_entity_resolver;
pub mod puppet_master_fake;
pub mod suggestion_providers;

/// Generate Initialized story_context_store, story_manager and mod_manager.
pub fn common_initialization(
    puppet_master_client: PuppetMasterProxy,
    entity_resolver: EntityResolverProxy,
) -> (Arc<Mutex<StoryContextStore>>, Arc<Mutex<StoryManager>>, Arc<Mutex<ModManager>>) {
    let story_context_store = Arc::new(Mutex::new(StoryContextStore::new(entity_resolver)));
    let story_manager = Arc::new(Mutex::new(StoryManager::new(Box::new(MemoryStorage::new()))));
    let mod_manager = Arc::new(Mutex::new(ModManager::new(
        story_context_store.clone(),
        puppet_master_client,
        story_manager.clone(),
        Arc::new(vec![]),
    )));
    (story_context_store, story_manager, mod_manager)
}
