// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::error::ModelError,
    crate::store::embedded::EmbeddedStore,
    crate::store::store::Store,
    anyhow::{Error, Result},
    serde::{Deserialize, Serialize},
    serde_json,
    std::boxed::Box,
    std::sync::RwLock,
};

/// The DataModel is the public facing data abstraction which acts as the
/// driver for the underlying data store. It is the job of the data model to
/// store and query the store and return the data in the correct abstract form
/// that is expected by the application.
pub struct DataModel {
    components: RwLock<Vec<Component>>,
    manifests: RwLock<Vec<Manifest>>,
    routes: RwLock<Vec<Route>>,
    _store: Box<dyn Store>,
}

impl DataModel {
    /// Connects to the internal data store and setups everything.
    pub fn connect(uri: String) -> Result<Self> {
        let store: Box<dyn Store> = Box::new(EmbeddedStore::connect(uri)?);
        Ok(Self {
            components: RwLock::new(Vec::new()),
            manifests: RwLock::new(Vec::new()),
            routes: RwLock::new(Vec::new()),
            _store: DataModel::setup_schema(store)?,
        })
    }

    /// Verifies the underlying data model is running the correct current
    /// schema.
    fn setup_schema(mut store: Box<dyn Store>) -> Result<Box<dyn Store>> {
        let collection_names =
            vec!["components", "component_instances", "manifests", "routes", "protocols"];

        if let Ok(collection) = store.get("scrutiny") {
            let scrutiny: Scrutiny =
                serde_json::from_value(collection.write().unwrap().get("scrutiny")?)?;
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
            // Verify that all the expected collections exist.
            for name in collection_names.iter() {
                if let Err(_) = store.get(name) {
                    return Err(Error::new(ModelError::model_collection_not_found(*name)));
                }
            }
        } else {
            // Construct all of the required collections and flush them to disk.
            let collection = store.create("scrutiny")?;
            collection.write().unwrap().insert(
                "scrutiny".to_string(),
                serde_json::to_value(Scrutiny::default()).unwrap(),
            )?;
            for name in collection_names.iter() {
                store.create(name)?;
            }
            store.flush()?;
        }
        Ok(store)
    }

    pub fn components(&self) -> &RwLock<Vec<Component>> {
        &self.components
    }

    pub fn manifests(&self) -> &RwLock<Vec<Manifest>> {
        &self.manifests
    }

    pub fn routes(&self) -> &RwLock<Vec<Route>> {
        &self.routes
    }
}

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

/// Defines a component. Each component has a unique id which is used to link
/// it in the Route table. Each component also has a url and a version. This
/// structure is intended to be lightweight and general purpose if you need to
/// append additional information about a component make another table and
/// index it on the `component.id`.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Component {
    pub id: i32,
    pub url: String,
    pub version: i32,
    pub inferred: bool,
}

/// A component instance is a specific instantiation of a component. These
/// may run in a particular realm with certain restrictions.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct ComponentInstance {
    pub id: i32,
    pub moniker: String,
    pub component_id: i32,
}

/// Defines a component manifest. The `component_id` maps 1:1 to
/// `component.id` indexes. This is stored in a different table as most queries
/// don't need the raw manifest.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Manifest {
    pub component_id: i32,
    pub manifest: String,
    // uses should not be persisted to backing key-value store, it is primarily
    // used to build routes.
    pub uses: Vec<String>,
}

// TODO(benwright) - Add support for "first class" capabilities such as runners,
// resolvers and events.
/// Defines a link between two components. The `src_id` is the `component_instance.id`
/// of the component giving a service or directory to the `dst_id`. The
/// `protocol_id` refers to the Protocol with this link.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Route {
    pub id: i32,
    pub src_id: i32,
    pub dst_id: i32,
    pub service_name: String,
    pub protocol_id: i32,
}

/// Defines either a FIDL or Directory protocol with some interface name such
/// as fuchshia.foo.Bar and an optional path such as "/dev".
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Protocol {
    pub id: i32,
    pub interface: String,
    pub path: String,
}
