use {
    crate::{
        object_handle::{ObjectHandle, ObjectHandleCursor},
        object_store::{
            allocator::{Allocator, SimpleAllocator},
            constants::{INVALID_OBJECT_ID, RESERVED_OBJECT_ID, ROOT_PARENT_STORE_OBJECT_ID},
            log::Log,
            record::ObjectType,
            Device, Directory, HandleOptions, ObjectStore, StoreOptions, Transaction,
        },
    },
    anyhow::Error,
    bincode::{deserialize_from, serialize_into},
    serde::{Deserialize, Serialize},
    std::{
        collections::HashMap,
        io::{BufWriter, Write},
        sync::{Arc, RwLock},
    },
};

#[cfg(test)]
use {crate::testing::fake_device::FakeDevice, anyhow::anyhow};

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

    pub fn new_store(&self, store: &Arc<ObjectStore>) {
        self.stores.write().unwrap().stores.insert(store.store_object_id(), store.clone());
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

#[derive(Clone, Default, Serialize, Deserialize)]
struct VolumeInfo {
    root_directory_object_id: u64,
}

pub struct Filesystem {
    device: Arc<dyn Device>,
    log: Arc<Log>,
    stores: Arc<StoreManager>,
    volume_directory: Arc<RwLock<Directory>>,
}

impl Filesystem {
    pub fn new_empty(device: Arc<dyn Device>) -> Result<Filesystem, Error> {
        let log = Arc::new(Log::new());
        let allocator = Arc::new(SimpleAllocator::new(&log));
        let stores = Arc::new(StoreManager::new());
        stores.new_store(&ObjectStore::new_empty(
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
        let mut transaction = Transaction::new();
        let handle = stores.root_store().create_object_with_id(
            &mut transaction,
            RESERVED_OBJECT_ID,
            HandleOptions::default(),
        )?;
        log.commit(transaction);
        let mut writer = BufWriter::new(ObjectHandleCursor::new(&handle as &dyn ObjectHandle, 0));
        let volume_manager_handle = stores.root_store().create_directory()?;
        let info = VolumeInfo { root_directory_object_id: volume_manager_handle.object_id() };
        serialize_into(&mut writer, &info)?;
        writer.flush()?;

        Ok(Filesystem {
            device,
            log,
            stores,
            volume_directory: Arc::new(RwLock::new(volume_manager_handle)),
        })
    }

    pub fn open(device: Arc<dyn Device>) -> Result<Filesystem, Error> {
        let log = Arc::new(Log::new());
        let allocator = Arc::new(SimpleAllocator::new(&log));
        let stores = Arc::new(StoreManager::new());
        log.replay(device.clone(), stores.clone(), allocator.clone())?;

        let handle =
            stores.root_store().open_object(RESERVED_OBJECT_ID, HandleOptions::default())?;
        let info: VolumeInfo =
            deserialize_from(ObjectHandleCursor::new(&handle as &dyn ObjectHandle, 0))?;
        let volume_manager_handle =
            stores.root_store().open_directory(info.root_directory_object_id)?;

        Ok(Filesystem {
            device,
            log,
            stores,
            volume_directory: Arc::new(RwLock::new(volume_manager_handle)),
        })
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

    pub fn new_volume(&self, volume_name: &str) -> Result<Directory, Error> {
        // TODO this should be transactional.

        let volume_store =
            self.volume_directory.write().unwrap().create_volume_store(volume_name)?;

        // Add the root directory.
        let mut transaction = Transaction::new();
        let handle = volume_store.create_object_with_id(
            &mut transaction,
            RESERVED_OBJECT_ID,
            HandleOptions::default(),
        )?;
        self.log.commit(transaction);
        let mut writer = BufWriter::new(ObjectHandleCursor::new(&handle as &dyn ObjectHandle, 0));
        let initial_dir_handle = volume_store.create_directory()?;
        let info = VolumeInfo { root_directory_object_id: initial_dir_handle.object_id() };
        serialize_into(&mut writer, &info)?;
        writer.flush()?;

        Ok(initial_dir_handle)
    }

    // TODO is Directory the best type to return? Wrapper type, maybe.
    pub fn volume(&self, volume_name: &str) -> Option<Directory> {
        let volume_directory = self.volume_directory.read().unwrap();
        let (object_id, object_type) = volume_directory.lookup(volume_name).ok()?;
        // TODO better error handling; this panics if the child's type is unexpected, or the
        // load fails
        if let ObjectType::Volume = object_type {
            // nop
        } else {
            panic!("Unexpected non-volume child")
        }
        let volume_store = self.stores.store(object_id).unwrap_or(
            self.stores.root_store().open_store(object_id, StoreOptions::default()).ok()?,
        );
        let handle = volume_store.open_object(RESERVED_OBJECT_ID, HandleOptions::default()).ok()?;
        let info: VolumeInfo =
            deserialize_from(ObjectHandleCursor::new(&handle as &dyn ObjectHandle, 0)).ok()?;
        volume_store.open_directory(info.root_directory_object_id).ok()
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
