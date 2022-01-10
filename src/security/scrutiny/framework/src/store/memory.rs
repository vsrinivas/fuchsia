// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::store::Store,
    anyhow::{anyhow, Result},
    serde_json::value::Value,
    std::collections::HashMap,
    uuid::Uuid,
};

/// The MemoryStore represents a directory of JSON files accessed by UUIDs.
/// This is a very simple storage model that is portable and doesn't require
/// importing a fully featured third party database. It makes no attempt to
/// understand or interpret the data. It simply takes Value types and either
/// reads them from disk or writes them to the disk. The store itself is
/// not thread safe. Writes should be done one at a time and a higher level
/// in memory cache should be implemented at a higher level.
pub struct MemoryStore {
    uri: String,
    ram_disk: HashMap<Uuid, Value>,
}

impl Store for MemoryStore {
    /// Connect to the database and return a handle to that database.
    fn connect(uri: String) -> Result<Self> {
        Ok(Self { uri, ram_disk: HashMap::new() })
    }

    /// Returns the database URI.
    fn uri(&self) -> &str {
        &self.uri
    }

    /// Lists all the collectionsin the database.
    fn collections(&self) -> Result<Vec<Uuid>> {
        Ok(self.ram_disk.keys().map(|k| k.clone()).collect())
    }

    /// Create a new table, initializing it and adding it to the database.
    fn set(&mut self, name: Uuid, collection: Value) -> Result<()> {
        self.ram_disk.insert(name, collection);
        Ok(())
    }

    /// Returns a collection if it exists.
    fn get(&mut self, name: &Uuid) -> Result<Value> {
        if let Some(collection) = self.ram_disk.get(&name) {
            return Ok(collection.clone());
        }
        Err(anyhow!("Failed to find collection with uuid: {}", name))
    }

    /// Drop a table, deleting it and removing it from the store.
    fn remove(&mut self, name: &Uuid) -> Result<()> {
        self.ram_disk.remove(&name);
        Ok(())
    }
}
