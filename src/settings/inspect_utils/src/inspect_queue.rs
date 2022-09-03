// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{self as inspect, Node};
use fuchsia_inspect_derive::{AttachError, Inspect};
use std::collections::VecDeque;

/// A vector of items to be written to inspect. It will only keep the last [size_limit]
/// items. It is compatible with inspect_derive.
#[derive(Default)]
pub struct InspectQueue<T> {
    items: VecDeque<T>,
    // Required because VecDeque::with_capacity doesn't necessarily give the
    // exact capacity specified.
    size_limit: usize,
    inspect_node: Node,
}

impl<T> InspectQueue<T> {
    pub fn new(size: usize) -> Self {
        Self {
            items: VecDeque::with_capacity(size),
            size_limit: size,
            inspect_node: Node::default(),
        }
    }

    pub fn items_mut(&mut self) -> &mut VecDeque<T> {
        &mut self.items
    }

    pub fn inspect_node(&self) -> &Node {
        &self.inspect_node
    }

    pub fn push(&mut self, item: T) {
        // If the queue is over capacity, remove one.
        if self.items.len() == self.size_limit {
            let _ = self.items.pop_front();
        }
        self.items.push_back(item);
    }

    #[cfg(test)]
    pub(crate) fn len(&self) -> usize {
        self.items.len()
    }
}

impl<T> Inspect for &mut InspectQueue<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        let items = &mut self.items;
        self.inspect_node = parent.create_child(name.as_ref());
        for inspect_info in items.iter_mut() {
            let _ = inspect_info.iattach(&self.inspect_node, inspect::unique_name(""));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::inspect_queue::InspectQueue;
    use fuchsia_inspect::{self as inspect, assert_data_tree, Node};
    use fuchsia_inspect_derive::{IValue, Inspect, WithInspect};

    #[derive(Inspect)]
    struct TestInspectWrapper {
        inspect_node: Node,
        queue: InspectQueue<TestInspectItem>,
    }

    impl TestInspectWrapper {
        fn new(queue: InspectQueue<TestInspectItem>) -> Self {
            Self { inspect_node: Node::default(), queue: queue }
        }
    }

    #[derive(Inspect)]
    struct TestInspectItem {
        inspect_node: Node,
        id: IValue<u64>,
    }

    impl TestInspectItem {
        fn new(id: u64) -> Self {
            Self { inspect_node: Node::default(), id: id.into() }
        }
    }

    // Test that a queue with less items than its capacity has the correct
    // inspect and queue content.
    #[test]
    fn test_queue_under_capacity() {
        let inspector = inspect::Inspector::new();
        let mut inspect_queue = InspectQueue::<TestInspectItem>::new(3);
        let test_val_1 = TestInspectItem::new(6);
        let test_val_2 = TestInspectItem::new(5);
        inspect_queue.push(test_val_1);
        inspect_queue.push(test_val_2);
        let _wrapper = TestInspectWrapper::new(inspect_queue)
            .with_inspect(inspector.root(), "inspect_wrapper")
            .expect("failed to create TestInspectWrapper inspect node");
        assert_data_tree!(inspector, root: {
            inspect_wrapper: {
                queue: {
                    "0": {
                        "id": 6 as u64,
                    },
                    "1": {
                        "id": 5 as u64,
                    },
                },
            }
        });
        assert_eq!(_wrapper.queue.len(), 2);
    }

    // Test that a queue with more items than its capacity has the correct
    // inspect and queue content.
    #[test]
    fn test_queue_over_capacity() {
        let inspector = inspect::Inspector::new();
        let mut inspect_queue = InspectQueue::<TestInspectItem>::new(2);
        let test_val_1 = TestInspectItem::new(6);
        let test_val_2 = TestInspectItem::new(5);
        let test_val_3 = TestInspectItem::new(4);
        inspect_queue.push(test_val_1);
        inspect_queue.push(test_val_2);
        inspect_queue.push(test_val_3);
        let _wrapper = TestInspectWrapper::new(inspect_queue)
            .with_inspect(inspector.root(), "inspect_wrapper")
            .expect("failed to create TestInspectWrapper inspect node");
        assert_data_tree!(inspector, root: {
            inspect_wrapper: {
                queue: {
                    "0": {
                        "id": 5 as u64,
                    },
                    "1": {
                        "id": 4 as u64,
                    },
                },
            }
        });
        assert_eq!(_wrapper.queue.len(), 2);
    }
}
