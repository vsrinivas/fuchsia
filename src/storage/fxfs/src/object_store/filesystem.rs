use {
    crate::{
        object_store::{
            allocator::{Allocator, SimpleAllocator},
            constants::{INVALID_OBJECT_ID, ROOT_PARENT_STORE_OBJECT_ID},
            log::Log,
            record::ObjectType,
            Device, Directory, ObjectStore, StoreOptions,
        },
    },
    anyhow::Error,
    std::{
        collections::HashMap,
        sync::{Arc, RwLock},
    },
};

#[cfg(test)]
use {
    anyhow::anyhow,
    crate::testing::fake_device::FakeDevice,
};

#[derive(Default)]
pub struct SyncOptions {
    pub new_super_block: bool,
}

pub struct StoreManager {
    stores: RwLock<Stores>,
}

struct Stores {
    stores: HashMap<u64, Arc<ObjectStore>>,
    root_store_object_id: u64,
}

impl StoreManager {
    pub fn new() -> StoreManager {
        StoreManager {
            stores: RwLock::new(Stores {
                stores: HashMap::new(),
                root_store_object_id: INVALID_OBJECT_ID,
            }),
        }
    }

    pub fn root_parent_store(&self) -> Arc<ObjectStore> {
        self.store(ROOT_PARENT_STORE_OBJECT_ID).unwrap()
    }

    pub fn new_store(&self, store: Arc<ObjectStore>) {
        self.stores.write().unwrap().stores.insert(store.store_object_id(), store);
    }

    pub fn store(&self, store_object_id: u64) -> Option<Arc<ObjectStore>> {
        self.stores.read().unwrap().stores.get(&store_object_id).cloned()
    }

    pub fn set_root_store(&self, store: Arc<ObjectStore>) {
        let mut stores = self.stores.write().unwrap();
        stores.root_store_object_id = store.store_object_id();
        stores.stores.insert(store.store_object_id(), store);
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        let stores = self.stores.read().unwrap();
        stores.stores.get(&stores.root_store_object_id).unwrap().clone()
    }
}

pub struct VolumeManager {
    // We store volumes as entries in a directory.
    storage: Arc<RwLock<Directory>>,
}

impl VolumeManager {
    pub fn new(storage: Directory) -> Self {
        Self { storage: Arc::new(RwLock::new(storage)) }
    }

    pub fn new_volume(&self, volume_name: &str) -> Result<Directory, Error> {
        // TODO this should be transactional.

        let volume_store = self.storage.write().unwrap().create_volume_store(volume_name)?;
        println!("added volume ({:?}) object id {:?}", volume_name, volume_store.store_object_id());

        // Add the root directory.
        let initial_dir_handle = volume_store.create_directory()?;
        volume_store.set_root_object_id(initial_dir_handle.object_id());
        // TODO do we need to force?
        volume_store.flush(true)?;
        Ok(initial_dir_handle)
    }

    pub fn volume(&self, volume_name: &str) -> Option<Directory> {
        let storage = self.storage.read().unwrap();
        let (object_id, object_type) = storage.lookup(volume_name).ok()?;
        // TODO better error handling; this panics if the child's type is unexpected, or the
        // load fails
        if let ObjectType::Volume = object_type {
            // nop
        } else {
            panic!("Unexpceted non-volume child")
        }
        let root_store = storage.object_store();
        let volume_store = root_store.open_store(object_id, StoreOptions::default()).ok()?;
        volume_store.open_directory(volume_store.root_object_id()).ok()
    }
}

pub struct Filesystem {
    device: Arc<dyn Device>,
    log: Arc<Log>,
    stores: Arc<StoreManager>,
    volumes: Arc<VolumeManager>,
}

impl Filesystem {
    pub fn new_empty(device: Arc<dyn Device>) -> Result<Filesystem, Error> {
        let log = Arc::new(Log::new());
        let allocator = Arc::new(SimpleAllocator::new(&log));
        let stores = Arc::new(StoreManager::new());
        stores.new_store(ObjectStore::new_empty(
            None,
            ROOT_PARENT_STORE_OBJECT_ID,
            device.clone(),
            &(allocator.clone() as Arc<dyn Allocator>),
            &log,
            StoreOptions::default(),
        ));
        log.init_empty(&stores, &(allocator as Arc<dyn Allocator>))?;

        // Add a directory as the root of all volumes. The root store's root object ID will be
        // this directory.
        let volume_manager_handle = stores.root_store().create_directory()?;
        stores.root_store().set_root_object_id(volume_manager_handle.object_id());
        println!("volume manager object id {:?}", volume_manager_handle.object_id());
        let volumes = Arc::new(VolumeManager::new(volume_manager_handle));

        Ok(Filesystem { device, log, stores, volumes })
    }

    pub fn open(device: Arc<dyn Device>) -> Result<Filesystem, Error> {
        let log = Arc::new(Log::new());
        let allocator = Arc::new(SimpleAllocator::new(&log));
        let stores = Arc::new(StoreManager::new());
        log.replay(device.clone(), stores.clone(), allocator.clone())?;

        let volume_manager_handle =
            stores.root_store().open_directory(stores.root_store().root_object_id())?;
        let volumes = Arc::new(VolumeManager::new(volume_manager_handle));

        Ok(Filesystem { device, log, stores, volumes })
    }

    pub fn new_volume(&mut self, name: &str) -> Result<Directory, Error> {
        self.volumes.new_volume(name)
    }

    // TODO is Directory the best type to return? Wrapper type, maybe.
    pub fn volume(&self, name: &str) -> Option<Directory> {
        self.volumes.volume(name)
    }

    pub fn root_parent_store(&self) -> Arc<ObjectStore> {
        self.stores.root_parent_store()
    }

    pub fn root_store(&self) -> Arc<ObjectStore> {
        self.stores.root_store()
    }

    pub fn sync(&mut self, options: SyncOptions) -> Result<(), Error> {
        self.log.sync(options)
    }

    pub fn device(&self) -> &Arc<dyn Device> {
        return &self.device;
    }
}

// TODO: Consider sync on drop

#[test]
fn test_lookup_nonexistent_volume() -> Result<(), Error> {
    let device = Arc::new(FakeDevice::new(512));
    let filesystem = Filesystem::new_empty(device.clone())?;
    if let None = filesystem.volume("vol") {
        Ok(())
    } else {
        Err(anyhow!("Expected no volume"))
    }
}

// TODO fix me
/*
#[test]
fn test_add_volume() -> Result<(), Error> {
    let device = Arc::new(FakeDevice::new(512));
    {
        let mut filesystem = Filesystem::new_empty(device.clone())?;

        let mut volume = filesystem.new_volume("vol")?;
        volume.create_child_file("foo")?;
        filesystem.sync(SyncOptions::default())?;
    };
    {
        let filesystem = Filesystem::open(device.clone())?;

        let volume = filesystem.volume("vol").ok_or(anyhow!("Volume not found"))?;
        volume.lookup("foo")?;
    };
    Ok(())
}
*/