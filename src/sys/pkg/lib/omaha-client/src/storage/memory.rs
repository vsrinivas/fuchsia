// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use futures::future::BoxFuture;
use futures::prelude::*;
use std::collections::HashMap;

/// The MemStorage struct is an in-memory-only implementation of the Storage trait, to be used in
/// testing scenarios.
#[derive(Debug)]
pub struct MemStorage {
    /// The values current stored.
    data: HashMap<String, Value>,

    /// Whether commit() has been called after set_*() or remove().
    committed: bool,
}

/// Value is an enumeration for holding the values in MemStorage.
#[derive(Debug)]
enum Value {
    String(String),
    Int(i64),
    Bool(bool),
}

/// The stub implementation doesn't return errors, so this is just a placeholder.
#[derive(Debug, Fail)]
pub enum StorageErrors {
    #[fail(display = "Unknown error occurred")]
    Unknown,
}

impl MemStorage {
    pub fn new() -> Self {
        MemStorage { data: HashMap::new(), committed: true }
    }

    pub fn committed(&self) -> bool {
        self.committed
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }
}

impl Storage for MemStorage {
    type Error = StorageErrors;

    /// Get a string from the backing store.  Returns None if there is no value for the given key.
    fn get_string<'a>(&'a self, key: &'a str) -> BoxFuture<'_, Option<String>> {
        future::ready(match self.data.get(key) {
            Some(Value::String(s)) => Some(s.clone()),
            _ => None,
        })
        .boxed()
    }

    /// Get an int from the backing store.  Returns None if there is no value for the given key.
    fn get_int<'a>(&'a self, key: &'a str) -> BoxFuture<'_, Option<i64>> {
        future::ready(match self.data.get(key) {
            Some(Value::Int(i)) => Some(*i),
            _ => None,
        })
        .boxed()
    }

    /// Get a boolean from the backing store.  Returns None if there is no value for the given key.
    fn get_bool<'a>(&'a self, key: &'a str) -> BoxFuture<'_, Option<bool>> {
        future::ready(match self.data.get(key) {
            Some(Value::Bool(b)) => Some(*b),
            _ => None,
        })
        .boxed()
    }

    /// Set a value to be stored in the backing store.  The implementation should cache the value
    /// until the |commit()| fn is called, and then persist all cached values at that time.
    fn set_string<'a>(
        &'a mut self,
        key: &'a str,
        value: &'a str,
    ) -> BoxFuture<'_, Result<(), Self::Error>> {
        self.data.insert(key.to_string(), Value::String(value.to_string()));
        self.committed = false;
        future::ready(Ok(())).boxed()
    }

    /// Set a value to be stored in the backing store.  The implementation should cache the value
    /// until the |commit()| fn is called, and then persist all cached values at that time.
    fn set_int<'a>(
        &'a mut self,
        key: &'a str,
        value: i64,
    ) -> BoxFuture<'_, Result<(), Self::Error>> {
        self.data.insert(key.to_string(), Value::Int(value));
        self.committed = false;
        future::ready(Ok(())).boxed()
    }

    /// Set a value to be stored in the backing store.  The implementation should cache the value
    /// until the |commit()| fn is called, and then persist all cached values at that time.
    fn set_bool<'a>(
        &'a mut self,
        key: &'a str,
        value: bool,
    ) -> BoxFuture<'_, Result<(), Self::Error>> {
        self.data.insert(key.to_string(), Value::Bool(value));
        self.committed = false;
        future::ready(Ok(())).boxed()
    }

    fn remove<'a>(&'a mut self, key: &'a str) -> BoxFuture<'_, Result<(), Self::Error>> {
        self.data.remove(key);
        self.committed = false;
        future::ready(Ok(())).boxed()
    }

    /// Set a value to be stored in the backing store.  The implementation should cache the value
    /// until the |commit()| fn is called, and then persist all cached values at that time.
    fn commit(&mut self) -> BoxFuture<'_, Result<(), Self::Error>> {
        self.committed = true;
        future::ready(Ok(())).boxed()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::storage::tests::*;
    use futures::executor::block_on;

    #[test]
    fn test_set_get_remove_string() {
        block_on(do_test_set_get_remove_string(&mut MemStorage::new()));
    }

    #[test]
    fn test_set_get_remove_int() {
        block_on(do_test_set_get_remove_int(&mut MemStorage::new()));
    }

    #[test]
    fn test_set_get_remove_bool() {
        block_on(do_test_set_get_remove_bool(&mut MemStorage::new()));
    }

    #[test]
    fn test_return_none_for_wrong_value_type() {
        block_on(do_return_none_for_wrong_value_type(&mut MemStorage::new()));
    }

    #[test]
    fn test_ensure_no_error_remove_nonexistent_key() {
        block_on(do_ensure_no_error_remove_nonexistent_key(&mut MemStorage::new()));
    }

    #[test]
    fn test_committed() {
        block_on(async {
            let mut storage = MemStorage::new();
            assert_eq!(true, storage.committed());
            storage.set_bool("some bool key", false).await.unwrap();
            assert_eq!(false, storage.committed());
            storage.commit().await.unwrap();
            assert_eq!(true, storage.committed());
            storage.set_string("some string key", "some string").await.unwrap();
            assert_eq!(false, storage.committed());
            storage.set_int("some int key", 42).await.unwrap();
            assert_eq!(false, storage.committed());
            storage.commit().await.unwrap();
            assert_eq!(true, storage.committed());
        });
    }
}
