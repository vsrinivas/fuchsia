// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! High level node abstraction on top of Fuchsia's Stash service.
///! Allows to insert structured, hierarchical data into a Stash backed
///! store.
use {
    anyhow::{format_err, Error},
    fidl_fuchsia_stash as fidl_stash,
    std::collections::{HashMap, HashSet},
};

pub mod policy;

pub const NODE_SEPARATOR: &'static str = "#/@";

pub struct StashNode {
    // Always terminated with `NODE_SEPARATOR`
    key: String,
    stash: fidl_stash::StoreAccessorProxy,
}

/// A convience wrapper around a Stash-backed `HashMap`.
pub struct StashNodeFields(HashMap<String, fidl_stash::Value>);

impl StashNodeFields {
    /// Returns `None` if the field is not present or its value is not of type `String`.
    /// Returns `Some(String)` otherwise.
    pub fn get_str(&self, field: &str) -> Option<&String> {
        match self.0.get(field) {
            Some(fidl_stash::Value::Stringval(x)) => Some(x),
            _ => None,
        }
    }

    /// Returns `None` if the field is not present or its value is not of type `i64`.
    /// Returns `Some(i64)` otherwise.
    pub fn get_int(&self, field: &str) -> Option<i64> {
        match self.0.get(field) {
            Some(fidl_stash::Value::Intval(x)) => Some(*x),
            _ => None,
        }
    }

    /// Returns `None` if the field is not present or its value is not of type `bool`.
    /// Returns `Some(bool)` otherwise.
    pub fn get_bool(&self, field: &str) -> Option<bool> {
        match self.0.get(field) {
            Some(fidl_stash::Value::Boolval(x)) => Some(*x),
            _ => None,
        }
    }

    /// Returns `None` if the field is not present or its value is not of type `f64`.
    /// Returns `Some(f64)` otherwise.
    pub fn get_float(&self, field: &str) -> Option<f64> {
        match self.0.get(field) {
            Some(fidl_stash::Value::Floatval(x)) => Some(*x),
            _ => None,
        }
    }
}

impl StashNode {
    pub fn root(stash: fidl_stash::StoreAccessorProxy) -> Self {
        Self { key: NODE_SEPARATOR.to_string(), stash }
    }
    pub fn with_key(key: String, stash: fidl_stash::StoreAccessorProxy) -> Self {
        Self { key, stash }
    }

    /// Returns the key's current node. The key shouldn't be accessed unless the key itself is of
    /// a constant, never changing value.
    pub fn key(&self) -> String {
        self.key.clone()
    }

    /// Requests a child node with a given name.
    /// The child may or may not exist already in Stash.
    pub fn child(&self, key: &str) -> Self {
        Self { key: format!("{}{}{}", self.key, key, NODE_SEPARATOR), stash: self.stash.clone() }
    }

    /// Deletes an entire node and all its fields and children.
    /// No-op if the Node is not yet backed by Stash.
    pub fn delete(&mut self) -> Result<(), Error> {
        self.stash.delete_prefix(&self.key)?;
        Ok(())
    }

    /// Deletes a specific field of the node. No-op if the field doesn't exist.
    pub fn delete_field(&mut self, field: &str) -> Result<(), Error> {
        self.stash.delete_value(&format!("{}{}", self.key, field))?;
        Ok(())
    }

    fn write_val(&mut self, field: &str, mut value: fidl_stash::Value) -> Result<(), Error> {
        self.stash.set_value(&format!("{}{}", self.key, field), &mut value)?;
        Ok(())
    }
    pub fn write_str(&mut self, field: &str, s: String) -> Result<(), Error> {
        self.write_val(field, fidl_stash::Value::Stringval(s))
    }

    pub fn write_int(&mut self, field: &str, i: i64) -> Result<(), Error> {
        self.write_val(field, fidl_stash::Value::Intval(i))
    }

    pub fn write_bool(&mut self, field: &str, b: bool) -> Result<(), Error> {
        self.write_val(field, fidl_stash::Value::Boolval(b))
    }

    pub fn write_float(&mut self, field: &str, f: f64) -> Result<(), Error> {
        self.write_val(field, fidl_stash::Value::Floatval(f))
    }

    /// Flushes all changes made to Stash. Blocks until stash completes the flush.
    /// Note that this applies to the entire tree, not just changes made under this node.
    pub async fn flush(&mut self) -> Result<(), Error> {
        self.stash.flush().await?.map_err(|e| format_err!("failed to flush changes: {:?}", e))
    }

    /// Returns an unordered set of all of the node's direct child nodes.
    pub async fn children(&self) -> Result<HashSet<StashNode>, Error> {
        let (local, remote) = fidl::endpoints::create_proxy::<_>()?;
        let () = self.stash.list_prefix(&self.key, remote)?;

        let parent_key_len = self.key.len();
        let mut children = HashSet::new();
        loop {
            let key_list = local.get_next().await?;
            if key_list.is_empty() {
                break;
            }

            for list_item in key_list {
                let key = &list_item.key[parent_key_len..];
                if let Some(idx) = key.find(NODE_SEPARATOR) {
                    children.insert(self.child(&key[..idx]));
                }
            }
        }
        Ok(children)
    }

    /// Returns an `StashNodeFields` granting access to all direct fields of the node.
    pub async fn fields(&self) -> Result<StashNodeFields, Error> {
        let (local, remote) = fidl::endpoints::create_proxy::<_>()?;
        let () = self.stash.get_prefix(&self.key, remote)?;
        let parent_key_len = self.key.len();
        let mut fields = HashMap::new();
        loop {
            let key_value_list = local.get_next().await?;
            if key_value_list.is_empty() {
                break;
            }

            for key_value in key_value_list {
                let key = &key_value.key[parent_key_len..];
                if let None = key.find(NODE_SEPARATOR) {
                    fields.insert(key.to_string(), key_value.val);
                }
            }
        }
        Ok(StashNodeFields(fields))
    }

    async fn read_val(&self, field: &str) -> Option<fidl_stash::Value> {
        match self.stash.get_value(&format!("{}{}", self.key, field)).await.ok() {
            Some(Some(boxed_value)) => Some(*boxed_value),
            _ => None,
        }
    }

    pub async fn read_int(&self, field: &str) -> Option<i64> {
        match self.read_val(field).await {
            Some(fidl_stash::Value::Intval(x)) => Some(x),
            _ => None,
        }
    }

    pub async fn read_str(&self, field: &str) -> Option<String> {
        match self.read_val(field).await {
            Some(fidl_stash::Value::Stringval(x)) => Some(x),
            _ => None,
        }
    }

    pub async fn read_bool(&self, field: &str) -> Option<bool> {
        match self.read_val(field).await {
            Some(fidl_stash::Value::Boolval(x)) => Some(x),
            _ => None,
        }
    }

    pub async fn read_float(&self, field: &str) -> Option<f64> {
        match self.read_val(field).await {
            Some(fidl_stash::Value::Floatval(x)) => Some(x),
            _ => None,
        }
    }
}
impl std::hash::Hash for StashNode {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.key.hash(state);
    }
}
impl PartialEq for StashNode {
    fn eq(&self, other: &Self) -> bool {
        self.key == other.key
    }
}
impl Eq for StashNode {}

#[cfg(test)]
mod tests {
    use {super::*, fidl::endpoints::create_proxy, fuchsia_component::client::connect_to_service};

    fn new_stash_store(id: &str) -> fidl_stash::StoreAccessorProxy {
        let store_client = connect_to_service::<fidl_stash::StoreMarker>()
            .expect("failed connecting to Stash service");
        store_client.identify(id).expect("failed identifying client to store");
        let (proxy, remote) = create_proxy().expect("failed creating accessor proxy");
        store_client.create_accessor(false, remote).expect("failed creating Stash accessor");
        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_root() {
        let proxy = new_stash_store("test_root");

        let mut root = StashNode::root(proxy);

        // Nothing written yet.
        let fields = root.fields().await.expect("error reading fields");
        assert!(fields.0.is_empty());

        // Write a field.
        root.write_str("test", "Foobar".to_string()).expect("error writing field");
        root.flush().await.expect("error flushing");

        // Read a single fields and all fields.
        assert_eq!(Some("Foobar".to_string()), root.read_str("test").await);
        let fields = root.fields().await.expect("error reading fields");
        assert!(!fields.0.is_empty());
        assert_eq!(Some(&"Foobar".to_string()), fields.get_str("test"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_overwrite() {
        let proxy = new_stash_store("test_overwrite");

        let mut root = StashNode::root(proxy);

        // Write a field.
        root.write_str("test", "FoobarOne".to_string()).expect("error writing field");
        root.flush().await.expect("error flushing");

        // Overwrite the same field.
        root.write_str("test", "FoobarTwo".to_string()).expect("error writing field");
        root.flush().await.expect("error flushing");
        assert_eq!(Some("FoobarTwo".to_string()), root.read_str("test").await);

        // Overwrite the same field with a different type.
        root.write_int("test", 1337).expect("error writing field");
        root.flush().await.expect("error flushing");
        assert_eq!(None, root.read_str("test").await);
        assert_eq!(Some(1337), root.read_int("test").await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_child() {
        let proxy = new_stash_store("test_child");

        let root = StashNode::root(proxy);

        let mut child = root.child("child");
        child.write_str("test", "Foobar".to_string()).expect("error writing field");
        child.flush().await.expect("error flushing");

        // Root should have no fields.
        let fields = root.fields().await.expect("error reading fields");
        assert!(fields.0.is_empty());

        // Child should hold entry.
        assert_eq!(Some("Foobar".to_string()), child.read_str("test").await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_multi_level_value() {
        let proxy = new_stash_store("test_multi_level_value");

        let mut root = StashNode::root(proxy);

        root.write_str("same_key", "Value Root".to_string()).expect("error writing field");
        root.write_str("root", "Foobar Root".to_string()).expect("error writing field");
        root.flush().await.expect("error flushing");

        let mut child = root.child("child");
        child.write_str("same_key", "Value Child".to_string()).expect("error writing field");
        child.write_str("child", "Foobar Child".to_string()).expect("error writing field");
        child.flush().await.expect("error flushing");

        assert_eq!(Some("Value Root".to_string()), root.read_str("same_key").await);
        assert_eq!(Some("Foobar Root".to_string()), root.read_str("root").await);
        assert_eq!(Some("Value Child".to_string()), child.read_str("same_key").await);
        assert_eq!(Some("Foobar Child".to_string()), child.read_str("child").await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_delete() {
        let proxy = new_stash_store("test_delete");

        let mut root = StashNode::root(proxy);

        root.write_str("same_key", "Value Root".to_string()).expect("error writing field");
        root.write_str("root", "Foobar Root".to_string()).expect("error writing field");
        root.flush().await.expect("error flushing");

        let mut child = root.child("child");
        child.write_str("same_key", "Value Child".to_string()).expect("error writing field");
        child.write_str("child", "Foobar Child".to_string()).expect("error writing field");
        child.flush().await.expect("error flushing");

        child.delete().expect("failed to delete");
        child.flush().await.expect("error flushing");

        assert_eq!(Some("Value Root".to_string()), root.read_str("same_key").await);
        assert_eq!(Some("Foobar Root".to_string()), root.read_str("root").await);

        let fields = child.fields().await.expect("error reading fields");
        assert!(fields.0.is_empty());
        assert!(root.children().await.expect("error fetching children").is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_delete_field() {
        let proxy = new_stash_store("test_delete_field");

        let mut root = StashNode::root(proxy);

        root.write_str("same_key", "Value Root".to_string()).expect("error writing field");
        root.write_str("root", "Foobar Root".to_string()).expect("error writing field");
        root.flush().await.expect("error flushing");

        root.delete_field("same_key").expect("failed to delete");
        root.flush().await.expect("error flushing");

        assert_eq!(None, root.read_str("same_key").await);
        assert_eq!(Some("Foobar Root".to_string()), root.read_str("root").await);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_children() {
        let proxy = new_stash_store("test_children");

        let root = StashNode::root(proxy);

        let mut child_a = root.child("child a");
        let expected_key = child_a.key.clone();

        // Accessing the child will not yet create it.
        let children = root.children().await.expect("error fetching children");
        assert!(children.is_empty());

        child_a.write_int("an int", 42).expect("failed to write value");
        child_a.flush().await.expect("error flushting");

        // Child should now be available to query.
        let children = root.children().await.expect("error fetching children");
        assert_eq!(children.len(), 1);
        let added_child = children.iter().next().unwrap();

        // Read from child.
        assert_eq!(Some(42), added_child.read_int("an int").await);
        assert_eq!(expected_key, added_child.key);
    }
}
