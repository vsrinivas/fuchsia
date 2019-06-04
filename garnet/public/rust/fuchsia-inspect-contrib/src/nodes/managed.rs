// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::nodes::NodeExt;

use fuchsia_inspect::vmo::{
    BytesProperty, DoubleProperty, IntProperty, Node, StringProperty, UintProperty,
};
use std::sync::Arc;

/// Node type intended for create-and-forget use case. The node holds all the properties and
/// children created under it so clients don't have to hold onto them.
pub struct ManagedNode {
    node: Arc<Node>,
    items: Vec<NodeValue>,
}

impl ManagedNode {
    pub fn new(node: Node) -> Self {
        Self { node: Arc::new(node), items: vec![] }
    }

    pub fn writer(&mut self) -> NodeWriter {
        NodeWriter::new(self.node.clone(), &mut self.items)
    }
}

enum NodeValue {
    Node(Arc<Node>),
    String(StringProperty),
    Bytes(BytesProperty),
    Uint(UintProperty),
    Int(IntProperty),
    Double(DoubleProperty),
}

pub struct NodeWriter<'c> {
    node: Arc<Node>,
    items: &'c mut Vec<NodeValue>,
}

impl<'c> NodeWriter<'c> {
    fn new(node: Arc<Node>, items: &'c mut Vec<NodeValue>) -> Self {
        Self { node, items }
    }

    pub fn create_string(&mut self, key: &str, value: &str) -> &mut Self {
        let val = self.node.create_string(key, value);
        self.items.push(NodeValue::String(val));
        self
    }

    pub fn create_time(&mut self, key: &str) -> &mut Self {
        let val = self.node.create_time(key);
        self.items.push(NodeValue::String(val.inner));
        self
    }

    pub fn create_bytes(&mut self, key: &str, value: &[u8]) -> &mut Self {
        let val = self.node.create_bytes(key, value);
        self.items.push(NodeValue::Bytes(val));
        self
    }

    pub fn create_uint(&mut self, key: &str, value: u64) -> &mut Self {
        let val = self.node.create_uint(key, value);
        self.items.push(NodeValue::Uint(val));
        self
    }

    pub fn create_int(&mut self, key: &str, value: i64) -> &mut Self {
        let val = self.node.create_int(key, value);
        self.items.push(NodeValue::Int(val));
        self
    }

    pub fn create_double(&mut self, key: &str, value: f64) -> &mut Self {
        let val = self.node.create_double(key, value);
        self.items.push(NodeValue::Double(val));
        self
    }

    pub fn create_child(&mut self, key: &str) -> NodeWriter {
        let child = Arc::new(self.node.create_child(key));
        self.items.push(NodeValue::Node(child.clone()));
        NodeWriter::new(child, self.items)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_inspect::{assert_inspect_tree, vmo::Inspector};

    #[test]
    fn test_node_writer() {
        let inspector = Inspector::new().unwrap();
        let mut node = ManagedNode::new(inspector.root().create_child("config"));
        node.writer()
            .create_string("str_key", "str_value")
            .create_bytes("bytes_key", &[1, 3, 3, 7])
            .create_uint("uint_key", 1)
            .create_child("child")
            .create_int("int_key", 2)
            .create_double("double_key", 3f64);

        assert_inspect_tree!(inspector, root: {
            config: {
                str_key: "str_value",
                bytes_key: vec![1, 3, 3, 7],
                uint_key: 1u64,
                child: {
                    int_key: 2i64,
                    double_key: 3f64,
                }
            }
        });
    }

}
