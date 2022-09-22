// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::Node;
use fuchsia_inspect_derive::{AttachError, Inspect, WithInspect};
use std::collections::HashMap;

/// A map that wraps an inspect node and attaches all inserted values to the node.
///
/// This class can either be explicitly given an inspect node through [ManagedInspectMap::with_node]
/// or can create its own inspect node when included in a struct that derives Inspect or when
/// [ManagedInspectMap::with_inspect] is called.
#[derive(Default)]
pub struct ManagedInspectMap<V> {
    map: HashMap<String, V>,
    node: Node,
}

impl<V> ManagedInspectMap<V>
where
    for<'a> &'a mut V: Inspect,
{
    /// Creates a new [ManagedInspectMap] that attaches inserted values to the given node.
    pub fn with_node(node: Node) -> Self {
        Self { map: HashMap::new(), node }
    }

    /// Returns a reference to the underlying map. Clients should not insert values into the
    /// map through this reference.
    pub fn map(&self) -> &HashMap<String, V> {
        &self.map
    }

    /// Returns a mutable reference to the underlying map. Clients should not insert values into the
    /// map through this reference.
    pub fn map_mut(&mut self) -> &mut HashMap<String, V> {
        &mut self.map
    }

    /// Inserts the given value into the map and attach it to the inspect tree. Returns the previous
    /// value with the given key, if any.
    pub fn insert(&mut self, key: String, value: V) -> Option<V> {
        // `with_inspect` will only return an error on types with interior mutability.
        let value_with_inspect =
            value.with_inspect(&self.node, &key).expect("Failed to attach new map entry");
        self.map.insert(key, value_with_inspect)
    }

    /// Inserts the given value into the map and attaches it to the inspect tree with a different
    /// name. Returns the previous value with the given map key, if any.
    ///
    /// This is useful for cases where the unique key for the map is not useful for actually
    /// recording to inspect.
    pub fn insert_with_property_name(
        &mut self,
        map_key: String,
        property_name: String,
        value: V,
    ) -> Option<V> {
        // `with_inspect` will only return an error on types with interior mutability.
        let value_with_inspect =
            value.with_inspect(&self.node, &property_name).expect("Failed to attach new map entry");
        self.map.insert(map_key, value_with_inspect)
    }

    /// Returns a mutable reference to the value at the given key, inserting a value if not present.
    pub fn get_or_insert_with(&mut self, key: String, value: impl FnOnce() -> V) -> &mut V {
        let node = &self.node;
        self.map.entry(key.clone()).or_insert_with(|| {
            // `with_inspect` will only return an error on types with interior mutability.
            value().with_inspect(node, &key).expect("Failed to attach new map entry")
        })
    }

    /// Returns a mutable reference to the entry at `key`.
    pub fn get_mut(&mut self, key: &str) -> Option<&mut V> {
        self.map.get_mut(key)
    }

    /// Returns an immutable reference to the entry at `key`.
    pub fn get(&self, key: &str) -> Option<&V> {
        self.map.get(key)
    }

    /// Returns a reference to the inspect node associated with this map.
    pub fn inspect_node(&self) -> &Node {
        &self.node
    }
}

impl<V> Inspect for &mut ManagedInspectMap<V>
where
    for<'a> &'a mut V: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self.node = parent.create_child(name.as_ref());
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::managed_inspect_map::ManagedInspectMap;
    use fuchsia_inspect::{assert_data_tree, Inspector, Node};
    use fuchsia_inspect_derive::{IValue, Inspect, WithInspect};

    #[derive(Default, Inspect)]
    struct TestInspectWrapper {
        inspect_node: Node,
        pub test_map: ManagedInspectMap<IValue<String>>,
    }

    // Tests that inserting items into the map automatically records them in inspect.
    #[test]
    fn test_map_insert() {
        let inspector = Inspector::new();

        let mut map = ManagedInspectMap::<IValue<String>>::with_node(
            inspector.root().create_child("managed_node"),
        );

        let _ = map.insert("key1".to_string(), "value1".to_string().into());
        let _ = map.insert("key2".to_string(), "value2".to_string().into());

        assert_data_tree!(inspector, root: {
            managed_node: {
                "key1": "value1",
                "key2": "value2"
            }
        });
    }

    // Tests that removing items from the map automatically removes them from inspect.
    #[test]
    fn test_map_remove() {
        let inspector = Inspector::new();

        let mut map = ManagedInspectMap::<IValue<String>>::with_node(
            inspector.root().create_child("managed_node"),
        );

        let _ = map.insert("key1".to_string(), "value1".to_string().into());
        let _ = map.insert("key2".to_string(), "value2".to_string().into());

        let _ = map.map_mut().remove(&"key1".to_string());

        assert_data_tree!(inspector, root: {
            managed_node: {
                "key2": "value2"
            }
        });
    }

    // Tests that inserting items with a different property name shows the intended property name in
    // inspect.
    #[test]
    fn test_map_insert_with_property_name() {
        let inspector = Inspector::new();

        let mut map = ManagedInspectMap::<IValue<String>>::with_node(
            inspector.root().create_child("managed_node"),
        );

        let _ = map.insert_with_property_name(
            "key1".to_string(),
            "property_name_1".to_string(),
            "value1".to_string().into(),
        );

        // This will overwrite the previous insert due to using the same map key.
        let _ = map.insert_with_property_name(
            "key1".to_string(),
            "property_name_2".to_string(),
            "value2".to_string().into(),
        );

        assert_data_tree!(inspector, root: {
            managed_node: {
                "property_name_2": "value2"
            }
        });
    }

    // Tests that the map automatically attaches itself to the inspect hierarchy when used as a
    // field in a struct that derives Inspect.
    #[test]
    fn test_map_derive_inspect() {
        let inspector = Inspector::new();

        let mut wrapper = TestInspectWrapper::default()
            .with_inspect(inspector.root(), "wrapper_node")
            .expect("Failed to attach wrapper_node");

        let _ = wrapper.test_map.insert("key1".to_string(), "value1".to_string().into());

        // The map's node is named test_map since that's the field name in TestInspectWrapper.
        assert_data_tree!(inspector, root: {
            wrapper_node: {
                test_map: {
                    "key1": "value1",
                }
            }
        });
    }
}
