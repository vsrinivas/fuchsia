// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    serde_json::value::Value,
    std::collections::hash_map::Iter,
    std::sync::{Arc, RwLock},
};

/// The core storage type used by a collection.
pub type Element = Value;

/// Represents a simple high level abstracted data store.
pub trait Store: Send + Sync {
    /// Connect to the database and return a handle to that database.
    fn connect(uri: String) -> Result<Self>
    where
        Self: std::marker::Sized;
    /// Returns the database URI.
    fn uri(&self) -> &str;
    /// Lists all the collectionsin the database.
    fn collections(&self) -> Result<Vec<String>>;
    /// Create a new table, initializing it and adding it to the database.
    fn create(&mut self, name: &str) -> Result<Arc<RwLock<dyn Collection>>>;
    /// Returns an existing collection.
    fn get(&mut self, name: &str) -> Result<Arc<RwLock<dyn Collection>>>;
    /// Drop a table, deleting it and removing it from the database.
    fn drop(&mut self, name: &str) -> Result<Arc<RwLock<dyn Collection>>>;
    /// Flush forces all the collections to be written to disk.
    fn flush(&self) -> Result<()>;
}

/// A collection holds all JSON data tagged with a unique key.
/// Implementations must provide O(1) insertion and retrieval.
pub trait Collection {
    /// Returns true if the key is in the collection.
    fn contains(&self, key: &str) -> bool;
    /// Inserts an element into the collection.
    fn insert(&mut self, key: String, value: Element) -> Result<()>;
    /// Removes a specific key identifier from the collection.
    fn remove(&mut self, key: &str) -> Result<Element>;
    /// Retrieves a specific item from the collection.
    fn get(&self, key: &str) -> Result<Element>;
    /// Forces this collection to be written to disk.
    fn flush(&self) -> Result<()>;
    /// Iterator over the key,value pairs in the collection.
    fn iter(&self) -> Iter<'_, String, Value>;
}
