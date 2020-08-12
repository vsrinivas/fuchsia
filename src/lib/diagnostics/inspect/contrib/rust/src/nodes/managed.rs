// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::nodes::NodeExt;

use fuchsia_inspect::{
    BoolProperty, BytesProperty, DoubleProperty, IntProperty, Node, StringProperty, UintProperty,
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

    pub fn writer(&mut self) -> NodeWriter<'_> {
        NodeWriter::new(self.node.clone(), &mut self.items)
    }
}

enum NodeValue {
    Node(Arc<Node>),
    String(StringProperty),
    Bool(BoolProperty),
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

    pub fn create_string(&mut self, key: impl AsRef<str>, value: impl AsRef<str>) -> &mut Self {
        let val = self.node.create_string(key.as_ref(), value.as_ref());
        self.items.push(NodeValue::String(val));
        self
    }

    pub fn create_time(&mut self, key: impl AsRef<str>) -> &mut Self {
        let val = self.node.create_time(key.as_ref());
        self.items.push(NodeValue::String(val.inner));
        self
    }

    pub fn create_bool(&mut self, key: impl AsRef<str>, value: bool) -> &mut Self {
        let val = self.node.create_bool(key.as_ref(), value);
        self.items.push(NodeValue::Bool(val));
        self
    }

    pub fn create_bytes(&mut self, key: impl AsRef<str>, value: impl AsRef<[u8]>) -> &mut Self {
        let val = self.node.create_bytes(key.as_ref(), value.as_ref());
        self.items.push(NodeValue::Bytes(val));
        self
    }

    pub fn create_uint(&mut self, key: impl AsRef<str>, value: u64) -> &mut Self {
        let val = self.node.create_uint(key.as_ref(), value);
        self.items.push(NodeValue::Uint(val));
        self
    }

    pub fn create_int(&mut self, key: impl AsRef<str>, value: i64) -> &mut Self {
        let val = self.node.create_int(key.as_ref(), value);
        self.items.push(NodeValue::Int(val));
        self
    }

    pub fn create_double(&mut self, key: impl AsRef<str>, value: f64) -> &mut Self {
        let val = self.node.create_double(key.as_ref(), value);
        self.items.push(NodeValue::Double(val));
        self
    }

    pub fn create_child(&mut self, key: impl AsRef<str>) -> NodeWriter<'_> {
        let child = Arc::new(self.node.create_child(key.as_ref()));
        self.items.push(NodeValue::Node(child.clone()));
        NodeWriter::new(child, self.items)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_inspect::{assert_inspect_tree, Inspector};

    #[test]
    fn test_node_writer() {
        let inspector = Inspector::new();
        let mut node = ManagedNode::new(inspector.root().create_child("config"));
        node.writer()
            .create_string("str_key", "str_value")
            .create_bool("bool_key", true)
            .create_bytes("bytes_key", &[1, 3, 3, 7])
            .create_uint("uint_key", 1)
            .create_child("child")
            .create_int("int_key", 2)
            .create_double("double_key", 3f64);

        assert_inspect_tree!(inspector, root: {
            config: {
                str_key: "str_value",
                bool_key: true,
                bytes_key: vec![1u8, 3, 3, 7],
                uint_key: 1u64,
                child: {
                    int_key: 2i64,
                    double_key: 3f64,
                }
            }
        });
    }

    #[test]
    fn test_node_writer_with_owned_types() {
        let inspector = Inspector::new();
        let mut node = ManagedNode::new(inspector.root().create_child("config"));
        node.writer()
            .create_string("str_key".to_string(), "str_value".to_string())
            .create_bool("bool_key", true)
            .create_bytes("bytes_key".to_string(), vec![1, 3, 3, 7])
            .create_uint("uint_key".to_string(), 1)
            .create_child("child".to_string())
            .create_int("int_key".to_string(), 2)
            .create_double("double_key".to_string(), 3f64);

        assert_inspect_tree!(inspector, root: {
            config: {
                str_key: "str_value",
                bool_key: true,
                bytes_key: vec![1u8, 3, 3, 7],
                uint_key: 1u64,
                child: {
                    int_key: 2i64,
                    double_key: 3f64,
                }
            }
        });
    }
}
