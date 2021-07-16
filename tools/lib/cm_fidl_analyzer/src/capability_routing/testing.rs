// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_tree::{BuildTreeResult, ComponentTreeBuilder},
    cm_rust::{CapabilityDecl, ChildDecl, ComponentDecl, ExposeDecl, OfferDecl, UseDecl},
    fidl_fuchsia_sys2::StartupMode,
    std::collections::HashMap,
};

// Test helper functions for implementations of CapabilityRouteVerifier.

pub fn new_component_decl(
    uses: Vec<UseDecl>,
    exposes: Vec<ExposeDecl>,
    offers: Vec<OfferDecl>,
    capabilities: Vec<CapabilityDecl>,
    children: Vec<ChildDecl>,
) -> ComponentDecl {
    ComponentDecl {
        program: None,
        uses,
        exposes,
        offers,
        capabilities,
        children,
        collections: vec![],
        facets: None,
        environments: vec![],
    }
}

pub fn new_child_decl(name: &String, url: &String) -> ChildDecl {
    ChildDecl {
        name: name.to_string(),
        url: url.to_string(),
        startup: StartupMode::Lazy,
        environment: None,
        on_terminate: None,
    }
}

// Builds a `ComponentTree` with the structure `root -- child`, with the specified
// `UseDecl`s, `OfferDecl`s, and `CapabilityDecl`s at each of the two nodes.
pub fn build_two_node_tree(
    root_uses: Vec<UseDecl>,
    root_offers: Vec<OfferDecl>,
    root_declares: Vec<CapabilityDecl>,
    child_uses: Vec<UseDecl>,
    child_exposes: Vec<ExposeDecl>,
    child_declares: Vec<CapabilityDecl>,
) -> BuildTreeResult {
    let root_url = "root_url".to_string();
    let child_url = "child_url".to_string();
    let child_name = "child".to_string();

    let root_decl = new_component_decl(
        root_uses,
        vec![],
        root_offers,
        root_declares,
        vec![new_child_decl(&child_name, &child_url)],
    );
    let child_decl = new_component_decl(child_uses, child_exposes, vec![], child_declares, vec![]);

    let mut decls = HashMap::new();
    decls.insert(root_url.clone(), root_decl.clone());
    decls.insert(child_url, child_decl.clone());

    ComponentTreeBuilder::new(decls).build(root_url)
}
