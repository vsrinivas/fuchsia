// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_fidl_analyzer::component_tree::{ComponentTree, ComponentTreeError},
    scrutiny::prelude::*,
    serde::{Deserialize, Serialize},
    std::collections::HashSet,
    uuid::Uuid,
};

#[derive(Deserialize, Serialize)]
pub struct V2ComponentTree {
    pub deps: HashSet<String>,
    // TODO(pesk): start serializing the `tree` field if/when cm_rust::ComponentDecl
    // implements Serialize.
    #[serde(skip)]
    pub tree: ComponentTree,
    pub errors: Vec<ComponentTreeError>,
}

impl V2ComponentTree {
    pub fn new(
        deps: HashSet<String>,
        tree: ComponentTree,
        errors: Vec<ComponentTreeError>,
    ) -> Self {
        Self { deps, tree, errors }
    }
}

impl DataCollection for V2ComponentTree {
    fn uuid() -> Uuid {
        Uuid::parse_str("42a56c22-8e21-592c-2601-35a1e0ad970c").unwrap()
    }
    fn collection_name() -> String {
        "Component V2 Tree Collection".to_string()
    }
    fn collection_description() -> String {
        "Contains a V2 topology map based on the root.cm and all v2 manifests found in the build."
            .to_string()
    }
}
