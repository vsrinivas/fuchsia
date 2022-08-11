// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::device_storage::DeviceStorage;
use super::fidl_storage::FidlStorage;
use crate::stash_logger::StashInspectLogger;
use anyhow::{format_err, Error};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_io::DirectoryProxy;
use fidl_fuchsia_stash::StoreProxy;
use futures::lock::Mutex;
use std::{collections::HashSet, sync::Arc};

/// `DeviceStorageFactory` abstracts over how to initialize and retrieve the `DeviceStorage`
/// instance.
#[async_trait::async_trait]
pub trait StorageFactory {
    /// The storage type used to manage persisted data.
    type Storage;

    /// Initialize the storage to be able to manage storage for objects of type T.
    /// This will return an Error once `get_store` is called the first time.
    async fn initialize<T>(&self) -> Result<(), Error>
    where
        T: StorageAccess<Storage = Self::Storage>;

    /// Retrieve the store singleton instance.
    async fn get_store(&self) -> Arc<Self::Storage>;
}

/// A trait for describing which storages an item needs access to.
/// See [StashDeviceStorageFactory::initialize] for usage.
/// # Example
///
/// ```
/// # struct SomeItem;
/// # struct StorageItem;
///
/// impl DeviceStorageCompatible for StorageItem {
///    # fn default_value() -> Self { StorageItem }
///    // ...
///    const KEY: &'static str = "some_key";
/// }
///
/// impl StorageAccess for SomeItem {
///     type Storage = DeviceStorage;
///     const STORAGE_KEYS: &'static [&'static str] = &[StorageItem::KEY];
/// }
/// ```
pub trait StorageAccess {
    type Storage;

    /// This field should be populated by keys that are used by the corresponding storage mechanism.
    const STORAGE_KEYS: &'static [&'static str];
}

/// The state of the factory. Only one state can be active at a time because once
/// the [`DeviceStorage`] is created, there's no way to change the keys, so there's
/// no need to keep the set of keys anymore.
pub enum InitializationState<T, U = ()> {
    /// This represents the state of the factory before the first request to get
    /// [`DeviceStorage`]. It maintains a list of all keys that might be used for
    /// storage.
    Initializing(HashSet<&'static str>, U),
    /// A temporary state used to help in the conversion from [Initializing] to [Initialized]. This
    /// value is never intended to be read, but is necessary to keep the memory valid while
    /// ownership is taken of the values in [Initializing], but before the values in [Initialized]
    /// are ready.
    Partial,
    /// This represents the initialized state. When this is active, it is no longer
    /// possible to add new storage keys to [`DeviceStorage`].
    Initialized(Arc<T>),
}

impl<T> InitializationState<T, ()> {
    /// Construct the default `InitializationState`.
    pub fn new() -> Self {
        Self::Initializing(HashSet::new(), ())
    }
}

impl<T, U> Default for InitializationState<T, U>
where
    U: Default,
{
    fn default() -> Self {
        Self::Initializing(Default::default(), Default::default())
    }
}

impl<T> InitializationState<T, DirectoryProxy> {
    /// Construct the default `InitializationState`.
    pub fn with_storage_dir(storage_dir: DirectoryProxy) -> Self {
        Self::Initializing(HashSet::new(), storage_dir)
    }
}

/// Factory that vends out storage.
pub struct StashDeviceStorageFactory {
    store: StoreProxy,
    device_storage_cache: Mutex<InitializationState<DeviceStorage>>,
    inspect_handle: Arc<Mutex<StashInspectLogger>>,
}

impl StashDeviceStorageFactory {
    /// Construct a new instance of `StashDeviceStorageFactory`.
    pub fn new(
        store: StoreProxy,
        inspect_handle: Arc<Mutex<StashInspectLogger>>,
    ) -> StashDeviceStorageFactory {
        StashDeviceStorageFactory {
            store,
            device_storage_cache: Mutex::new(InitializationState::new()),
            inspect_handle,
        }
    }

    // Speeds up compilation by not needing to monomorphize this code for all T's.
    async fn initialize_storage(&self, keys: &'static [&'static str]) -> Result<(), Error> {
        match &mut *self.device_storage_cache.lock().await {
            InitializationState::Initializing(initial_keys, ()) => {
                for &key in keys {
                    let _ = initial_keys.insert(key);
                }
                Ok(())
            }
            InitializationState::Initialized(_) => {
                Err(format_err!("Cannot initialize an already accessed device storage"))
            }
            _ => unreachable!(),
        }
    }
}

#[async_trait::async_trait]
impl StorageFactory for StashDeviceStorageFactory {
    type Storage = DeviceStorage;

    async fn initialize<T>(&self) -> Result<(), Error>
    where
        T: StorageAccess<Storage = DeviceStorage>,
    {
        self.initialize_storage(T::STORAGE_KEYS).await
    }

    async fn get_store(&self) -> Arc<DeviceStorage> {
        let initialization = &mut *self.device_storage_cache.lock().await;
        match initialization {
            InitializationState::Initializing(initial_keys, ()) => {
                let device_storage = Arc::new(DeviceStorage::with_stash_proxy(
                    initial_keys.drain(),
                    || {
                        let (accessor_proxy, server_end) =
                            create_proxy().expect("failed to create proxy for stash");
                        self.store
                            .create_accessor(false, server_end)
                            .expect("failed to create accessor for stash");
                        accessor_proxy
                    },
                    Arc::clone(&self.inspect_handle),
                ));
                *initialization = InitializationState::Initialized(Arc::clone(&device_storage));
                device_storage
            }
            InitializationState::Initialized(device_storage) => Arc::clone(device_storage),
            _ => unreachable!(),
        }
    }
}

/// Factory that vends out storage.
pub struct FidlStorageFactory {
    migration_id: u64,
    device_storage_cache: Mutex<InitializationState<FidlStorage, DirectoryProxy>>,
}

impl FidlStorageFactory {
    /// Construct a new instance of `FidlStorageFactory`.
    pub fn new(migration_id: u64, storage_dir: DirectoryProxy) -> FidlStorageFactory {
        FidlStorageFactory {
            migration_id,
            device_storage_cache: Mutex::new(InitializationState::with_storage_dir(storage_dir)),
        }
    }

    // Speeds up compilation by not needing to monomorphize this code for all T's.
    async fn initialize_storage(&self, keys: &'static [&'static str]) -> Result<(), Error> {
        match &mut *self.device_storage_cache.lock().await {
            InitializationState::Initializing(initial_keys, _) => {
                for &key in keys {
                    let _ = initial_keys.insert(key);
                }
                Ok(())
            }
            InitializationState::Initialized(_) => {
                Err(format_err!("Cannot initialize an already accessed device storage"))
            }
            _ => unreachable!(),
        }
    }
}

#[async_trait::async_trait]
impl StorageFactory for FidlStorageFactory {
    type Storage = FidlStorage;

    async fn initialize<T>(&self) -> Result<(), Error>
    where
        T: StorageAccess<Storage = FidlStorage>,
    {
        self.initialize_storage(T::STORAGE_KEYS).await
    }

    async fn get_store(&self) -> Arc<FidlStorage> {
        let initialization = &mut *self.device_storage_cache.lock().await;
        match initialization {
            InitializationState::Initializing(..) => {
                let (initial_keys, storage_dir) =
                    match std::mem::replace(initialization, InitializationState::Partial) {
                        InitializationState::Initializing(initial_keys, storage_dir) => {
                            (initial_keys, storage_dir)
                        }
                        _ => unreachable!(),
                    };
                let migration_id = self.migration_id;
                let (device_storage, sync_tasks) = FidlStorage::with_file_proxy(
                    initial_keys.into_iter(),
                    storage_dir,
                    move |key| {
                        let temp_file_name = format!("{key}.tmp");
                        let file_name = format!("{key}_{migration_id}.pfidl");
                        Ok((temp_file_name, file_name))
                    },
                )
                .await
                .expect("failed to get storage");
                for task in sync_tasks {
                    task.detach();
                }

                let device_storage = Arc::new(device_storage);
                *initialization = InitializationState::Initialized(Arc::clone(&device_storage));
                device_storage
            }
            InitializationState::Initialized(device_storage) => Arc::clone(device_storage),
            _ => unreachable!(),
        }
    }
}
