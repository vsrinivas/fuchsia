// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
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
use std::ops::{Add, Sub};
use std::sync::Arc;
use std::time::{Duration, Instant};

const SETTINGS_PREFIX: &str = "settings";

/// Minimum amount of time between Commit calls to Stash, in milliseconds. The Commit call triggers
/// file I/O which is slow. If we call commit too often, we can overwhelm Stash, which eventually
/// causes the kernel to crash our service due to filling up the channel.
const MIN_COMMIT_INTERVAL_MS: u64 = 500;

/// `CommitParams` specifies the behavior for a commit request to the task loop that handles
/// committing to Stash.
#[derive(Copy, Clone)]
struct CommitParams {
    /// If true, flushes to disk instead of just committing, which waits for the file to be written
    /// to disk.
    flush: bool,
}

/// Stores device level settings in persistent storage.
/// User level settings should not use this.
pub struct DeviceStorage<T: DeviceStorageCompatible> {
    /// If true, reads will be returned from the data in memory rather than reading from storage.
    caching_enabled: bool,

    /// If true, writes to the underlying storage will only occur at most every
    /// MIN_WRITE_INTERVAL_MS.
    debounce_writes: bool,
    current_data: Option<T>,

    /// Sender to communicate with task loop that handles commits.
    commit_sender: UnboundedSender<CommitParams>,
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

    fn deserialize_from(value: &String) -> Self {
        Self::extract(&value).unwrap_or_else(|error| {
            fx_log_err!("error occurred:{:?}", error);
            Self::default_value()
        })
    }

    fn extract(value: &String) -> Result<Self, Error> {
        serde_json::from_str(&value).map_err(|_| format_err!("could not deserialize"))
    }

    fn serialize_to(&self) -> String {
        serde_json::to_string(self).expect("value should serialize")
    }

    const KEY: &'static str;
}

impl<T: DeviceStorageCompatible> DeviceStorage<T> {
    pub fn new(stash_proxy: StoreAccessorProxy, current_data: Option<T>) -> Self {
        let (commit_sender, commit_receiver) = futures::channel::mpsc::unbounded::<CommitParams>();

        let storage = DeviceStorage {
            caching_enabled: true,
            debounce_writes: true,
            current_data,
            commit_sender,
            stash_proxy: stash_proxy.clone(),
        };

        Task::spawn(async move {
            const MIN_COMMIT_DURATION: Duration = Duration::from_millis(MIN_COMMIT_INTERVAL_MS);
            let mut pending_commit: Option<CommitParams> = None;

            // The time of the last commit. Initialized to MIN_COMMIT_INTERVAL_MS before the current
            // time so that the first commit always goes through, no matter the timing.
            let mut last_commit: Instant = Instant::now().sub(MIN_COMMIT_DURATION);

            // Timer for commit cooldown. OptionFuture allows us to wait on the future even if
            // it's None.
            let mut next_commit_timer: OptionFuture<Timer> = None.into();
            let mut next_commit_timer_fuse = next_commit_timer.fuse();

            let commit_fuse = commit_receiver.fuse();

            futures::pin_mut!(commit_fuse);
            loop {
                futures::select! {
                    commit_params = commit_fuse.select_next_some() => {
                        // Received a request to do a commit.
                        let now = Instant::now();
                        let next_commit_time = if now - last_commit > MIN_COMMIT_DURATION {
                            // Last commit happened more than MIN_COMMIT_INTERVAL_MS ago, commit
                            // immediately in next iteration of loop.
                            now
                        } else {
                            // Last commit was less than MIN_COMMIT_INTERVAL_MS ago, schedule it
                            // accordingly. It's okay if the time is in the past, Timer will still
                            // trigger on the next loop iteration.
                            last_commit.add(MIN_COMMIT_DURATION)
                        };
                        pending_commit = Some(commit_params);
                        next_commit_timer = Some(Timer::new(next_commit_time.into_time())).into();
                        next_commit_timer_fuse = next_commit_timer.fuse();
                    }

                    _ = next_commit_timer_fuse => {
                        // Timer triggered, check for pending commits.
                        if let Some(params) = pending_commit {
                            DeviceStorage::<T>::stash_commit(&stash_proxy, params.flush).await;
                            last_commit = Instant::now();
                            pending_commit = None;
                        }
                    }

                    complete => break,
                }
            }
        })
        .detach();

        return storage;
    }

    #[cfg(test)]
    fn set_caching_enabled(&mut self, enabled: bool) {
        self.caching_enabled = enabled;
    }

    #[cfg(test)]
    fn set_debounce_writes(&mut self, debounce: bool) {
        self.debounce_writes = debounce;
    }

    /// Triggers a commit on the given stash proxy, or a flush if `flush` is true.
    async fn stash_commit(stash_proxy: &StoreAccessorProxy, flush: bool) {
        if flush {
            if stash_proxy.flush().await.is_err() {
                fx_log_err!("flush error");
            }
        } else {
            stash_proxy.commit().unwrap_or_else(|_| fx_log_err!("commit failed"));
        }
    }

    pub async fn write(&mut self, new_value: &T, flush: bool) -> Result<(), Error> {
        if self.current_data.as_ref() != Some(new_value) {
            let mut serialized = Value::Stringval(new_value.serialize_to());
            self.stash_proxy.set_value(&prefixed(T::KEY), &mut serialized)?;
            if !self.debounce_writes {
                // Not debouncing writes for testing, just commit immediately.
                DeviceStorage::<T>::stash_commit(&self.stash_proxy, flush).await;
            } else {
                self.commit_sender.unbounded_send(CommitParams { flush }).ok();
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
                    self.current_data = Some(T::deserialize_from(&string_value));
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
                storage.set_debounce_writes(false);

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
    use fidl_fuchsia_stash::{
        StoreAccessorMarker, StoreAccessorRequest, StoreAccessorRequestStream,
    };
    use fuchsia_async as fasync;
    use fuchsia_async::{Executor, Time};
    use futures::lock::Mutex;
    use futures::prelude::*;
    use serde::{Deserialize, Serialize};
    use std::convert::TryInto;
    use std::marker::Unpin;
    use std::task::Poll;
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

    /// Advances `future` until `executor` finishes. Panics if the end result was a stall.
    fn advance_executor<F>(executor: &mut Executor, mut future: &mut F)
    where
        F: Future + Unpin,
    {
        loop {
            executor.wake_main_future();
            match executor.run_one_step(&mut future) {
                Some(Poll::Ready(_)) => return,
                None => panic!("Executor stalled!"),
                Some(Poll::Pending) => {}
            }
        }
    }

    /// Verifies that a SetValue call was sent to stash with the given value.
    async fn verify_stash_set(stash_stream: &mut StoreAccessorRequestStream, expected_value: i32) {
        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::SetValue { key, val, control_handle: _ }) => {
                assert_eq!(key, "settings_testkey");
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

    /// Verifies that a Commit call was sent to stash.
    async fn verify_stash_commit(stash_stream: &mut StoreAccessorRequestStream) {
        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::Commit { .. }) => {} // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    /// Verifies that a Flush call was sent to stash.
    async fn verify_stash_flush(stash_stream: &mut StoreAccessorRequestStream) {
        match stash_stream.next().await.unwrap() {
            Ok(StoreAccessorRequest::Flush { responder }) => {
                responder.send(&mut Ok(())).ok();
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

            while let Some(req) = stash_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    StoreAccessorRequest::GetValue { key, responder } => {
                        assert_eq!(key, "settings_testkey");
                        let mut response = Value::Stringval(value_to_get.serialize_to());

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

    // Test that an initial write to DeviceStorage causes a SetValue and Commit to Stash
    // without any wait.
    #[test]
    fn test_first_write_commits_immediately() {
        let written_value = VALUE2;
        let mut executor = Executor::new_with_fake_time().expect("Failed to create executor");

        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);

        // Write to device storage.
        let value_to_write = TestStruct { value: written_value };
        let write_future = storage.write(&value_to_write, false);
        futures::pin_mut!(write_future);

        // Write request finishes immediately.
        matches!(executor.run_until_stalled(&mut write_future), Poll::Ready(Result::Ok(_)));

        // Set request is received immediately on write.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, written_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // Start listening for the commit request.
        let commit_future = verify_stash_commit(&mut stash_stream);
        futures::pin_mut!(commit_future);

        // Commit is received without a wait. Due to the way time works with executors, if there was
        // a delay, the test would stall since time never advances.
        advance_executor(&mut executor, &mut commit_future);
    }

    // Test that multiple writes to DeviceStorage will cause a SetValue each time, but will only
    // Commit to Stash at an interval.
    #[test]
    fn test_multiple_write_debounce() {
        // Custom executor for this test so that we can advance the clock arbitrarily and verify the
        // state of the executor at any given point.
        let mut executor = Executor::new_with_fake_time().expect("Failed to create executor");
        executor.set_fake_time(Time::from_nanos(0));

        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);

        let first_value = VALUE1;
        let second_value = VALUE2;

        // First write finishes immediately.
        {
            let value_to_write = TestStruct { value: first_value };
            let write_future = storage.write(&value_to_write, false);
            futures::pin_mut!(write_future);
            matches!(executor.run_until_stalled(&mut write_future), Poll::Ready(Result::Ok(_)));
        }

        // First set request is received immediately on write.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, first_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // First commit request is received.
        {
            let commit_future = verify_stash_commit(&mut stash_stream);
            futures::pin_mut!(commit_future);
            advance_executor(&mut executor, &mut commit_future);
        }

        // Now we repeat the process with a second write request, which will need to advance the
        // fake time due to the timer.

        // Second write finishes immediately.
        {
            let value_to_write = TestStruct { value: second_value };
            let write_future = storage.write(&value_to_write, false);
            futures::pin_mut!(write_future);
            matches!(executor.run_until_stalled(&mut write_future), Poll::Ready(Result::Ok(_)));
        }

        // Second set request finishes immediately on write.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, second_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // Start waiting for commit request.
        let commit_future = verify_stash_commit(&mut stash_stream);
        futures::pin_mut!(commit_future);

        // Executor stalls due to waiting on timer to finish.
        matches!(executor.run_until_stalled(&mut commit_future), Poll::Pending);

        // Advance time to 1ms before the commit triggers.
        executor.set_fake_time(Time::from_nanos(
            ((MIN_COMMIT_INTERVAL_MS - 1) * 10_u64.pow(6)).try_into().unwrap(),
        ));

        // Executor is still waiting on the time to finish.
        matches!(executor.run_until_stalled(&mut commit_future), Poll::Pending);

        // Advance time so that the commit will trigger.
        executor.set_fake_time(Time::from_nanos(
            (MIN_COMMIT_INTERVAL_MS * 10_u64.pow(6)).try_into().unwrap(),
        ));

        // Stash receives a commit request after one timer cycle and the future terminates.
        advance_executor(&mut executor, &mut commit_future);
    }

    // Tests that a write requesting a flush is sent to Stash as a Flush request.
    #[test]
    fn test_write_with_flush() {
        let written_value = VALUE2;
        let mut executor = Executor::new_with_fake_time().expect("Failed to create executor");

        let (stash_proxy, mut stash_stream) =
            fidl::endpoints::create_proxy_and_stream::<StoreAccessorMarker>().unwrap();

        let mut storage: DeviceStorage<TestStruct> = DeviceStorage::new(stash_proxy, None);

        // Write to device storage with flush set to true.
        let value_to_write = TestStruct { value: written_value };
        let write_future = storage.write(&value_to_write, true);
        futures::pin_mut!(write_future);
        matches!(executor.run_until_stalled(&mut write_future), Poll::Ready(Result::Ok(_)));

        // Stash receives a set request.
        {
            let set_value_future = verify_stash_set(&mut stash_stream, written_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // Start listening for the flush request.
        let flush_future = verify_stash_flush(&mut stash_stream);
        futures::pin_mut!(flush_future);

        // Commit is received without a wait. Due to the way time works with executors, if there was
        // a delay, the test would stall since time never advances.
        advance_executor(&mut executor, &mut flush_future);
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

    // This mod includes structs to only be used by
    // test_device_compatible_migration tests.
    mod test_device_compatible_migration {
        use crate::handler::device_storage::DeviceStorageCompatible;
        use serde::{Deserialize, Serialize};

        pub const DEFAULT_V1_VALUE: i32 = 1;
        pub const DEFAULT_CURRENT_VALUE: i32 = 2;
        pub const DEFAULT_CURRENT_VALUE_2: i32 = 3;

        #[derive(PartialEq, Clone, Serialize, Deserialize, Debug)]
        pub struct V1 {
            pub value: i32,
        }

        impl DeviceStorageCompatible for V1 {
            const KEY: &'static str = "testkey";

            fn default_value() -> Self {
                Self { value: DEFAULT_V1_VALUE }
            }
        }

        #[derive(PartialEq, Clone, Serialize, Deserialize, Debug)]
        pub struct Current {
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

            fn deserialize_from(value: &String) -> Self {
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
