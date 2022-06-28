// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::Node;
use fuchsia_inspect_derive::{AttachError, Inspect};
use std::collections::HashMap;

/// A map from String to inspect-writeable item T. InspectWritableMap is compatible
/// with inspect_derive.
#[derive(Default)]
pub(crate) struct InspectWritableMap<T> {
    map: HashMap<String, T>,
}

impl<T> InspectWritableMap<T> {
    pub(crate) fn new() -> Self {
        Self { map: HashMap::new() }
    }

    #[allow(dead_code)]
    pub(crate) fn get(&mut self, key: &str) -> Option<&T> {
        self.map.get(key)
    }

    pub(crate) fn get_mut(&mut self, key: &str) -> Option<&mut T> {
        self.map.get_mut(key)
    }

    pub(crate) fn set(&mut self, key: String, value: T) {
        let _ = self.map.insert(key, value);
    }

    /// Retrieves the existing value of key [key] if it exists, otherwise sets
    /// the value to [value] and returns it.
    pub(crate) fn get_or_insert_with(&mut self, key: String, value: impl FnOnce() -> T) -> &mut T {
        self.map.entry(key).or_insert_with(value)
    }
}

impl<T> Inspect for &mut InspectWritableMap<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, _name: impl AsRef<str>) -> Result<(), AttachError> {
        let map = &mut self.map;
        for (key, inspect_info) in map.iter_mut() {
            let _ = inspect_info.iattach(parent, key);
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::base::SettingType;
    use crate::inspect::utils::inspect_writable_map::InspectWritableMap;
    use fuchsia_inspect::{self as inspect, assert_data_tree, Node};
    use fuchsia_inspect_derive::{IValue, Inspect, WithInspect};

    #[derive(Inspect)]
    struct TestInspectWrapper {
        inspect_node: Node,
        map: InspectWritableMap<TestInspectItem>,
    }

    impl TestInspectWrapper {
        fn new(inspect_writable_map: InspectWritableMap<TestInspectItem>) -> Self {
            Self { inspect_node: Node::default(), map: inspect_writable_map }
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

        fn set_id(&mut self, id: u64) {
            self.id.iset(id);
        }
    }

    // Test that a map with a single item can be written to inspect.
    #[test]
    fn test_single_item_map() {
        let inspector = inspect::Inspector::new();
        let mut map = InspectWritableMap::<TestInspectItem>::new();
        let test_val_1 = TestInspectItem::new(6);
        map.set(format!("{:?}", SettingType::Unknown), test_val_1);
        let _wrapper = TestInspectWrapper::new(map)
            .with_inspect(inspector.root(), "inspect_wrapper")
            .expect("failed to create TestInspectWrapper inspect node");
        assert_data_tree!(inspector, root: {
            inspect_wrapper: {
                "Unknown": {
                    "id": 6 as u64,
                },
            }
        });
    }

    // Test that a map with multiple items can be written to inspect.
    #[test]
    fn test_multiple_item_map() {
        let inspector = inspect::Inspector::new();
        let mut map = InspectWritableMap::<TestInspectItem>::new();
        let test_val_1 = TestInspectItem::new(6);
        let test_val_2 = TestInspectItem::new(7);
        let test_val_3 = TestInspectItem::new(8);
        map.set(format!("{:?}", SettingType::Unknown), test_val_1);
        map.set(format!("{:?}", SettingType::Unknown), test_val_2);
        map.set(format!("{:?}", SettingType::Audio), test_val_3);
        let _wrapper = TestInspectWrapper::new(map)
            .with_inspect(inspector.root(), "inspect_wrapper")
            .expect("failed to create TestInspectWrapper inspect node");
        assert_data_tree!(inspector, root: {
            inspect_wrapper: {
                "Unknown": {
                    "id": 7 as u64,
                },
                "Audio": {
                    "id": 8 as u64,
                }
            }
        });
    }

    // Test that a value written to a key can be retrieved again.
    #[test]
    fn test_get() {
        let mut map = InspectWritableMap::<TestInspectItem>::new();
        let test_val_1 = TestInspectItem::new(6);
        map.set(format!("{:?}", SettingType::Unknown), test_val_1);
        assert_eq!(
            *map.get(&format!("{:?}", SettingType::Unknown))
                .expect("Could not find first test value")
                .id,
            6
        );
    }

    // Test the get_or_insert function.
    #[test]
    fn test_get_or_insert() {
        let mut map = InspectWritableMap::<TestInspectItem>::new();
        let test_val_1 = TestInspectItem::new(6);
        let test_val_2 = TestInspectItem::new(7);
        let unknown_type_key = format!("{:?}", SettingType::Unknown);

        let first_val = map.get_or_insert_with(unknown_type_key.clone(), || test_val_1);
        assert_eq!(*(*first_val).id, 6);

        let second_val = map.get_or_insert_with(unknown_type_key, || test_val_2);
        assert_eq!(*(*second_val).id, 6);

        second_val.set_id(7);
        assert_eq!(*(*second_val).id, 7);
    }
}
