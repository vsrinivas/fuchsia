// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*, cm_rust::ComponentDecl, fidl_fuchsia_data as fdata, std::collections::HashSet,
    std::sync::Arc,
};

/// Returns true if the given child realm (live or deleting) exists.
pub async fn has_child<'a>(realm: &'a Realm, moniker: &'a str) -> bool {
    realm.lock_state().await.get().all_child_realms().contains_key(&moniker.into())
}

/// Return the instance id of the given live child.
pub async fn get_instance_id<'a>(realm: &'a Realm, moniker: &'a str) -> u32 {
    realm.lock_state().await.get().get_live_child_instance_id(&moniker.into()).unwrap()
}

/// Return all monikers of the live children of the given `realm`.
pub async fn get_live_children(realm: &Realm) -> HashSet<PartialMoniker> {
    realm.lock_state().await.get().live_child_realms().map(|(m, _)| m.clone()).collect()
}

/// Return the child realm of the given `realm` with moniker `child`.
pub async fn get_live_child<'a>(realm: &'a Realm, child: &'a str) -> Arc<Realm> {
    realm.lock_state().await.get().get_live_child_realm(&child.into()).unwrap().clone()
}

/// Returns an empty component decl for an executable component.
pub fn default_component_decl() -> ComponentDecl {
    ComponentDecl {
        program: Some(fdata::Dictionary { entries: vec![] }),
        uses: Vec::new(),
        exposes: Vec::new(),
        offers: Vec::new(),
        children: Vec::new(),
        collections: Vec::new(),
        facets: None,
        storage: Vec::new(),
    }
}
