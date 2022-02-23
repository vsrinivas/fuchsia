// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::inspect::stash_logger::StashInspectLogger;
use crate::storage::UpdateState;
use anyhow::{format_err, Context, Error};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_stash::{StoreAccessorProxy, StoreProxy, Value};
use fuchsia_async::{Task, Timer, WakeupTime};
use fuchsia_syslog::fx_log_err;
use futures::channel::mpsc::UnboundedSender;
use futures::future::OptionFuture;
use futures::lock::Mutex;
use futures::{FutureExt, StreamExt};
use serde::de::DeserializeOwned;
use serde::Serialize;
use std::any::Any;
use std::borrow::Cow;
use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use std::time::{Duration, Instant};

const SETTINGS_PREFIX: &str = "settings";

/// Minimum amount of time between Flush calls to Stash, in milliseconds. The Flush call triggers
/// file I/O which is slow. If we call flush too often, we can overwhelm Stash, which eventually
/// causes the kernel to crash our service due to filling up the channel.
const MIN_FLUSH_INTERVAL_MS: u64 = 500;

/// Stores device level settings in persistent storage.
/// User level settings should not use this.
pub struct DeviceStorage {
    /// Map of [`DeviceStorageCompatible`] keys to their typed storage.
    typed_storage_map: HashMap<&'static str, TypedStorage>,

    /// If true, reads will be returned from the data in memory rather than reading from storage.
    caching_enabled: bool,

    /// If true, writes to the underlying storage will only occur at most every
    /// MIN_WRITE_INTERVAL_MS.
    debounce_writes: bool,

    /// Handle used to write stash failures to inspect.
    inspect_handle: Arc<Mutex<StashInspectLogger>>,
}

/// A wrapper for managing all communication and caching for one particular type of data being
/// stored. The actual types are erased.
struct TypedStorage {
    /// Sender to communicate with task loop that handles flushes.
    flush_sender: UnboundedSender<()>,

    /// Cached storage managed through interior mutability.
    cached_storage: Mutex<CachedStorage>,
}

/// `CachedStorage` abstracts over a cached value that's read from and written
/// to some backing store.
struct CachedStorage {
    /// Cache for the most recently read or written value.
    current_data: Option<Box<dyn Any + Send + Sync>>,

    /// Stash connection for this particular type's stash storage.
    stash_proxy: StoreAccessorProxy,
}

/// Structs that can be stored in device storage should derive the Serialize, Deserialize, and
/// Clone traits, as well as provide constants.
/// KEY should be unique the struct, usually the name of the struct itself.
/// DEFAULT_VALUE will be the value returned when nothing has yet been stored.
///
/// Anything that implements this should not introduce breaking changes with the same key.
/// Clients that want to make a breaking change should create a new structure with a new key and
/// implement conversion/cleanup logic. Adding optional fields to a struct is not breaking, but
/// removing fields, renaming fields, or adding non-optional fields are.
///
/// The [`Storage`] trait has [`Send`] and [`Sync`] requirements, so they have to be carried here
/// as well. This was not necessary before because rust could determine the additional trait
/// requirements at compile-time just for when the [`Storage`] trait was used. We don't get that
/// benefit anymore once we hide the type.
///
/// [`Storage`]: super::setting_handler::persist::Storage
pub trait DeviceStorageCompatible:
    Serialize + DeserializeOwned + Clone + PartialEq + Any + Send + Sync
{
    fn default_value() -> Self;

    fn deserialize_from(value: &str) -> Self {
        Self::extract(&value).unwrap_or_else(|error| {
            fx_log_err!("error occurred:{:?}", error);
            Self::default_value()
        })
    }

    fn extract(value: &str) -> Result<Self, Error> {
        serde_json::from_str(&value).map_err(|_| format_err!("could not deserialize"))
    }

    fn serialize_to(&self) -> String {
        serde_json::to_string(self).expect("value should serialize")
    }

    const KEY: &'static str;
}

/// This trait represents types that can be converted into a storable type. It's also important
/// that the type it is transformed into can also be converted back into this type. This reverse
/// conversion is used to populate the fields of the original type with the stored values plus
/// defaulting the other fields that, e.g. might later be populated from hardware APIs.
///
/// # Example
/// ```
/// // Struct used in controllers.
/// struct SomeSettingInfo {
///     storable_field: u8,
///     hardware_backed_field: String,
/// }
///
/// // Struct only used for storage.
/// #[derive(Serialize, Deserialize, PartialEq, Clone)]
/// struct StorableSomeSettingInfo {
///     storable_field: u8,
/// }
///
/// // Impl compatible for the storable type.
/// impl DeviceStorageCompatible for StorableSomeSettingInfo {
///     const KEY: &'static str = "some_setting_info";
///
///     fn default_value() -> Self {
///         Self { storable_field: 1, }
///     }
/// }
///
/// // Impl convertible for controller type.
/// impl DeviceStorageConvertible for SomeSettingInfo {
///     type Storable = StorableSomeSettingInfo;
///     fn get_storable(&self) -> Cow<'_, Self::Storable> {
///         Cow::Owned(Self {
///             storable_field: self.storable_field,
///             hardware_backed_field: String::new()
///         })
///     }
/// }
///
/// // This impl helps us convert from the storable version to the
/// // controller version of the struct. Hardware fields should be backed
/// // by default or usable values.
/// impl Into<SomeSettingInfo> for StorableSomeSettingInfo {
///     fn into(self) -> SomeSettingInfo {
///         SomeSettingInfo {
///             storable_field: self.storable_field,
///             hardware_backed_field: String::new(),
///         }
///     }
/// }
///
/// ```
pub trait DeviceStorageConvertible: Sized {
    /// The type that will be used for storing the data.
    type Storable: DeviceStorageCompatible + Into<Self>;

    /// Convert `self` into its storable version.
    // The reason we don't take ownership here is that the setting handler uses the original value
    // to send a message on the message hub for when the change is written. Serializing also only
    // borrows the data and doesn't need to own it. When `Storable` is `Self`, we only need to keep
    // the borrow on self, but when the types differ, then we need to own the newly constructed
    // type.
    fn get_storable(&self) -> Cow<'_, Self::Storable>;
}

// Any type that is storage compatible is also storage convertible (it can convert to itself!).
impl<T> DeviceStorageConvertible for T
where
    T: DeviceStorageCompatible,
{
    type Storable = T;

    fn get_storable(&self) -> Cow<'_, Self::Storable> {
        Cow::Borrowed(self)
    }
}

/// A trait for describing which storages an item needs access to.
/// See [StashDeviceStorageFactory::initialize] for usage.
pub trait DeviceStorageAccess {
    /// This field should be populated by items that implement [`DeviceStorageCompatible`].
    ///
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
    /// impl DeviceStorageAccess for SomeItem {
    ///     const STORAGE_KEYS: &'static [&'static str] = &[StorageItem::KEY];
    /// }
    /// ```
    const STORAGE_KEYS: &'static [&'static str];
}

impl DeviceStorage {
    /// Construct a device storage from the iteratable item, which will produce the keys for
    /// storage, and from a generator that will produce a stash proxy given a particular key.
    pub(crate) fn with_stash_proxy<I, G>(
        iter: I,
        stash_generator: G,
        inspect_handle: Arc<Mutex<StashInspectLogger>>,
    ) -> Self
    where
        I: IntoIterator<Item = &'static str>,
        G: Fn() -> StoreAccessorProxy,
    {
        let typed_storage_map = iter
            .into_iter()
            .map({
                let inspect_handle = Arc::clone(&inspect_handle);
                move |key| {
                    // Generate a separate stash proxy for each key.
                    let (flush_sender, flush_receiver) = futures::channel::mpsc::unbounded::<()>();
                    let stash_proxy = stash_generator();

                    let storage = TypedStorage {
                        flush_sender,
                        cached_storage: Mutex::new(CachedStorage {
                            current_data: None,
                            stash_proxy: stash_proxy.clone(),
                        }),
                    };

                    let stash_proxy_clone = stash_proxy.clone();
                    let inspect_handle = Arc::clone(&inspect_handle);
                    // Each key has an independent flush queue.
                    Task::spawn(async move {
                        const MIN_FLUSH_DURATION: Duration =
                            Duration::from_millis(MIN_FLUSH_INTERVAL_MS);
                        let mut has_pending_flush = false;

                        // The time of the last flush. Initialized to MIN_FLUSH_INTERVAL_MS before the
                        // current time so that the first flush always goes through, no matter the
                        // timing.
                        let mut last_flush: Instant = Instant::now() - MIN_FLUSH_DURATION;

                        // Timer for flush cooldown. OptionFuture allows us to wait on the future even
                        // if it's None.
                        let mut next_flush_timer: OptionFuture<Timer> = None.into();
                        let mut next_flush_timer_fuse = next_flush_timer.fuse();

                        let flush_fuse = flush_receiver.fuse();

                        futures::pin_mut!(flush_fuse);
                        loop {
                            futures::select! {
                                _ = flush_fuse.select_next_some() => {
                                    // Received a request to do a flush.
                                    let now = Instant::now();
                                    let next_flush_time = if now - last_flush > MIN_FLUSH_DURATION {
                                        // Last flush happened more than MIN_FLUSH_INTERVAL_MS ago,
                                        // flush immediately in next iteration of loop.
                                        now
                                    } else {
                                        // Last flush was less than MIN_FLUSH_INTERVAL_MS ago, schedule
                                        // it accordingly. It's okay if the time is in the past, Timer
                                        // will still trigger on the next loop iteration.
                                        last_flush + MIN_FLUSH_DURATION
                                    };
                                    has_pending_flush = true;
                                    next_flush_timer = Some(Timer::new(next_flush_time.into_time()))
                                        .into();
                                    next_flush_timer_fuse = next_flush_timer.fuse();
                                }

                                _ = next_flush_timer_fuse => {
                                    // Timer triggered, check for pending flushes.
                                    if has_pending_flush {
                                        DeviceStorage::stash_flush(
                                            &stash_proxy_clone,
                                            Arc::clone(&inspect_handle),
                                            &key.to_string()).await;
                                        last_flush = Instant::now();
                                        has_pending_flush = false;
                                    }
                                }

                                complete => break,
                            }
                        }
                    })
                    .detach();
                    (key, storage)
                }
            })
            .collect();
        DeviceStorage {
            caching_enabled: true,
            debounce_writes: true,
            typed_storage_map,
            inspect_handle,
        }
    }

    #[cfg(test)]
    fn set_caching_enabled(&mut self, enabled: bool) {
        self.caching_enabled = enabled;
    }

    #[cfg(test)]
    fn set_debounce_writes(&mut self, debounce: bool) {
        self.debounce_writes = debounce;
    }

    /// Triggers a flush on the given stash proxy.
    async fn stash_flush(
        stash_proxy: &StoreAccessorProxy,
        inspect_handle: Arc<Mutex<StashInspectLogger>>,
        setting_key: &String,
    ) {
        if matches!(stash_proxy.flush().await, Ok(Err(_)) | Err(_)) {
            // TODO(fxbug.dev/89083): save a record of the recent error messages as well.
            // Record the write failure to inspect.
            inspect_handle.lock().await.record_flush_failure(setting_key);
        }
    }

    async fn inner_write(
        &self,
        key: &'static str,
        new_value: String,
        data_as_any: Box<dyn Any + Send + Sync>,
        mapping_fn: Box<dyn FnOnce(&(dyn Any + Send + Sync)) -> String + Send>,
    ) -> Result<UpdateState, Error> {
        let typed_storage = self
            .typed_storage_map
            .get(key)
            .ok_or_else(|| format_err!("Invalid data keyed by {}", key))?;
        let mut cached_storage = typed_storage.cached_storage.lock().await;
        let mut maybe_init;
        let cached_value = {
            maybe_init = cached_storage
                .current_data
                .as_deref()
                // Get the data as a shared reference so we don't move out of the option.
                .map(mapping_fn);
            if maybe_init.is_none() {
                let stash_key = prefixed(key);
                if let Some(stash_value) =
                    cached_storage.stash_proxy.get_value(&stash_key).await.unwrap_or_else(|_| {
                        panic!("failed to get value from stash for {:?}", stash_key)
                    })
                {
                    if let Value::Stringval(string_value) = &*stash_value {
                        maybe_init = Some(string_value.clone());
                    } else {
                        panic!("Unexpected type for key found in stash");
                    }
                }
            }
            maybe_init.as_ref()
        };

        Ok(if cached_value != Some(&new_value) {
            let mut serialized = Value::Stringval(new_value);
            let key = prefixed(key);
            cached_storage.stash_proxy.set_value(&key, &mut serialized)?;
            if !self.debounce_writes {
                // Not debouncing writes for testing, just flush immediately.
                DeviceStorage::stash_flush(
                    &cached_storage.stash_proxy,
                    Arc::clone(&self.inspect_handle),
                    &key,
                )
                .await;
            } else {
                typed_storage.flush_sender.unbounded_send(()).with_context(|| {
                    format!("flush_sender failed to send flush message, associated key is {}", key)
                })?;
            }
            cached_storage.current_data = Some(data_as_any);
            UpdateState::Updated
        } else {
            UpdateState::Unchanged
        })
    }

    /// Write `new_value` to storage. The write will be persisted to disk at a set interval.
    pub(crate) async fn write<T>(&self, new_value: &T) -> Result<UpdateState, Error>
    where
        T: DeviceStorageCompatible,
    {
        self.inner_write(
            T::KEY,
            new_value.serialize_to(),
            Box::new(new_value.clone()) as Box<dyn Any + Send + Sync>,
            Box::new(|any: &(dyn Any + Send + Sync)| {
                // Attempt to downcast the `dyn Any` to its original type. If `T` was not its
                // original type, then we want to panic because there's a compile-time issue
                // with overlapping keys.
                let value = any.downcast_ref::<T>().expect(
                    "Type mismatch even though keys match. Two different\
                                        types have the same key value",
                );
                value.serialize_to()
            }),
        )
        .await
    }

    #[cfg(test)]
    /// Test-only method to write directly to stash without touching the cache. This is used for
    /// setting up data as if it existed on disk before the connection to stash was made.
    async fn write_str(&self, key: &'static str, value: String) -> Result<(), Error> {
        let typed_storage =
            self.typed_storage_map.get(key).expect("Did not request an initialized key");
        let cached_storage = typed_storage.cached_storage.lock().await;
        let mut value = Value::Stringval(value);
        cached_storage.stash_proxy.set_value(&prefixed(key), &mut value)?;
        typed_storage.flush_sender.unbounded_send(()).unwrap();
        Ok(())
    }

    /// Gets the latest value cached locally, or loads the value from storage.
    /// Doesn't support multiple concurrent callers of the same struct.
    pub(crate) async fn get<T>(&self) -> T
    where
        T: DeviceStorageCompatible,
    {
        let typed_storage = self
            .typed_storage_map
            .get(T::KEY)
            // TODO(fxbug.dev/67371) Replace this with an error result.
            .unwrap_or_else(|| panic!("Invalid data keyed by {}", T::KEY));
        let mut cached_storage = typed_storage.cached_storage.lock().await;
        if cached_storage.current_data.is_none() || !self.caching_enabled {
            let stash_key = prefixed(T::KEY);
            if let Some(stash_value) =
                cached_storage.stash_proxy.get_value(&stash_key).await.unwrap_or_else(|_| {
                    panic!("failed to get value from stash for {:?}", stash_key)
                })
            {
                if let Value::Stringval(string_value) = &*stash_value {
                    cached_storage.current_data =
                        Some(Box::new(T::deserialize_from(&string_value))
                            as Box<dyn Any + Send + Sync>);
                } else {
                    panic!("Unexpected type for key found in stash");
                }
            } else {
                cached_storage.current_data =
                    Some(Box::new(T::default_value()) as Box<dyn Any + Send + Sync>);
            }
        }

        cached_storage
            .current_data
            .as_ref()
            .expect("should always have a value")
            .downcast_ref::<T>()
            .expect(
                "Type mismatch even though keys match. Two different types have the same key\
                     value",
            )
            .clone()
    }
}

/// `DeviceStorageFactory` abstracts over how to initialize and retrieve the `DeviceStorage`
/// instance.
#[async_trait::async_trait]
pub trait DeviceStorageFactory {
    /// Initialize the storage to be able to manage storage for objects of type T.
    /// This will return an Error once `get_store` is called the first time.
    async fn initialize<T>(&self) -> Result<(), Error>
    where
        T: DeviceStorageAccess;

    /// Retrieve the store singleton instance.
    async fn get_store(&self) -> Arc<DeviceStorage>;
}

/// The state of the factory. Only one state can be active at a time because once
/// the [`DeviceStorage`] is created, there's no way to change the keys, so there's
/// no need to keep the set of keys anymore.
enum InitializationState {
    /// This represents the state of the factory before the first request to get
    /// [`DeviceStorage`]. It maintains a list of all keys that might be used for
    /// storage.
    Initializing(HashSet<&'static str>),
    /// This represents the initialized state. When this is active, it is no longer
    /// possible to add new storage keys to [`DeviceStorage`].
    Initialized(Arc<DeviceStorage>),
}

impl InitializationState {
    /// Construct the default `InitializationState`.
    fn new() -> Self {
        Self::Initializing(HashSet::new())
    }
}

/// Factory that vends out storage.
pub struct StashDeviceStorageFactory {
    store: StoreProxy,
    device_storage_cache: Mutex<InitializationState>,
    inspect_handle: Arc<Mutex<StashInspectLogger>>,
}

impl StashDeviceStorageFactory {
    /// Construct a new instance of `StashDeviceStorageFactory`.
    pub fn new(
        identity: &str,
        store: StoreProxy,
        inspect_handle: Arc<Mutex<StashInspectLogger>>,
    ) -> StashDeviceStorageFactory {
        store.identify(identity).expect("was not able to identify with stash");
        StashDeviceStorageFactory {
            store,
            device_storage_cache: Mutex::new(InitializationState::new()),
            inspect_handle,
        }
    }

    // Speeds up compilation by not needing to monomorphize this code for all T's.
    async fn initialize_storage(&self, keys: &'static [&'static str]) -> Result<(), Error> {
        match &mut *self.device_storage_cache.lock().await {
            InitializationState::Initializing(initial_keys) => {
                for &key in keys {
                    let _ = initial_keys.insert(key);
                }
                Ok(())
            }
            InitializationState::Initialized(_) => {
                Err(format_err!("Cannot initialize an already accessed device storage"))
            }
        }
    }
}

#[async_trait::async_trait]
impl DeviceStorageFactory for StashDeviceStorageFactory {
    async fn initialize<T>(&self) -> Result<(), Error>
    where
        T: DeviceStorageAccess,
    {
        self.initialize_storage(T::STORAGE_KEYS).await
    }

    async fn get_store(&self) -> Arc<DeviceStorage> {
        let initialization = &mut *self.device_storage_cache.lock().await;
        match initialization {
            InitializationState::Initializing(initial_keys) => {
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
            InitializationState::Initialized(device_storage) => Arc::clone(&device_storage),
        }
    }
}

fn prefixed(input_string: &str) -> String {
    format!("{}_{}", SETTINGS_PREFIX, input_string)
}

#[cfg(test)]
pub(crate) mod testing {
    use std::sync::Arc;

    use fidl_fuchsia_stash::{StoreAccessorMarker, StoreAccessorRequest};
    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use futures::prelude::*;

    use crate::inspect::stash_logger::StashInspectLoggerHandle;

    use super::*;

    #[derive(PartialEq)]
    pub(crate) enum StashAction {
        Get,
        Flush,
        Set,
    }

    pub(crate) struct StashStats {
        actions: Vec<StashAction>,
    }

    impl StashStats {
        pub(crate) fn new() -> Self {
            StashStats { actions: Vec::new() }
        }

        pub(crate) fn record(&mut self, action: StashAction) {
            self.actions.push(action);
        }
    }

    /// Storage that does not write to disk, for testing.
    pub(crate) struct InMemoryStorageFactory {
        initial_data: HashMap<&'static str, String>,
        device_storage_cache: Mutex<InitializationState>,
        inspect_handle: Arc<Mutex<StashInspectLogger>>,
    }

    impl Default for InMemoryStorageFactory {
        fn default() -> Self {
            Self::new()
        }
    }

    const INITIALIZATION_ERROR: &str =
        "Cannot initialize an already accessed device storage. Make \
        sure you're not retrieving a DeviceStorage before passing InMemoryStorageFactory to an \
        EnvironmentBuilder. That must be done after. If you need initial data, use \
        InMemoryStorageFactory::with_initial_data";

    impl InMemoryStorageFactory {
        /// Constructs a new `InMemoryStorageFactory` with the ability to create a [`DeviceStorage`]
        /// that can only read and write to the storage keys passed in.
        pub(crate) fn new() -> Self {
            InMemoryStorageFactory {
                initial_data: HashMap::new(),
                device_storage_cache: Mutex::new(InitializationState::new()),
                inspect_handle: StashInspectLoggerHandle::new().logger,
            }
        }

        /// Constructs a new `InMemoryStorageFactory` with the data written to stash. This simulates
        /// the data existing in storage before the RestoreAgent reads it.
        pub(crate) fn with_initial_data<T>(data: &T) -> Self
        where
            T: DeviceStorageCompatible,
        {
            let mut map = HashMap::new();
            let _ = map.insert(T::KEY, serde_json::to_string(data).unwrap());
            InMemoryStorageFactory {
                initial_data: map,
                device_storage_cache: Mutex::new(InitializationState::new()),
                inspect_handle: StashInspectLoggerHandle::new().logger,
            }
        }

        /// Helper method to simplify setup for `InMemoryStorageFactory` in tests.
        pub(crate) async fn initialize_storage<T>(&self)
        where
            T: DeviceStorageCompatible,
        {
            self.initialize_storage_for_key(T::KEY).await;
        }

        async fn initialize_storage_for_key(&self, key: &'static str) {
            match &mut *self.device_storage_cache.lock().await {
                InitializationState::Initializing(initial_keys) => {
                    let _ = initial_keys.insert(key);
                }
                InitializationState::Initialized(_) => panic!("{}", INITIALIZATION_ERROR),
            }
        }

        async fn initialize_storage_for_keys(&self, keys: &'static [&'static str]) {
            match &mut *self.device_storage_cache.lock().await {
                InitializationState::Initializing(initial_keys) => {
                    for &key in keys {
                        let _ = initial_keys.insert(key);
                    }
                }
                InitializationState::Initialized(_) => panic!("{}", INITIALIZATION_ERROR),
            }
        }

        /// Retrieve the [`DeviceStorage`] singleton.
        pub(crate) async fn get_device_storage(&self) -> Arc<DeviceStorage> {
            let initialization = &mut *self.device_storage_cache.lock().await;
            match initialization {
                InitializationState::Initializing(initial_keys) => {
                    let mut device_storage = DeviceStorage::with_stash_proxy(
                        initial_keys.drain(),
                        || {
                            let (stash_proxy, _) = spawn_stash_proxy();
                            stash_proxy
                        },
                        Arc::clone(&self.inspect_handle),
                    );
                    device_storage.set_caching_enabled(false);
                    device_storage.set_debounce_writes(false);

                    // write initial data to storage
                    for (&key, data) in &self.initial_data {
                        device_storage
                            .write_str(key, data.clone())
                            .await
                            .expect("Failed to write initial data");
                    }

                    let device_storage = Arc::new(device_storage);
                    *initialization = InitializationState::Initialized(Arc::clone(&device_storage));
                    device_storage
                }
                InitializationState::Initialized(device_storage) => Arc::clone(&device_storage),
            }
        }
    }

    #[async_trait::async_trait]
    impl DeviceStorageFactory for InMemoryStorageFactory {
        async fn initialize<T>(&self) -> Result<(), Error>
        where
            T: DeviceStorageAccess,
        {
            self.initialize_storage_for_keys(T::STORAGE_KEYS).await;
            Ok(())
        }

        async fn get_store(&self) -> Arc<DeviceStorage> {
            self.get_device_storage().await
        }
    }

    fn spawn_stash_proxy() -> (StoreAccessorProxy, Arc<Mutex<StashStats>>) {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();
        let stats = Arc::new(Mutex::new(StashStats::new()));
        let stats_clone = stats.clone();
        fasync::Task::spawn(async move {
            let mut stored_value: Option<Value> = None;
            let mut stored_key: Option<String> = None;

            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key, responder } => {
                        stats_clone.lock().await.record(StashAction::Get);
                        if let Some(key_string) = stored_key {
                            assert_eq!(key, key_string);
                        }
                        stored_key = Some(key);

                        responder.send(stored_value.as_mut()).unwrap();
                    }
                    StoreAccessorRequest::SetValue { key, val, control_handle: _ } => {
                        stats_clone.lock().await.record(StashAction::Set);
                        if let Some(key_string) = stored_key {
                            assert_eq!(key, key_string);
                        }
                        stored_key = Some(key);
                        stored_value = Some(val);
                    }
                    StoreAccessorRequest::Flush { responder } => {
                        stats_clone.lock().await.record(StashAction::Flush);
                        let _ = responder.send(&mut Ok(()));
                    }
                    _ => {}
                }
            }
        })
        .detach();
        (stash_proxy, stats)
    }
}

#[cfg(test)]
mod tests {
    use std::convert::TryInto;
    use std::marker::Unpin;
    use std::sync::Arc;
    use std::task::Poll;

    use assert_matches::assert_matches;
    use fidl_fuchsia_stash::{
        FlushError, StoreAccessorMarker, StoreAccessorRequest, StoreAccessorRequestStream,
    };
    use fuchsia_async as fasync;
    use fuchsia_async::{TestExecutor, Time};
    use fuchsia_inspect::assert_data_tree;
    use futures::prelude::*;
    use serde::{Deserialize, Serialize};

    use testing::*;

    use crate::inspect::stash_logger::StashInspectLoggerHandle;
    use crate::tests::helpers::move_executor_forward_and_get;

    use super::*;

    const VALUE0: i32 = 3;
    const VALUE1: i32 = 33;
    const VALUE2: i32 = 128;

    #[derive(PartialEq, Clone, Serialize, Deserialize, Debug)]
    struct TestStruct {
        value: i32,
    }

    const STORE_KEY: &str = "settings_testkey";

    impl DeviceStorageCompatible for TestStruct {
        const KEY: &'static str = "testkey";

        fn default_value() -> Self {
            TestStruct { value: VALUE0 }
        }
    }

    /// Advances `future` until `executor` finishes. Panics if the end result was a stall.
    fn advance_executor<F>(executor: &mut TestExecutor, mut future: &mut F)
    where
        F: Future + Unpin,
    {
        loop {
            executor.wake_main_future();
            match executor.run_one_step(&mut future) {
                Some(Poll::Ready(_)) => return,
                None => panic!("TestExecutor stalled!"),
                Some(Poll::Pending) => {}
            }
        }
    }

    /// Verifies that a SetValue call was sent to stash with the given value.
    async fn verify_stash_set(stash_stream: &mut StoreAccessorRequestStream, expected_value: i32) {
        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::SetValue { key, val, control_handle: _ }) => {
                assert_eq!(key, STORE_KEY);
                if let Value::Stringval(string_value) = val {
                    let input_value = TestStruct::deserialize_from(&string_value);
                    assert_eq!(input_value.value, expected_value);
                } else {
                    panic!("Unexpected type for key found in stash");
                }
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    /// Verifies that a SetValue call was sent to stash with the given value.
    async fn validate_stash_get_and_respond(
        stash_stream: &mut StoreAccessorRequestStream,
        response: String,
    ) {
        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::GetValue { key, responder }) => {
                assert_eq!(key, STORE_KEY);
                responder
                    .send(Some(&mut Value::Stringval(response)))
                    .expect("unable to send response");
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    /// Verifies that a Flush call was sent to stash.
    async fn verify_stash_flush(stash_stream: &mut StoreAccessorRequestStream) {
        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::Flush { responder }) => {
                let _ = responder.send(&mut Ok(()));
            } // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    /// Verifies that a Flush call was sent to stash, and send back a failure.
    async fn fail_stash_flush(stash_stream: &mut StoreAccessorRequestStream) {
        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::Flush { responder }) => {
                let _ = responder.send(&mut Err(FlushError::CommitFailed));
            } // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_get() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::Task::spawn(async move {
            let value_to_get = TestStruct { value: VALUE1 };

            #[allow(clippy::single_match)]
            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key, responder } => {
                        assert_eq!(key, STORE_KEY);
                        let mut response = Value::Stringval(value_to_get.serialize_to());

                        responder.send(Some(&mut response)).unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();

        let storage = DeviceStorage::with_stash_proxy(
            vec![TestStruct::KEY],
            move || stash_proxy.clone(),
            StashInspectLoggerHandle::new().logger,
        );
        let result = storage.get::<TestStruct>().await;

        assert_eq!(result.value, VALUE1);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_get_default() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::Task::spawn(async move {
            #[allow(clippy::single_match)]
            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key: _, responder } => {
                        responder.send(None).unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();

        let storage = DeviceStorage::with_stash_proxy(
            vec![TestStruct::KEY],
            move || stash_proxy.clone(),
            StashInspectLoggerHandle::new().logger,
        );
        let result = storage.get::<TestStruct>().await;

        assert_eq!(result.value, VALUE0);
    }

    // For an invalid stash value, the get() method should return the default value.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_invalid_stash() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::Task::spawn(async move {
            #[allow(clippy::single_match)]
            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key: _, responder } => {
                        let mut response = Value::Stringval("invalid value".to_string());
                        responder.send(Some(&mut response)).unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();

        let storage = DeviceStorage::with_stash_proxy(
            vec![TestStruct::KEY],
            move || stash_proxy.clone(),
            StashInspectLoggerHandle::new().logger,
        );

        let result = storage.get::<TestStruct>().await;

        assert_eq!(result.value, VALUE0);
    }

    // Verifies that stash flush failures are written to inspect.
    #[test]
    fn test_flush_fail_writes_to_inspect() {
        let written_value = VALUE2;
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");

        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let storage = DeviceStorage::with_stash_proxy(
            vec![TestStruct::KEY],
            move || stash_proxy.clone(),
            StashInspectLoggerHandle::new().logger,
        );

        // Write to device storage.
        let value_to_write = TestStruct { value: written_value };
        let write_future = storage.write(&value_to_write);
        futures::pin_mut!(write_future);

        // Initial cache check is done if no read was ever performed.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

        {
            let respond_future = validate_stash_get_and_respond(
                &mut stash_stream,
                serde_json::to_string(&TestStruct::default_value()).unwrap(),
            );
            futures::pin_mut!(respond_future);
            advance_executor(&mut executor, &mut respond_future);
        }

        // Write request finishes immediately.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Ready(Ok(_)));

        // Set request is received immediately on write.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, written_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // Start listening for the flush request.
        let flush_future = fail_stash_flush(&mut stash_stream);
        futures::pin_mut!(flush_future);

        // Flush is received without a wait. Due to the way time works with executors, if there was
        // a delay, the test would stall since time never advances.
        advance_executor(&mut executor, &mut flush_future);

        // Queue up a second write to guarantee that CachedStorage has written the failure to
        // inspect.
        {
            let value_to_write = TestStruct { value: VALUE1 };
            let write_future = storage.write(&value_to_write);
            futures::pin_mut!(write_future);
            assert_matches!(
                executor.run_until_stalled(&mut write_future),
                Poll::Ready(Result::Ok(_))
            );
        }

        let logger_handle = StashInspectLoggerHandle::new();
        let lock_future = logger_handle.logger.lock();
        let inspector = move_executor_forward_and_get(
            &mut executor,
            lock_future,
            "Couldn't get inspect logger lock",
        )
        .inspector;
        assert_data_tree!(inspector, root: {
            stash_failures: {
                testkey: {
                    count: 1u64,
                }
            }
        });
    }

    // Test that an initial write to DeviceStorage causes a SetValue and Flush to Stash
    // without any wait.
    #[test]
    fn test_first_write_flushes_immediately() {
        let written_value = VALUE2;
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");

        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let storage = DeviceStorage::with_stash_proxy(
            vec![TestStruct::KEY],
            move || stash_proxy.clone(),
            StashInspectLoggerHandle::new().logger,
        );

        // Write to device storage.
        let value_to_write = TestStruct { value: written_value };
        let write_future = storage.write(&value_to_write);
        futures::pin_mut!(write_future);

        // Initial cache check is done if no read was ever performed.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

        {
            let respond_future = validate_stash_get_and_respond(
                &mut stash_stream,
                serde_json::to_string(&TestStruct::default_value()).unwrap(),
            );
            futures::pin_mut!(respond_future);
            advance_executor(&mut executor, &mut respond_future);
        }

        // Write request finishes immediately.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Ready(Ok(_)));

        // Set request is received immediately on write.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, written_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // Start listening for the flush request.
        let flush_future = verify_stash_flush(&mut stash_stream);
        futures::pin_mut!(flush_future);

        // Flush is received without a wait. Due to the way time works with executors, if there was
        // a delay, the test would stall since time never advances.
        advance_executor(&mut executor, &mut flush_future);
    }

    #[derive(Copy, Clone, PartialEq, Serialize, Deserialize)]
    struct WrongStruct;

    impl DeviceStorageCompatible for WrongStruct {
        const KEY: &'static str = "WRONG_STRUCT";

        fn default_value() -> Self {
            WrongStruct
        }
    }

    // Test that an initial write to DeviceStorage causes a SetValue and Flush to Stash
    // without any wait.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_with_mismatch_type_returns_error() {
        let (stash_proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let spawned = fasync::Task::spawn(async move {
            while let Some(request) = stream.next().await {
                match request {
                    Ok(StoreAccessorRequest::GetValue { key, responder }) => {
                        assert_eq!(key, STORE_KEY);
                        let _ = responder.send(Some(&mut Value::Stringval(
                            serde_json::to_string(&TestStruct { value: VALUE2 }).unwrap(),
                        )));
                    }
                    Ok(StoreAccessorRequest::SetValue { key, .. }) => {
                        assert_eq!(key, STORE_KEY);
                    }
                    _ => panic!("Unexpected request {:?}", request),
                }
            }
        });

        let storage = DeviceStorage::with_stash_proxy(
            vec![TestStruct::KEY],
            move || stash_proxy.clone(),
            StashInspectLoggerHandle::new().logger,
        );

        // Write successfully to storage once.
        let result = storage.write(&TestStruct { value: VALUE2 }).await;
        assert!(result.is_ok());

        // Write to device storage again with a different type to validate that the type can't
        // be changed.
        let result = storage.write(&WrongStruct).await;
        assert_matches!(result, Err(e) if e.to_string() == "Invalid data keyed by WRONG_STRUCT");

        drop(storage);
        spawned.await;
    }

    // Test that multiple writes to DeviceStorage will cause a SetValue each time, but will only
    // Flush to Stash at an interval.
    #[test]
    fn test_multiple_write_debounce() {
        // Custom executor for this test so that we can advance the clock arbitrarily and verify the
        // state of the executor at any given point.
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
        executor.set_fake_time(Time::from_nanos(0));

        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let storage = DeviceStorage::with_stash_proxy(
            vec![TestStruct::KEY],
            move || stash_proxy.clone(),
            StashInspectLoggerHandle::new().logger,
        );

        let first_value = VALUE1;
        let second_value = VALUE2;

        // First write finishes immediately.
        {
            let value_to_write = TestStruct { value: first_value };
            let write_future = storage.write(&value_to_write);
            futures::pin_mut!(write_future);

            // Initial cache check is done if no read was ever performed.
            assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

            {
                let respond_future = validate_stash_get_and_respond(
                    &mut stash_stream,
                    serde_json::to_string(&TestStruct::default_value()).unwrap(),
                );
                futures::pin_mut!(respond_future);
                advance_executor(&mut executor, &mut respond_future);
            }

            assert_matches!(
                executor.run_until_stalled(&mut write_future),
                Poll::Ready(Result::Ok(_))
            );
        }

        // First set request is received immediately on write.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, first_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // First flush request is received.
        {
            let flush_future = verify_stash_flush(&mut stash_stream);
            futures::pin_mut!(flush_future);
            advance_executor(&mut executor, &mut flush_future);
        }

        // Now we repeat the process with a second write request, which will need to advance the
        // fake time due to the timer.

        // Second write finishes immediately.
        {
            let value_to_write = TestStruct { value: second_value };
            let write_future = storage.write(&value_to_write);
            futures::pin_mut!(write_future);
            assert_matches!(
                executor.run_until_stalled(&mut write_future),
                Poll::Ready(Result::Ok(_))
            );
        }

        // Second set request finishes immediately on write.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, second_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // Start waiting for flush request.
        let flush_future = verify_stash_flush(&mut stash_stream);
        futures::pin_mut!(flush_future);

        // TextExecutor stalls due to waiting on timer to finish.
        assert_matches!(executor.run_until_stalled(&mut flush_future), Poll::Pending);

        // Advance time to 1ms before the flush triggers.
        executor.set_fake_time(Time::from_nanos(
            ((MIN_FLUSH_INTERVAL_MS - 1) * 10_u64.pow(6)).try_into().unwrap(),
        ));

        // TextExecutor is still waiting on the time to finish.
        assert_matches!(executor.run_until_stalled(&mut flush_future), Poll::Pending);

        // Advance time so that the flush will trigger.
        executor.set_fake_time(Time::from_nanos(
            (MIN_FLUSH_INTERVAL_MS * 10_u64.pow(6)).try_into().unwrap(),
        ));

        // Stash receives a flush request after one timer cycle and the future terminates.
        advance_executor(&mut executor, &mut flush_future);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_in_memory_storage() {
        let factory = InMemoryStorageFactory::new();
        factory.initialize_storage::<TestStruct>().await;

        let store_1 = factory.get_device_storage().await;
        let store_2 = factory.get_device_storage().await;

        // Write initial data through first store.
        let test_struct = TestStruct { value: VALUE0 };

        // Ensure writing from store1 ends up in store2
        test_write_propagation(store_1.clone(), store_2.clone(), test_struct).await;

        let test_struct_2 = TestStruct { value: VALUE1 };
        // Ensure writing from store2 ends up in store1
        test_write_propagation(store_2.clone(), store_1.clone(), test_struct_2).await;
    }

    async fn test_write_propagation(
        store_1: Arc<DeviceStorage>,
        store_2: Arc<DeviceStorage>,
        data: TestStruct,
    ) {
        assert!(store_1.write(&data).await.is_ok());

        // Ensure it is read in from second store.
        let retrieved_struct = store_2.get().await;
        assert_eq!(data, retrieved_struct);
    }

    // This mod includes structs to only be used by
    // test_device_compatible_migration tests.
    mod test_device_compatible_migration {
        use super::super::DeviceStorageCompatible;
        use serde::{Deserialize, Serialize};

        pub(crate) const DEFAULT_V1_VALUE: i32 = 1;
        pub(crate) const DEFAULT_CURRENT_VALUE: i32 = 2;
        pub(crate) const DEFAULT_CURRENT_VALUE_2: i32 = 3;

        #[derive(PartialEq, Clone, Serialize, Deserialize, Debug)]
        pub(crate) struct V1 {
            pub value: i32,
        }

        impl DeviceStorageCompatible for V1 {
            const KEY: &'static str = "testkey";

            fn default_value() -> Self {
                Self { value: DEFAULT_V1_VALUE }
            }
        }

        #[derive(PartialEq, Clone, Serialize, Deserialize, Debug)]
        pub(crate) struct Current {
            pub value: i32,
            pub value_2: i32,
        }

        impl From<V1> for Current {
            fn from(v1: V1) -> Self {
                Current { value: v1.value, value_2: DEFAULT_CURRENT_VALUE_2 }
            }
        }

        impl DeviceStorageCompatible for Current {
            const KEY: &'static str = "testkey2";

            fn default_value() -> Self {
                Self { value: DEFAULT_CURRENT_VALUE, value_2: DEFAULT_CURRENT_VALUE_2 }
            }

            fn deserialize_from(value: &str) -> Self {
                Self::extract(&value).unwrap_or_else(|_| {
                    V1::extract(&value).map_or(Self::default_value(), Self::from)
                })
            }
        }
    }

    #[test]
    fn test_device_compatible_custom_migration() {
        // Create an initial struct based on the first version.
        let initial = test_device_compatible_migration::V1::default_value();
        // Serialize.
        let initial_serialized = initial.serialize_to();

        // Deserialize using the second version.
        let current =
            test_device_compatible_migration::Current::deserialize_from(&initial_serialized);
        // Assert values carried over from first version and defaults are used for rest.
        assert_eq!(current.value, test_device_compatible_migration::DEFAULT_V1_VALUE);
        assert_eq!(current.value_2, test_device_compatible_migration::DEFAULT_CURRENT_VALUE_2);
    }
}
