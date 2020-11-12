// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, serde_json::value::Value, uuid::Uuid};

/// Represents a simple high level abstracted data store.
pub trait Store: Send + Sync {
    /// Connect to the database and return a handle to that database.
    fn connect(uri: String) -> Result<Self>
    where
        Self: std::marker::Sized;
    /// Returns the database URI.
    fn uri(&self) -> &str;
    /// Lists all the collections in the store.
    fn collections(&self) -> Result<Vec<Uuid>>;
    /// Sets or replaces a collection and flushes it to storage.
    fn set(&mut self, name: Uuid, collection: Value) -> Result<()>;
    /// Reads a collecction from storage and returns it.
    fn get(&mut self, name: &Uuid) -> Result<Value>;
    /// Removes and deletes a colleection.
    fn remove(&mut self, name: &Uuid) -> Result<()>;
}
