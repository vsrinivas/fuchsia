// Copyright 2020 The Fuchsia Authors. All rights reserved.  Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::error::ModelError,
    crate::{
        model::collection::DataCollection,
        store::{embedded::EmbeddedStore, memory::MemoryStore, store::Store},
    },
    anyhow::{Error, Result},
    serde::{Deserialize, Serialize},
    serde_json,
    std::{
        any::Any,
        boxed::Box,
        collections::HashMap,
        path::PathBuf,
        sync::{Arc, Mutex},
    },
    uuid::Uuid,
};

/// The ModelEnvironment specifies the runtime configuration that defines where
/// `DataCollectors` should retrieve information for the current model. This is
/// important to allow collectors to dynamically change their collection source
/// based on configuration such as where the build directory is located etc.
#[derive(Serialize, Deserialize)]
pub struct ModelEnvironment {
    pub uri: String,
    pub build_path: PathBuf,
    pub repository_path: PathBuf,
}

impl ModelEnvironment {
    /// Helper function to return a clone of the build path.
    pub fn build_path(&self) -> PathBuf {
        self.build_path.clone()
    }
    /// Helper function to return a clone of the repository path.
    pub fn repository_path(&self) -> PathBuf {
        self.repository_path.clone()
    }
}

/// The DataModel is the public facing data abstraction which acts as the
/// driver for the underlying data store. It is the job of the data model to
/// store and query the store and return the data in the correct abstract form
/// that is expected by the application.
pub struct DataModel {
    collections: Mutex<HashMap<Uuid, Arc<dyn Any + 'static + Send + Sync>>>,
    environment: ModelEnvironment,
    store: Mutex<Box<dyn Store>>,
}

impl DataModel {
    /// Connects to the internal data store and setups everything.
    pub fn connect(environment: ModelEnvironment) -> Result<Self> {
        let store: Mutex<Box<dyn Store>> =
            DataModel::setup_schema(Self::store_factory(&environment)?)?;

        Ok(Self { collections: Mutex::new(HashMap::new()), environment, store })
    }

    /// Selects the internal store based on the URI set.
    fn store_factory(environment: &ModelEnvironment) -> Result<Box<dyn Store>> {
        if environment.uri == "{memory}" {
            Ok(Box::new(MemoryStore::connect(environment.uri.clone())?))
        } else {
            Ok(Box::new(EmbeddedStore::connect(environment.uri.clone())?))
        }
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
                    T::collection_name(),
                    T::collection_description(),
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
                        T::collection_name(),
                        T::collection_description(),
                    )))
                }
            } else {
                Err(Error::new(ModelError::model_collection_not_found(
                    T::collection_name(),
                    T::collection_description(),
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

    /// Returns an immutable reference to the ModelEnvironment which can be
    /// retrieved by `DataControllers` and `DataControllers`.
    pub fn env(&self) -> &ModelEnvironment {
        &self.environment
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
    fn collection_name() -> String {
        "Scrutiny Collection".to_string()
    }
    fn collection_description() -> String {
        "A trivial collection which contains the scrutiny data model version.".to_string()
    }
}
