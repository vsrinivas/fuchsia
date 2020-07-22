// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_stash::{StoreAccessorProxy, StoreProxy, Value};
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use serde::de::DeserializeOwned;
use serde::Serialize;
use std::sync::Arc;

const SETTINGS_PREFIX: &str = "settings";

/// Stores device level settings in persistent storage.
/// User level settings should not use this.
pub struct DeviceStorage<T: DeviceStorageCompatible> {
    caching_enabled: bool,
    current_data: Option<T>,
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
pub trait DeviceStorageCompatible: Serialize + DeserializeOwned + Clone + PartialEq {
    fn default_value() -> Self;
    const KEY: &'static str;
}

impl<T: DeviceStorageCompatible> DeviceStorage<T> {
    pub fn new(stash_proxy: StoreAccessorProxy, current_data: Option<T>) -> Self {
        return DeviceStorage {
            caching_enabled: true,
            current_data: current_data,
            stash_proxy: stash_proxy,
        };
    }

    #[cfg(test)]
    fn set_caching_enabled(&mut self, enabled: bool) {
        self.caching_enabled = enabled;
    }

    pub async fn write(&mut self, new_value: &T, flush: bool) -> Result<(), Error> {
        if self.current_data.as_ref() != Some(new_value) {
            let mut serialized = Value::Stringval(serde_json::to_string(new_value).unwrap());
            self.stash_proxy.set_value(&prefixed(T::KEY), &mut serialized)?;
            if flush {
                if self.stash_proxy.flush().await.is_err() {
                    return Err(format_err!("flush error"));
                }
            } else {
                self.stash_proxy.commit()?;
            }
            self.current_data = Some(new_value.clone());
        }
        Ok(())
    }

    /// Gets the latest value cached locally, or loads the value from storage.
    /// Doesn't support multiple concurrent callers of the same struct.
    pub async fn get(&mut self) -> T {
        if None == self.current_data || !self.caching_enabled {
            if let Some(stash_value) = self.stash_proxy.get_value(&prefixed(T::KEY)).await.unwrap()
            {
                if let Value::Stringval(string_value) = &*stash_value {
                    let data = match serde_json::from_str(&string_value) {
                        Ok(val) => val,
                        Err(e) => {
                            fx_log_err!("Failed to serialize value from stash, returning default");
                            fx_log_err!("{}", e);
                            T::default_value()
                        }
                    };
                    self.current_data = Some(data);
                } else {
                    panic!("Unexpected type for key found in stash");
                }
            } else {
                self.current_data = Some(T::default_value());
            }
        }
        if let Some(curent_value) = &self.current_data {
            curent_value.clone()
        } else {
            panic!("Should never have no value");
        }
    }
}

pub trait DeviceStorageFactory {
    fn get_store<T: DeviceStorageCompatible + 'static>(
        &mut self,
        context_id: u64,
    ) -> Arc<Mutex<DeviceStorage<T>>>;
}

/// Factory that vends out storage for individual structs.
pub struct StashDeviceStorageFactory {
    store: StoreProxy,
}

impl StashDeviceStorageFactory {
    pub fn create(identity: &str, store: StoreProxy) -> StashDeviceStorageFactory {
        let result = store.identify(identity);
        match result {
            Ok(_) => {}
            Err(_) => {
                panic!("Was not able to identify with stash");
            }
        }

        StashDeviceStorageFactory { store: store }
    }
}

impl DeviceStorageFactory for StashDeviceStorageFactory {
    /// Currently, this doesn't support more than one instance of the same struct.
    fn get_store<T: DeviceStorageCompatible + 'static>(
        &mut self,
        _context_id: u64,
    ) -> Arc<Mutex<DeviceStorage<T>>> {
        let (accessor_proxy, server_end) = create_proxy().unwrap();
        self.store.create_accessor(false, server_end).unwrap();

        Arc::new(Mutex::new(DeviceStorage::new(accessor_proxy, None)))
    }
}

fn prefixed(input_string: &str) -> String {
    format!("{}_{}", SETTINGS_PREFIX, input_string)
}

#[cfg(test)]
pub mod testing {
    use super::*;
    use fidl_fuchsia_stash::{StoreAccessorMarker, StoreAccessorRequest};
    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use futures::prelude::*;
    use std::any::TypeId;
    use std::collections::{HashMap, HashSet};

    #[derive(PartialEq)]
    pub enum StashAction {
        Get,
        Flush,
        Set,
        Commit,
    }

    pub struct StashStats {
        actions: Vec<StashAction>,
    }

    impl StashStats {
        pub fn new() -> Self {
            StashStats { actions: Vec::new() }
        }

        pub fn record(&mut self, action: StashAction) {
            self.actions.push(action);
        }

        pub fn get_record_count(&self, action: StashAction) -> usize {
            return self.actions.iter().filter(|&target| *target == action).count();
        }
    }

    /// Storage that does not write to disk, for testing.
    /// Only supports a single key/value pair
    pub struct InMemoryStorageFactory {
        proxies: HashMap<TypeId, (StoreAccessorProxy, Arc<Mutex<StashStats>>)>,
        prod_accessed_types: HashSet<u64>,
    }

    #[derive(PartialEq)]
    pub enum StorageAccessContext {
        Production,
        Test,
    }

    impl InMemoryStorageFactory {
        pub fn create() -> Arc<Mutex<InMemoryStorageFactory>> {
            Arc::new(Mutex::new(InMemoryStorageFactory {
                proxies: HashMap::new(),
                prod_accessed_types: HashSet::new(),
            }))
        }

        pub fn get_device_storage<T: DeviceStorageCompatible + 'static>(
            &mut self,
            access_context: StorageAccessContext,
            context_id: u64,
        ) -> Arc<Mutex<DeviceStorage<T>>> {
            let id = TypeId::of::<T>();
            if !self.proxies.contains_key(&id) {
                self.proxies.insert(id, spawn_stash_proxy());
            }

            if access_context == StorageAccessContext::Production {
                if self.prod_accessed_types.contains(&context_id) {
                    panic!("Should only access DeviceStorage once per type");
                }

                self.prod_accessed_types.insert(context_id);
            }

            if let Some((proxy, _)) = self.proxies.get(&id) {
                let mut storage = DeviceStorage::new(proxy.clone(), None);
                storage.set_caching_enabled(false);

                Arc::new(Mutex::new(storage))
            } else {
                panic!("proxy should be present");
            }
        }
    }

    impl DeviceStorageFactory for InMemoryStorageFactory {
        fn get_store<T: DeviceStorageCompatible + 'static>(
            &mut self,
            context_id: u64,
        ) -> Arc<Mutex<DeviceStorage<T>>> {
            self.get_device_storage(StorageAccessContext::Production, context_id)
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
                    StoreAccessorRequest::Commit { control_handle: _ } => {
                        stats_clone.lock().await.record(StashAction::Commit);
                    }
                    StoreAccessorRequest::Flush { responder } => {
                        stats_clone.lock().await.record(StashAction::Flush);
                        responder.send(&mut Ok(())).ok();
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
    use super::*;
    use fidl_fuchsia_stash::{StoreAccessorMarker, StoreAccessorRequest};
    use fuchsia_async as fasync;
    use futures::lock::Mutex;
    use futures::prelude::*;
    use serde::{Deserialize, Serialize};
    use testing::*;

    const CONTEXT_ID: u64 = 0;
    const VALUE0: i32 = 3;
    const VALUE1: i32 = 33;
    const VALUE2: i32 = 128;

    #[derive(PartialEq, Clone, Serialize, Deserialize, Debug)]
    struct TestStruct {
        value: i32,
    }

    impl DeviceStorageCompatible for TestStruct {
        const KEY: &'static str = "testkey";

        fn default_value() -> Self {
            TestStruct { value: VALUE0 }
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_get() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::Task::spawn(async move {
            let value_to_get = TestStruct { value: VALUE1 };

            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key, responder } => {
                        assert_eq!(key, "settings_testkey");
                        let mut response =
                            Value::Stringval(serde_json::to_string(&value_to_get).unwrap());

                        responder.send(Some(&mut response)).unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);

        let result = storage.get().await;

        assert_eq!(result.value, VALUE1);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_get_default() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::Task::spawn(async move {
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

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);

        let result = storage.get().await;

        assert_eq!(result.value, VALUE0);
    }

    // For an invalid stash value, the get() method should return the default value.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_invalid_stash() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        fasync::Task::spawn(async move {
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

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);

        let result = storage.get().await;

        assert_eq!(result.value, VALUE0);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);

        storage.write(&TestStruct { value: VALUE2 }, false).await.expect("writing shouldn't fail");

        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::SetValue { key, val, control_handle: _ }) => {
                assert_eq!(key, "settings_testkey");
                if let Value::Stringval(string_value) = val {
                    let input_value: TestStruct = serde_json::from_str(&string_value).unwrap();
                    assert_eq!(input_value.value, VALUE2);
                } else {
                    panic!("Unexpected type for key found in stash");
                }
            }
            request => panic!("Unexpected request: {:?}", request),
        }

        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::Commit { .. }) => {} // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_with_flush() {
        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);
        fasync::Task::spawn(async move {
            match stash_stream.next().await.unwrap() {
                Ok(StoreAccessorRequest::SetValue { key, val, control_handle: _ }) => {
                    assert_eq!(key, "settings_testkey");
                    if let Value::Stringval(string_value) = val {
                        let input_value: TestStruct = serde_json::from_str(&string_value).unwrap();
                        assert_eq!(input_value.value, VALUE2);
                    } else {
                        panic!("Unexpected type for key found in stash");
                    }
                }
                request => panic!("Unexpected request: {:?}", request),
            }

            match stash_stream.next().await.unwrap() {
                Ok(StoreAccessorRequest::Flush { responder }) => {
                    responder.send(&mut Ok(())).ok();
                } // expected
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .detach();
        storage.write(&TestStruct { value: VALUE2 }, true).await.expect("writing shouldn't fail");
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_in_memory_storage() {
        let factory = InMemoryStorageFactory::create();

        let store_1 = factory
            .lock()
            .await
            .get_device_storage::<TestStruct>(StorageAccessContext::Test, CONTEXT_ID);
        let store_2 = factory
            .lock()
            .await
            .get_device_storage::<TestStruct>(StorageAccessContext::Test, CONTEXT_ID);

        // Write initial data through first store.
        let test_struct = TestStruct { value: VALUE0 };

        // Ensure writing from store1 ends up in store2
        test_write_propagation(store_1.clone(), store_2.clone(), test_struct).await;

        let test_struct_2 = TestStruct { value: VALUE1 };
        // Ensure writing from store2 ends up in store1
        test_write_propagation(store_2.clone(), store_1.clone(), test_struct_2).await;
    }

    async fn test_write_propagation(
        store_1: Arc<Mutex<DeviceStorage<TestStruct>>>,
        store_2: Arc<Mutex<DeviceStorage<TestStruct>>>,
        data: TestStruct,
    ) {
        {
            let mut store_1_lock = store_1.lock().await;
            assert!(store_1_lock.write(&data, false).await.is_ok());
        }

        // Ensure it is read in from second store.
        {
            let mut store_2_lock = store_2.lock().await;
            let retrieved_struct = store_2_lock.get().await;

            assert_eq!(data, retrieved_struct);
        }
    }
}
