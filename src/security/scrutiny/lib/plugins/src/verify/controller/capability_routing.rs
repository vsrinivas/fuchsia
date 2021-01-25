// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::verify::collection::V2ComponentTree,
    anyhow::Result,
    cm_fidl_analyzer::component_tree::{
        BreadthFirstWalker, ComponentNode, ComponentNodeVisitor, ComponentTreeWalker,
    },
    scrutiny::{model::controller::DataController, model::model::*},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct TreeMappingController {}

// A placeholder ComponentNodeVisitor which just records the node path of each node visited,
// together with its component URL.
#[derive(Default)]
struct TreeMappingVisitor {
    pub visited: Vec<Value>,
}

impl ComponentNodeVisitor for TreeMappingVisitor {
    fn visit_node(&mut self, node: &ComponentNode) -> Result<()> {
        self.visited.push(json!({"node": node.short_display(), "url": node.url()}));
        Ok(())
    }
}

impl DataController for TreeMappingController {
    fn query(&self, model: Arc<DataModel>, _value: Value) -> Result<Value> {
        let tree = &model.get::<V2ComponentTree>()?.tree;
        let mut walker = BreadthFirstWalker::new(&tree)?;
        let mut visitor = TreeMappingVisitor::default();
        walker.walk(&tree, &mut visitor)?;

        return Ok(json!({"route": visitor.visited}));
    }

    fn description(&self) -> String {
        "a placeholder analyzer which walks the full v2 component tree and reports its route"
            .to_string()
    }
}
