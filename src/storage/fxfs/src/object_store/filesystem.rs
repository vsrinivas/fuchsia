use {
    crate::object_store::{
        allocator::{Allocator, SimpleAllocator},
        constants::ROOT_PARENT_STORE_OBJECT_ID,
        log::Log,
        Device, ObjectStore, StoreOptions,
    },
    anyhow::Error,
    std::{
        collections::HashMap,
        sync::{Arc, RwLock},
    },
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
            stores: RwLock::new(Stores { stores: HashMap::new(), root_store_object_id: 0 }),
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

pub struct Filesystem {
    device: Arc<dyn Device>,
    log: Arc<Log>,
    stores: Arc<StoreManager>,
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
        Ok(Filesystem { device, log, stores })
    }

    pub fn open(device: Arc<dyn Device>) -> Result<Filesystem, Error> {
        let log = Arc::new(Log::new());
        let allocator = Arc::new(SimpleAllocator::new(&log));
        let stores = Arc::new(StoreManager::new());
        log.replay(device.clone(), stores.clone(), allocator.clone())?;
        Ok(Filesystem { device, log, stores })
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
