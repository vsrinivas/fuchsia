// Copyright 2020 The Fuchsia Authors. All rights reserved.  Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::error::ModelError,
    crate::model::collection::DataCollection,
    anyhow::{Error, Result},
    scrutiny_config::ModelConfig,
    serde::{Deserialize, Serialize},
    std::{
        any::{Any, TypeId},
        collections::HashMap,
        sync::{Arc, Mutex},
    },
};

/// The DataModel is the public facing data abstraction which acts as the
/// driver for the underlying data store. It is the job of the data model to
/// store and query the store and return the data in the correct abstract form
/// that is expected by the application.
pub struct DataModel {
    collections: Mutex<HashMap<TypeId, Arc<dyn Any + 'static + Send + Sync>>>,
    config: ModelConfig,
}

impl DataModel {
    /// Connects to the internal data store and setups everything.
    pub fn new(config: ModelConfig) -> Result<Self> {
        Ok(Self { collections: Mutex::new(HashMap::new()), config })
    }

    /// Attempts to retrieve the DataCollection from the in memory HashMap.
    /// If this fails the DataModel will attempt to load the file from the
    /// EmbeddedStore (on disk) and cache it into the HashMap. Only if both of
    /// these fail will the collection return an error.
    pub fn get<T: DataCollection + Any + 'static + Send + Sync + for<'de> Deserialize<'de>>(
        &self,
    ) -> Result<Arc<T>> {
        let collections = self.collections.lock().unwrap();
        let type_id = TypeId::of::<T>();
        if collections.contains_key(&type_id) {
            if let Ok(result) = collections.get(&type_id).unwrap().clone().downcast::<T>() {
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

    /// Sets a DataCollection in the DataModel. This function should only be used
    /// by DataCollectors that intend to replace a given DataCollection with an updated
    /// complete collection. Small changes to an existing collection should be made
    /// directly with the get() option which returns a mutable copy of the collection.
    pub fn set<T: DataCollection + Any + 'static + Send + Sync + Serialize>(
        &self,
        collection: T,
    ) -> Result<()> {
        let mut collections = self.collections.lock().unwrap();
        let type_id = TypeId::of::<T>();
        collections.insert(type_id, Arc::new(collection));
        Ok(())
    }

    pub fn remove<T: DataCollection + Any>(&self) {
        let type_id = TypeId::of::<T>();
        let mut collections = self.collections.lock().unwrap();
        collections.remove(&type_id);
    }

    /// Returns an immutable reference to the ModelConfig which can be
    /// retrieved by `DataControllers` and `DataControllers`.
    pub fn config(&self) -> &ModelConfig {
        &self.config
    }
}
