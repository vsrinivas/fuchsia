// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        error::{CollectionError, StoreError},
        store::{Collection, Element, Store},
    },
    anyhow::{Error, Result},
    log::{info, trace, warn},
    serde::{Deserialize, Serialize},
    serde_json::value::Value,
    std::collections::hash_map::Iter,
    std::collections::HashMap,
    std::fs::{self, File},
    std::io::{BufReader, BufWriter},
    std::path::{Path, PathBuf},
    std::sync::{Arc, RwLock},
};

/// The EmbeddedStore is the defacto store that is completely in-memory and
/// flushes out to disk through JSON serialization. This is a very simple
/// storage model that is portable and doesn't require importing a fully
/// featured third party database.
pub struct EmbeddedStore {
    uri: String,
    collections: HashMap<String, Arc<RwLock<EmbeddedCollection>>>,
}

impl EmbeddedStore {
    /// Returns the expected collection path for a given collection name.
    fn collection_path(&self, collection_name: impl Into<String>) -> PathBuf {
        Path::new(&self.uri).join(format!("{}.bin", collection_name.into()))
    }
}

impl Store for EmbeddedStore {
    /// Connect to the database and return a handle to that database.
    fn connect(uri: String) -> Result<Self> {
        let mut collections: HashMap<String, Arc<RwLock<EmbeddedCollection>>> = HashMap::new();
        // A EmbeddedStore is just a directory with a set of json files in it.
        // Each json file is a serialized EmbeddedCollection.
        let path = Path::new(&uri);
        if !path.exists() {
            info!("Creating data model at: {}", uri);
            fs::create_dir(&path)?;
        }
        info!("Data Model: connecting {}", uri);
        for entry in fs::read_dir(&path)? {
            let entry = entry?;
            let path = entry.path();
            if let Some(ext) = path.extension() {
                if ext == "bin" {
                    info!("Loading collection: {:?}", path);
                    let name = path.file_stem().unwrap().to_os_string().into_string().unwrap();
                    let file = File::open(path.clone())?;
                    let reader = BufReader::new(file);
                    if let Ok(json) = serde_cbor::from_reader(reader) {
                        let collection = EmbeddedCollection::load(path, json)?;
                        collections.insert(name, Arc::new(RwLock::new(collection)));
                    } else {
                        warn!("Collection corrupted purging: {:?}", path);
                        let collection = EmbeddedCollection::new(path)?;
                        collections.insert(name, Arc::new(RwLock::new(collection)));
                    }
                }
            }
        }
        Ok(Self { uri, collections })
    }

    /// Returns the database URI.
    fn uri(&self) -> &str {
        &self.uri
    }

    /// Lists all the collectionsin the database.
    fn collections(&self) -> Result<Vec<String>> {
        Ok(self.collections.keys().map(|k| k.clone()).collect())
    }

    /// Create a new table, initializing it and adding it to the database.
    fn create(&mut self, name: &str) -> Result<Arc<RwLock<dyn Collection>>> {
        if self.collections.contains_key(name) {
            trace!("Failed to create collection it already exists: {}", name);
            Err(Error::new(StoreError::collection_already_exists(name.to_string())))
        } else {
            let collection_path = self.collection_path(name);
            let collection = Arc::new(RwLock::new(EmbeddedCollection::new(collection_path)?));
            collection.read().unwrap().flush()?;
            self.collections.insert(name.to_string(), Arc::clone(&collection));
            Ok(collection)
        }
    }

    /// Returns a collection if it exists.
    fn get(&mut self, name: &str) -> Result<Arc<RwLock<dyn Collection>>> {
        if let Some(collection) = self.collections.get(name) {
            Ok(collection.clone())
        } else {
            trace!("Failed to get collection it does not exist: {}", name);
            Err(Error::new(StoreError::collection_does_not_exist(name)))
        }
    }

    /// Drop a table, deleting it and removing it from the database.
    fn drop(&mut self, name: &str) -> Result<Arc<RwLock<dyn Collection>>> {
        if let Some(result) = self.collections.remove(name) {
            fs::remove_file(self.collection_path(name.to_string()))?;
            Ok(result)
        } else {
            trace!("Failed to drop collection it does not exist: {}", name);
            Err(Error::new(StoreError::collection_does_not_exist(name)))
        }
    }

    /// Flushes all the collections to disk.
    fn flush(&self) -> Result<()> {
        info!("Store: flushing to disk {}", self.uri);
        for (name, collection) in self.collections.iter() {
            info!("Flushing collection: {}", name);
            collection.read().unwrap().flush()?;
            trace!("Flushing collection finished: {}", name);
        }
        Ok(())
    }
}

/// The disk format for the EmbeddedCollection, this omits any runtime only
/// book keeping data.
#[derive(Serialize, Deserialize)]
struct EmbeddedCollectionFormat {
    elements: HashMap<String, Element>,
}

/// EmbeddedCollections are equally as trivial, they just provide a simple O(1)
/// indexed store.
struct EmbeddedCollection {
    path: PathBuf,
    collection: EmbeddedCollectionFormat,
}

impl EmbeddedCollection {
    /// Creates a completely new collection, this will not flush it to disk
    /// this should be done immediately after creation.
    fn new(path: PathBuf) -> Result<Self> {
        let collection = EmbeddedCollectionFormat { elements: HashMap::new() };
        Ok(Self { path, collection })
    }

    /// Collections are parsed from a simple JSON structure on disk.
    fn load(path: PathBuf, data: Value) -> Result<Self> {
        let collection: EmbeddedCollectionFormat = serde_json::from_value(data)?;
        Ok(Self { path, collection })
    }
}

impl Collection for EmbeddedCollection {
    fn contains(&self, key: &str) -> bool {
        self.collection.elements.contains_key(key)
    }

    fn insert(&mut self, key: String, value: Element) -> Result<()> {
        self.collection.elements.insert(key, value);
        Ok(())
    }

    fn remove(&mut self, key: &str) -> Result<Element> {
        if let Some(element) = self.collection.elements.remove(key) {
            Ok(element)
        } else {
            Err(Error::new(CollectionError::element_not_found(key.to_string())))
        }
    }

    fn get(&self, key: &str) -> Result<Element> {
        if let Some(element) = self.collection.elements.get(key) {
            Ok(element.clone())
        } else {
            Err(Error::new(CollectionError::element_not_found(key.to_string())))
        }
    }

    /// Serializes the collection to a JSON file on disk.
    fn flush(&self) -> Result<()> {
        let file = File::create(&self.path)?;
        let writer = BufWriter::new(file);
        serde_cbor::to_writer(writer, &self.collection)?;
        Ok(())
    }

    fn iter(&self) -> Iter<'_, String, Value> {
        self.collection.elements.iter()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, serde_json::json, tempfile::tempdir};

    fn create_store() -> EmbeddedStore {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        EmbeddedStore::connect(uri).unwrap()
    }

    #[test]
    fn test_embedded_store_create() {
        let store = create_store();
        assert_eq!(store.collections().unwrap().len(), 0);
    }

    #[test]
    fn test_embedded_create_collection() {
        let mut store = create_store();
        assert_eq!(store.create("foo").is_ok(), true);
        assert_eq!(store.create("bar").is_ok(), true);
    }

    #[test]
    fn test_embedded_create_collection_conflict() {
        let mut store = create_store();
        assert_eq!(store.create("foo").is_ok(), true);
        assert_eq!(store.create("foo").is_err(), true);
    }

    #[test]
    fn test_collection_basic() {
        let mut store = create_store();
        let collection_lock = store.create("foo").unwrap();
        let mut collection = collection_lock.write().unwrap();
        collection.insert("bar".to_string(), json!(true)).unwrap();

        let value = collection.get("bar").unwrap();
        assert_eq!(value, json!(true));
        assert_eq!(collection.contains("bar"), true);
        let removed_value = collection.remove("bar").unwrap();
        assert_eq!(removed_value, json!(true));
        assert_eq!(collection.contains("bar"), false);
    }

    #[test]
    fn test_collection_drop() {
        let mut store = create_store();
        store.create("foo").unwrap();
        assert_eq!(store.get("foo").is_ok(), true);
        assert_eq!(store.drop("foo").is_ok(), true);
        assert_eq!(store.get("foo").is_err(), true);
    }

    #[test]
    fn test_load() {
        let mut store = create_store();
        let uri = String::from(store.uri());
        {
            let collection_lock = store.create("foo").unwrap();
            let mut collection = collection_lock.write().unwrap();
            collection.insert("bar".to_string(), json!(true)).unwrap();
            collection.insert("foo".to_string(), json!(false)).unwrap();
        }
        store.flush().unwrap();
        drop(store);
        {
            let mut existing_store = EmbeddedStore::connect(uri).unwrap();
            let collection_lock = existing_store.get("foo").unwrap();
            let collection = collection_lock.read().unwrap();
            assert_eq!(collection.get("bar").unwrap(), json!(true));
            assert_eq!(collection.get("foo").unwrap(), json!(false));
        }
    }
}
