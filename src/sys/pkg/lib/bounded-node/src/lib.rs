// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{Node, NumericProperty as _, Property as _, UintProperty},
    fuchsia_inspect_contrib::inspectable::InspectableU64,
    std::collections::VecDeque,
};

/// A `Node` with a bounded number of child `Node`s.
///
/// Child `Node`s at indices `[begin, end)` are guaranteed to be fully constructed.
/// ```
/// # use fuchsia_inspect::{assert_inspect_tree, Inspector},
/// # use bounded_node::BoundedNode,
///
/// let inspector = Inspector::new();
/// let mut bounded_node =
///     BoundedNode::from_node_and_capacity(inspector.root().create_child("bounded-node"), 2);
///
/// struct Item {
///     _node: Node,
/// }
/// bounded_node.push(|n| {
///     n.record_string("dropped-field", "dropped-value");
///     Item { _node: n }
/// });
/// bounded_node.push(|n| {
///     n.record_string("some-field", "some-value");
///     Item { _node: n }
/// });
/// bounded_node.push(|n| {
///     n.record_string("other-field", "other-value");
///     Item { _node: n }
/// });
///
/// assert_inspect_tree!(
///     inspector,
///     root: {
///         "bounded-node": {
///             "capacity": 2u64,
///             "begin": 1u64,
///             "end": 3u64,
///             "children": {
///                 "1": { "some-field": "some-value" },
///                 "2": { "other-field": "other-value" }
///             }
///         }
///     }
/// )
/// ```
#[derive(Debug)]
pub struct BoundedNode<V> {
    node: Node,
    children_node: Node,
    capacity: usize,
    begin: UintProperty,
    end: InspectableU64,
    vs: VecDeque<V>,
}

impl<V> BoundedNode<V> {
    /// Creates a `BoundedNode`. `BoundedNode`s with `capacity` zero do not store any `V`s.
    pub fn from_node_and_capacity(node: Node, capacity: usize) -> Self {
        node.record_uint("capacity", capacity as u64);
        let children_node = node.create_child("children");
        let begin = node.create_uint("begin", 0);
        let end = InspectableU64::new(0, &node, "end");
        Self { node, children_node, capacity, begin, end, vs: VecDeque::new() }
    }

    /// Push a child `Node` to the exported Inspect tree. `f` is called with the new `Node` and
    /// returns a type `V` that should contain that `Node` and any other desired Inspect objects.
    /// Drops oldest child `Node` if `capacity` would be exceeded.
    pub fn push(&mut self, f: impl FnOnce(Node) -> V) {
        if self.capacity == 0 {
            // Increment `begin` first so the valid range is always empty.
            self.begin.add(1);
            *self.end.get_mut() += 1;
            return;
        }
        let v = f(self.children_node.create_child(self.end.to_string()));
        // Increment `end` after pushing the new Node, to preserve the valid Node range invariant.
        self.vs.push_front(v);
        *self.end.get_mut() += 1;
        // Increment `begin` before (possibly) dropping the oldest Node.
        self.begin.set((*self.end).saturating_sub(self.capacity as u64));
        self.vs.truncate(self.capacity);
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fuchsia_inspect::{assert_inspect_tree, Inspector},
    };

    struct Item {
        _node: Node,
    }
    impl Item {
        fn new(node: Node, i: i64) -> Self {
            node.record_int("i", i);
            Self { _node: node }
        }
    }

    #[test]
    fn zero_capacity_push() {
        let inspector = Inspector::new();
        let mut bounded_node =
            BoundedNode::from_node_and_capacity(inspector.root().create_child("bounded-node"), 0);

        bounded_node.push(|n| Item::new(n, 0));

        assert_inspect_tree!(
            inspector,
            root: {
                "bounded-node": {
                    "capacity": 0u64,
                    "begin": 1u64,
                    "end": 1u64,
                    "children": {},
                }
            }
        )
    }

    #[test]
    fn push() {
        let inspector = Inspector::new();
        let mut bounded_node =
            BoundedNode::from_node_and_capacity(inspector.root().create_child("bounded-node"), 1);

        bounded_node.push(|n| Item::new(n, 0));

        assert_inspect_tree!(
            inspector,
            root: {
                "bounded-node": {
                    "capacity": 1u64,
                    "begin": 0u64,
                    "end": 1u64,
                    "children": {
                        "0": { i: 0i64 }
                    },
                }
            }
        )
    }

    #[test]
    fn push_triggers_drop_of_oldest() {
        let inspector = Inspector::new();
        let mut bounded_node =
            BoundedNode::from_node_and_capacity(inspector.root().create_child("bounded-node"), 3);

        for i in 0..4 {
            bounded_node.push(|n| Item::new(n, i));
        }

        assert_inspect_tree!(
            inspector,
            root: {
                "bounded-node": {
                    "capacity": 3u64,
                    "begin": 1u64,
                    "end": 4u64,
                    "children": {
                        "1": { i: 1i64 },
                        "2": { i: 2i64 },
                        "3": { i: 3i64 },
                    }
                }
            }
        )
    }
}
