// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{Node, Property, StringProperty};
use fuchsia_inspect_derive::Unit;
// use itertools::Itertools;
use std::collections::VecDeque;

/// Wrapper around [std::collections::VecDeque] that only holds [String]. Implements
/// [fuchsia_inspect_derive::Unit], which allows it to be written to inspect as a single property
/// with its value being a comma-separated list that's concatenation of all of the items in the
/// VecDeque.
///
/// To use this in a a structure that implements [fuchsia_inspect_derive::Inspect], wrap this in the
/// [fuchsia_inspect_derive::IValue] smart pointer and it will automatically update the value of the
/// inspect property when updated.
#[derive(Default)]
pub struct JoinableInspectVecDeque(pub VecDeque<String>);

impl JoinableInspectVecDeque {
    fn join(&self) -> String {
        self.0.iter().map(String::as_str).collect::<Vec<_>>().join(",")
    }
}

impl Unit for JoinableInspectVecDeque {
    type Data = StringProperty;

    fn inspect_create(&self, parent: &Node, name: impl AsRef<str>) -> Self::Data {
        parent.create_string(name.as_ref(), self.join())
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        data.set(&self.join());
    }
}

#[cfg(test)]
mod tests {
    use crate::joinable_inspect_vecdeque::JoinableInspectVecDeque;
    use fuchsia_inspect::{assert_data_tree, Inspector, Node};
    use fuchsia_inspect_derive::{IValue, Inspect, WithInspect};

    #[derive(Default, Inspect)]
    struct TestInspectWrapper {
        inspect_node: Node,
        pub test_vec_deque: IValue<JoinableInspectVecDeque>,
    }

    // Tests that adding and removing from JoinableInspectVecDeque updates the inspect tree.
    #[test]
    fn test_vec_deque() {
        let inspector = Inspector::new();

        let mut wrapper = TestInspectWrapper::default()
            .with_inspect(inspector.root(), "wrapper_node")
            .expect("Failed to attach wrapper_node");

        let _ = wrapper.test_vec_deque.as_mut().0.push_back("test1".to_string());
        let _ = wrapper.test_vec_deque.as_mut().0.push_back("test2".to_string());
        let _ = wrapper.test_vec_deque.as_mut().0.push_back("test3".to_string());

        let _ = wrapper.test_vec_deque.as_mut().0.pop_front();

        assert_data_tree!(inspector, root: {
            wrapper_node: {
                "test_vec_deque": "test2,test3",
            }
        });
    }
}
