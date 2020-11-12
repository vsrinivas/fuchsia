// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::error::ModelError,
    crate::model::collection::DataCollection,
    crate::store::embedded::EmbeddedStore,
    crate::store::store::Store,
    anyhow::{Error, Result},
    serde::{Deserialize, Serialize},
    serde_json,
    std::any::Any,
    std::boxed::Box,
    std::collections::HashMap,
    std::sync::{Arc, Mutex},
    uuid::Uuid,
};

/// The DataModel is the public facing data abstraction which acts as the
/// driver for the underlying data store. It is the job of the data model to
/// store and query the store and return the data in the correct abstract form
/// that is expected by the application.
pub struct DataModel {
    collections: Mutex<HashMap<Uuid, Arc<dyn Any + 'static + Send + Sync>>>,
    store: Mutex<Box<dyn Store>>,
}

impl DataModel {
    /// Connects to the internal data store and setups everything.
    pub fn connect(uri: String) -> Result<Self> {
        let store: Mutex<Box<dyn Store>> =
            DataModel::setup_schema(Box::new(EmbeddedStore::connect(uri)?))?;

        Ok(Self { collections: Mutex::new(HashMap::new()), store })
    }

    /// Verifies the underlying data model is running the correct current
    /// schema.
    fn setup_schema(mut store: Box<dyn Store>) -> Result<Mutex<Box<dyn Store>>> {
        let scrutiny_uuid = Uuid::parse_str(STORE_UUID).unwrap();
        if let Ok(collection) = store.get(&scrutiny_uuid) {
            let scrutiny: Scrutiny = serde_json::from_value(collection)?;
            if scrutiny.magic != STORE_MAGIC {
                return Err(Error::new(ModelError::model_magic_incompatible(
                    STORE_MAGIC,
                    scrutiny.magic,
                )));
            }
            if scrutiny.version < STORE_VERSION {
                return Err(Error::new(ModelError::model_version_incompatible(
                    STORE_VERSION,
                    scrutiny.version,
                )));
            }
            if scrutiny.version > STORE_VERSION {
                return Err(Error::new(ModelError::model_version_incompatible(
                    STORE_VERSION,
                    scrutiny.version,
                )));
            }
        } else {
            // Save the new scrutiny collection.
            store.set(scrutiny_uuid, serde_json::to_value(Scrutiny::default()).unwrap())?;
        }
        Ok(Mutex::new(store))
    }

    /// Attempts to retrieve the DataCollection from the in memory HashMap.
    /// If this fails the DataModel will attempt to load the file from the
    /// EmbeddedStore (on disk) and cache it into the HashMap. Only if both of
    /// these fail will the collection return an error.
    pub fn get<T: DataCollection + Any + 'static + Send + Sync + for<'de> Deserialize<'de>>(
        &self,
    ) -> Result<Arc<T>> {
        let mut collections = self.collections.lock().unwrap();
        let mut store = self.store.lock().unwrap();
        let uuid = T::uuid();
        if collections.contains_key(&uuid) {
            if let Ok(result) = collections.get(&uuid).unwrap().clone().downcast::<T>() {
                Ok(result)
            } else {
                Err(Error::new(ModelError::model_collection_not_found(
                    T::uuid().to_hyphenated().to_string(),
                )))
            }
        } else {
            // Load the collection from storage and into memory.
            if let Ok(data) = store.get(&uuid) {
                let collection: Arc<T> = Arc::new(serde_json::from_value(data).unwrap());
                collections.insert(uuid.clone(), collection);
                if let Ok(result) = collections.get(&uuid).unwrap().clone().downcast::<T>() {
                    Ok(result)
                } else {
                    Err(Error::new(ModelError::model_collection_not_found(
                        T::uuid().to_hyphenated().to_string(),
                    )))
                }
            } else {
                Err(Error::new(ModelError::model_collection_not_found(
                    T::uuid().to_hyphenated().to_string(),
                )))
            }
        }
    }

    /// Sets a DataCollection in the DataModel. This function should only be used
    /// by DataCollectors that intend to replace a given DataCollection with an updated
    /// complete collection. Small changes to an existing collection should be made
    /// directly with the get() option which returns a mutable copy of the collection.
    pub fn set<T: DataCollection + Any + 'static + Send + Sync + Serialize>(
        &self,
        collection: T,
    ) -> Result<()> {
        let mut collections = self.collections.lock().unwrap();
        let mut store = self.store.lock().unwrap();
        let uuid = T::uuid();
        store.set(uuid, serde_json::to_value(&collection)?)?;
        collections.insert(uuid, Arc::new(collection));
        Ok(())
    }

    pub fn remove<T: DataCollection>(&self) -> Result<()> {
        let uuid = T::uuid();
        let mut collections = self.collections.lock().unwrap();
        let mut store = self.store.lock().unwrap();
        collections.remove(&uuid);
        store.remove(&uuid)
    }
}

const STORE_UUID: &str = "8b0a24ed-63e2-4f73-a8e1-7a2a92c82287";
const STORE_MAGIC: &str = "scrutiny";
const STORE_VERSION: usize = 1;

/// Defines the scrutiny collection which contains a single entry which
/// provides the versioning information for the store. If the store is on the
/// wrong version or has the wrong magic number the DataModel will use this to
/// fail out.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
struct Scrutiny {
    pub version: usize,
    pub magic: String,
}

impl Scrutiny {
    /// Returns the expected values of the scrutiny collection.
    pub fn default() -> Self {
        Self { version: STORE_VERSION, magic: STORE_MAGIC.to_string() }
    }
}

impl DataCollection for Scrutiny {
    fn uuid() -> Uuid {
        Uuid::parse_str(STORE_UUID).unwrap()
    }
}
