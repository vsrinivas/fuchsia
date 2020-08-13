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
    std::collections::HashMap,
    std::sync::RwLock,
};

/// The DataModel is the public facing data abstraction which acts as the
/// driver for the underlying data store. It is the job of the data model to
/// store and query the store and return the data in the correct abstract form
/// that is expected by the application.
pub struct DataModel {
    components: RwLock<Vec<Component>>,
    packages: RwLock<Vec<Package>>,
    manifests: RwLock<Vec<Manifest>>,
    routes: RwLock<Vec<Route>>,
    zbi: RwLock<Option<Zbi>>,
    store: RwLock<Box<dyn Store>>,
}

impl DataModel {
    /// Connects to the internal data store and setups everything.
    pub fn connect(uri: String) -> Result<Self> {
        let store: RwLock<Box<dyn Store>> =
            DataModel::setup_schema(Box::new(EmbeddedStore::connect(uri)?))?;

        let mut components = Vec::new();
        if let Ok(guard) = store.write().unwrap().get("components") {
            let collection = guard.read().unwrap();
            for (_index, component) in collection.iter() {
                components.push(serde_json::from_value(component.clone())?);
            }
        }

        let mut packages = Vec::new();
        if let Ok(guard) = store.write().unwrap().get("packages") {
            let collection = guard.read().unwrap();
            for (_index, package) in collection.iter() {
                packages.push(serde_json::from_value(package.clone())?);
            }
        }

        let mut manifests = Vec::new();
        if let Ok(guard) = store.write().unwrap().get("manifests") {
            let collection = guard.read().unwrap();
            for (_index, manifest) in collection.iter() {
                manifests.push(serde_json::from_value(manifest.clone())?);
            }
        }

        let mut routes = Vec::new();
        if let Ok(guard) = store.write().unwrap().get("routes") {
            let collection = guard.read().unwrap();
            for (_index, route) in collection.iter() {
                routes.push(serde_json::from_value(route.clone())?);
            }
        }

        let mut zbi: Option<Zbi> = None;
        if let Ok(guard) = store.write().unwrap().get("zbi") {
            let collection = guard.read().unwrap();
            if let Ok(value) = collection.get("zbi") {
                zbi = serde_json::from_value(value)?;
            }
        }

        Ok(Self {
            components: RwLock::new(components),
            packages: RwLock::new(packages),
            manifests: RwLock::new(manifests),
            routes: RwLock::new(routes),
            zbi: RwLock::new(zbi),
            store,
        })
    }

    /// Verifies the underlying data model is running the correct current
    /// schema.
    fn setup_schema(mut store: Box<dyn Store>) -> Result<RwLock<Box<dyn Store>>> {
        let collection_names = vec!["components", "packages", "manifests", "routes", "zbi"];

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
        Ok(RwLock::new(store))
    }

    pub fn components(&self) -> &RwLock<Vec<Component>> {
        &self.components
    }

    pub fn packages(&self) -> &RwLock<Vec<Package>> {
        &self.packages
    }

    pub fn manifests(&self) -> &RwLock<Vec<Manifest>> {
        &self.manifests
    }

    pub fn routes(&self) -> &RwLock<Vec<Route>> {
        &self.routes
    }

    pub fn zbi(&self) -> &RwLock<Option<Zbi>> {
        &self.zbi
    }

    pub fn flush(&self) -> Result<()> {
        let mut store = self.store.write().unwrap();
        if let Ok(mut collection) = store.get("components").unwrap().write() {
            for component in self.components.read().unwrap().iter() {
                collection
                    .insert(component.id.to_string(), serde_json::to_value(component).unwrap())?;
            }
        }
        if let Ok(mut collection) = store.get("packages").unwrap().write() {
            for package in self.packages.read().unwrap().iter() {
                collection
                    .insert(package.url.to_string(), serde_json::to_value(package).unwrap())?;
            }
        }
        if let Ok(mut collection) = store.get("routes").unwrap().write() {
            for route in self.routes.read().unwrap().iter() {
                collection.insert(route.id.to_string(), serde_json::to_value(route).unwrap())?;
            }
        }
        if let Ok(mut collection) = store.get("manifests").unwrap().write() {
            for manifest in self.manifests.read().unwrap().iter() {
                collection.insert(
                    manifest.component_id.to_string(),
                    serde_json::to_value(manifest).unwrap(),
                )?;
            }
        }
        if let Ok(mut collection) = store.get("zbi").unwrap().write() {
            if let Some(zbi) = &*self.zbi.read().unwrap() {
                collection.insert("zbi".to_string(), serde_json::to_value(zbi).unwrap())?;
            }
        }
        store.flush()
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

/// Defines a fuchsia package. Each package has a unique url. This provides an
/// expanded meta/contents so you can see all of the files defined in this
/// package.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Package {
    pub url: String,
    pub merkle: String,
    pub contents: HashMap<String, String>,
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

/// Defines all of the known ZBI section types. These are used to partition
/// the Zircon boot image into sections.
#[repr(u32)]
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub enum ZbiType {
    AcpiRsdp = 0x50445352,
    BootVersion = 0x53525642,
    BootloaderFile = 0x4C465442,
    Cmdline = 0x4c444d43,
    CpuConfig = 0x43555043,
    CpuTopology = 0x544F504F,
    Crashlog = 0x4d4f4f42,
    Discard = 0x50494b53,
    DriverBoardInfo = 0x4953426D,
    DriverBoardPrivate = 0x524F426D,
    DriverMacAddress = 0x43414D6D,
    DriverPartitionMap = 0x5452506D,
    E820MemoryTable = 0x30323845,
    EfiMemoryMap = 0x4d494645,
    EfiSystemTable = 0x53494645,
    FrameBuffer = 0x42465753,
    ImageArgs = 0x47524149,
    KernelDriver = 0x5652444B,
    MemoryConfig = 0x434D454D,
    Nvram = 0x4c4c564e,
    NvramDeprecated = 0x4c4c5643,
    PlatformId = 0x44494C50,
    RebootReason = 0x42525748,
    SerialNumber = 0x4e4c5253,
    Smbios = 0x49424d53,
    StorageBootfs = 0x42534642,
    StorageBootfsFactory = 0x46534642,
    StorageRamdisk = 0x4b534452,
    KernelArm64 = 0x384e524b,
    KernelX64 = 0x4C4E524B,
    Unknown,
}

/// ZbiSection holder that contains the type and an uncompressed buffer
/// containing the data.
#[allow(dead_code)]
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct ZbiSection {
    pub section_type: ZbiType,
    pub buffer: Vec<u8>,
}

/// Defines all of the parsed information in the ZBI.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
pub struct Zbi {
    // Raw section data for each zbi section. This section isn't serialized to
    // disk because it occupies a large amount of space.
    #[serde(skip)]
    pub sections: Vec<ZbiSection>,
    // File names to data contained in bootfs.
    // TODO(benwright) - Work out how to optimize this for speed.
    #[serde(skip)]
    pub bootfs: HashMap<String, Vec<u8>>,
    pub cmdline: String,
}

#[cfg(test)]
mod tests {
    use {super::*, tempfile::tempdir};

    fn create_model() -> (String, DataModel) {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let uri_clone = uri.clone();
        (uri, DataModel::connect(uri_clone).unwrap())
    }

    #[test]
    fn test_model_flush_empty() {
        let (_, model) = create_model();
        assert_eq!(model.flush().is_ok(), true);
    }

    #[test]
    fn test_model_flush_insert() {
        let (uri, model) = create_model();
        let component = Component { id: 0, url: "foo".to_string(), version: 1, inferred: true };
        model.components().write().unwrap().push(component);
        assert_eq!(model.flush().is_ok(), true);
        drop(model);

        let reloaded_model = DataModel::connect(uri.clone()).unwrap();
        assert_eq!(reloaded_model.components.read().unwrap().len(), 1);
    }

    #[test]
    fn test_zbi_empty_on_construction() {
        let (_, model) = create_model();
        assert_eq!(*model.zbi().read().unwrap(), None);
    }
}
