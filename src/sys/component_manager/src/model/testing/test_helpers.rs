// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*, cm_rust::ComponentDecl, fidl_fuchsia_data as fdata, std::collections::HashSet,
    std::sync::Arc,
};

/// Return the child of the given realm.
pub async fn get_child<'a>(realm: &'a Realm, moniker: &'a str) -> Arc<Realm> {
    realm.lock_state().await.get().live_child_realms()[&moniker.into()].clone()
}

/// Return all monikers of the children of the given `realm`.
pub async fn get_children(realm: &Realm) -> HashSet<ChildMoniker> {
    realm.lock_state().await.get().live_child_realms().keys().cloned().collect()
}

/// Return the child realm of the given `realm` with moniker `child`.
pub async fn get_child_realm<'a>(realm: &'a Realm, child: &'a str) -> Arc<Realm> {
    realm.lock_state().await.get().live_child_realms()[&child.into()].clone()
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
