// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::component_tree::{BuildTreeResult, ComponentTreeBuilder, NodePath},
    cm_rust::{
        CapabilityDecl, CapabilityPath, ChildDecl, ComponentDecl, DependencyType, DirectoryDecl,
        ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget, OfferDecl,
        OfferDirectoryDecl, OfferProtocolDecl, OfferSource, OfferTarget, ProtocolDecl, UseDecl,
        UseDirectoryDecl, UseProtocolDecl, UseSource,
    },
    fidl_fuchsia_io2 as fio2,
    fidl_fuchsia_sys2::StartupMode,
    moniker::PartialChildMoniker,
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

pub fn node_path(parts: Vec<&str>) -> NodePath {
    NodePath::new(parts.iter().map(|&s| PartialChildMoniker::new(s.to_string(), None)).collect())
}

pub fn empty_capability_path() -> CapabilityPath {
    CapabilityPath { dirname: "".to_string(), basename: "".to_string() }
}

pub fn default_use_dir() -> UseDirectoryDecl {
    UseDirectoryDecl {
        source: UseSource::Parent,
        source_name: "default_source".into(),
        target_path: empty_capability_path(),
        rights: fio2::Operations::Connect,
        subdir: None,
        dependency_type: DependencyType::Strong,
    }
}

pub fn default_offer_dir() -> OfferDirectoryDecl {
    OfferDirectoryDecl {
        source: OfferSource::Parent,
        source_name: "default_source".into(),
        target: OfferTarget::static_child("default_child".to_string()),
        target_name: "default_target".into(),
        rights: Some(fio2::Operations::Connect),
        subdir: None,
        dependency_type: DependencyType::Strong,
    }
}

pub fn default_expose_dir() -> ExposeDirectoryDecl {
    ExposeDirectoryDecl {
        source: ExposeSource::Self_,
        source_name: "default_source".into(),
        target: ExposeTarget::Parent,
        target_name: "default_target".into(),
        rights: Some(fio2::Operations::Connect),
        subdir: None,
    }
}

pub fn default_declare_dir() -> DirectoryDecl {
    DirectoryDecl {
        name: "default_name".into(),
        source_path: None,
        rights: fio2::Operations::Connect,
    }
}

pub fn default_use_protocol() -> UseProtocolDecl {
    UseProtocolDecl {
        source: UseSource::Parent,
        source_name: "default_source".into(),
        target_path: empty_capability_path(),
        dependency_type: DependencyType::Strong,
    }
}

pub fn default_offer_protocol() -> OfferProtocolDecl {
    OfferProtocolDecl {
        source: OfferSource::Parent,
        source_name: "default_source".into(),
        target: OfferTarget::static_child("default_child".to_string()),
        target_name: "default_target".into(),
        dependency_type: DependencyType::Strong,
    }
}

pub fn default_expose_protocol() -> ExposeProtocolDecl {
    ExposeProtocolDecl {
        source: ExposeSource::Self_,
        source_name: "default_source".into(),
        target: ExposeTarget::Parent,
        target_name: "default_target".into(),
    }
}

pub fn default_declare_protocol() -> ProtocolDecl {
    ProtocolDecl { name: "default_name".into(), source_path: None }
}
