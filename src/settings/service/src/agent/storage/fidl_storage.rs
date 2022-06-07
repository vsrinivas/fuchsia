// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::storage::UpdateState;
use anyhow::{format_err, Context, Error};
use fidl::encoding::{decode_persistent, encode_persistent, Persistable};
use fidl::Status;
use fidl_fuchsia_io::FileProxy;
use fuchsia_async::{Task, Timer, WakeupTime};
use fuchsia_fs::file::ReadError;
use fuchsia_fs::node::OpenError;
use fuchsia_zircon as zx;
use futures::channel::mpsc::UnboundedSender;
use futures::future::OptionFuture;
use futures::lock::Mutex;
use futures::{FutureExt, StreamExt};
use std::any::Any;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};

/// Minimum amount of time between flushing to disk, in milliseconds. The flush call triggers
/// file I/O which is slow.
// TODO(fxbug.dev/95380) Investigate if this value should be updated for fidl-based storage.
const MIN_FLUSH_INTERVAL_MS: u64 = 500;

pub trait FidlStorageConvertible {
    type Storable: Persistable + Any;
    const KEY: &'static str;

    fn default_value() -> Self;
    fn to_storable(self) -> Self::Storable;
    fn from_storable(storable: Self::Storable) -> Self;
}

/// Stores device level settings in persistent storage.
/// User level settings should not use this.
pub struct FidlStorage {
    /// Map of [`FidlStorageConvertible`] keys to their typed storage.
    typed_storage_map: HashMap<&'static str, TypedStorage>,

    /// If true, reads will be returned from the data in memory rather than reading from storage.
    caching_enabled: bool,

    /// If true, writes to the underlying storage will only occur at most every
    /// [MIN_WRITE_INTERVAL_MS].
    debounce_writes: bool,
}

/// A wrapper for managing all communication and caching for one particular type of data being
/// stored. The actual types are erased.
struct TypedStorage {
    /// Sender to communicate with task loop that handles flushes.
    flush_sender: UnboundedSender<()>,

    /// Cached storage managed through interior mutability.
    cached_storage: Arc<Mutex<CachedStorage>>,
}

/// `CachedStorage` abstracts over a cached value that's read from and written
/// to some backing store.
struct CachedStorage {
    /// Cache for the most recently read or written value. The value is stored as the encoded bytes
    /// of the persistent fidl.
    current_data: Option<Vec<u8>>,

    /// File connection for this particular storage entry.
    file_proxy: FileProxy,
}

impl CachedStorage {
    /// Triggers a sync on the file proxy.
    async fn sync(&mut self) {
        let result = self.file_proxy.sync().await;
        if !matches!(result, Ok(Ok(()))) {
            // TODO(fxbug.dev/91404) log writing error
        }
    }
}

impl FidlStorage {
    /// Construct a fidl storage from:
    /// * The iterable item, which will produce the keys for storage
    /// * A generator function that will produce a file proxy for each key
    pub(crate) fn with_file_proxy<I, G>(iter: I, file_generator: G) -> Self
    where
        I: IntoIterator<Item = &'static str>,
        G: Fn(&'static str) -> FileProxy,
    {
        let typed_storage_map = iter
            .into_iter()
            .map(|key| {
                // Generate a separate file proxy for each key.
                let (flush_sender, flush_receiver) = futures::channel::mpsc::unbounded::<()>();
                let file_proxy = file_generator(key);

                let cached_storage =
                    Arc::new(Mutex::new(CachedStorage { current_data: None, file_proxy }));
                let storage =
                    TypedStorage { flush_sender, cached_storage: Arc::clone(&cached_storage) };

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
                                // Timer triggered, check for pending syncs.
                                if has_pending_flush {
                                    cached_storage.lock().await.sync().await;
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
            })
            .collect();
        FidlStorage { caching_enabled: true, debounce_writes: true, typed_storage_map }
    }

    #[cfg(test)]
    fn set_caching_enabled(&mut self, enabled: bool) {
        self.caching_enabled = enabled;
    }

    #[cfg(test)]
    fn set_debounce_writes(&mut self, debounce: bool) {
        self.debounce_writes = debounce;
    }

    async fn inner_write(
        &self,
        key: &'static str,
        new_value: Vec<u8>,
    ) -> Result<UpdateState, Error> {
        let typed_storage = self
            .typed_storage_map
            .get(key)
            .ok_or_else(|| format_err!("Invalid data keyed by {}", key))?;
        let mut cached_storage = typed_storage.cached_storage.lock().await;
        let bytes;
        let cached_value = {
            let maybe_init = cached_storage.current_data.as_ref();
            match maybe_init {
                Some(cached_value) => cached_value,
                None => {
                    bytes = fuchsia_fs::file::read(&cached_storage.file_proxy)
                        .await
                        .unwrap_or_else(|e| match e {
                            ReadError::Open(OpenError::OpenError(e))
                                if e == zx::Status::NOT_FOUND =>
                            {
                                panic!("fidl file missing for {:?}", key)
                            }
                            _ => {
                                panic!(
                                    "failed to get value from fidl storage for {:?}: {:?}",
                                    key, e
                                )
                            }
                        });
                    &bytes
                }
            }
        };

        Ok(if *cached_value != new_value {
            fuchsia_fs::file::write(&cached_storage.file_proxy, &new_value).await?;
            if !self.debounce_writes {
                // Not debouncing writes for testing, just flush immediately.
                cached_storage.sync().await;
            } else {
                typed_storage.flush_sender.unbounded_send(()).with_context(|| {
                    format!("flush_sender failed to send flush message, associated key is {}", key)
                })?;
            }
            cached_storage.current_data = Some(new_value);
            UpdateState::Updated
        } else {
            UpdateState::Unchanged
        })
    }

    /// Write `new_value` to storage. The write will be persisted to disk at a set interval.
    pub(crate) async fn write<T>(&self, new_value: T) -> Result<UpdateState, Error>
    where
        T: FidlStorageConvertible,
    {
        let new_value = encode_persistent(&mut new_value.to_storable())?;
        self.inner_write(T::KEY, new_value).await
    }

    /// Gets the latest value cached locally, or loads the value from storage.
    /// Doesn't support multiple concurrent callers of the same struct.
    pub(crate) async fn get<T>(&self) -> T
    where
        T: FidlStorageConvertible,
    {
        let typed_storage = self
            .typed_storage_map
            .get(T::KEY)
            // TODO(fxbug.dev/67371) Replace this with an error result.
            .unwrap_or_else(|| panic!("Invalid data keyed by {}", T::KEY));
        let mut cached_storage = typed_storage.cached_storage.lock().await;
        if cached_storage.current_data.is_none() || !self.caching_enabled {
            let data = match fuchsia_fs::file::read(&cached_storage.file_proxy).await {
                Ok(data) => Some(data),
                Err(ReadError::ReadError(Status::NOT_FOUND)) => None,
                Err(e) => panic!("failed to get fidl data from disk for {:?}: {:?}", T::KEY, e),
            };

            cached_storage.current_data = data;
        }

        cached_storage
            .current_data
            .as_ref()
            .map(|data| {
                <T as FidlStorageConvertible>::from_storable(
                    decode_persistent(data)
                        .expect("Should not be able to save mismatching types in file"),
                )
            })
            .unwrap_or_else(|| T::default_value())
    }
}

#[cfg(test)]
pub(crate) mod testing {
    use super::*;
    use fidl_fuchsia_io::{FileMarker, FileRequest, SeekOrigin};
    use fuchsia_async as fasync;
    use futures::TryStreamExt;

    pub(crate) fn spawn_file_proxy() -> FileProxy {
        let (file_proxy, mut file_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<FileMarker>().unwrap();
        fasync::Task::spawn(async move {
            let mut stored_value: Vec<u8> = vec![];
            let mut ptr = 0;

            while let Some(req) = file_request_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    FileRequest::Read { count, responder } => {
                        let response: Vec<_> = stored_value
                            .iter()
                            .skip(ptr as usize)
                            .take(count as usize)
                            .copied()
                            .collect();
                        let response_len = response.len();
                        let _ = responder.send(&mut Ok(response));
                        ptr = response_len;
                    }
                    FileRequest::Write { data, responder } => {
                        let len = data.len() as u64;
                        stored_value = data;
                        let _ = responder.send(&mut Ok(len));
                    }
                    FileRequest::Sync { responder } => {
                        let _ = responder.send(&mut Ok(()));
                    }
                    FileRequest::Seek { origin, offset, responder } => {
                        ptr = match origin {
                            SeekOrigin::Start => {
                                if offset < 0 {
                                    responder.send(&mut Err(-1)).unwrap();
                                    continue;
                                } else {
                                    offset as usize
                                }
                            }
                            SeekOrigin::Current => (ptr as i64 + offset) as usize,
                            SeekOrigin::End => {
                                if offset > 0 {
                                    responder.send(&mut Err(-1)).unwrap();
                                    continue;
                                } else {
                                    (stored_value.len() as i64 + offset) as usize
                                }
                            }
                        };
                        responder.send(&mut Ok(ptr as u64)).unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();
        file_proxy
    }
}

#[cfg(test)]
mod tests {
    use std::convert::TryInto;
    use std::marker::Unpin;
    use std::task::Poll;

    use assert_matches::assert_matches;
    use fidl::{fidl_empty_struct, fidl_struct};
    use fidl_fuchsia_io::{FileMarker, FileRequest, FileRequestStream};
    use fuchsia_async as fasync;
    use fuchsia_async::{TestExecutor, Time};
    use futures::prelude::*;

    use super::*;

    const VALUE0: i32 = 3;
    const VALUE1: i32 = 33;
    const VALUE2: i32 = 128;

    #[derive(PartialEq, Clone, Copy, Debug, Default)]
    struct TestStruct {
        value: i32,
    }

    impl FidlStorageConvertible for TestStruct {
        type Storable = Self;
        const KEY: &'static str = "testkey";

        fn default_value() -> Self {
            TestStruct { value: VALUE0 }
        }

        fn to_storable(self) -> Self::Storable {
            self
        }

        fn from_storable(storable: Self::Storable) -> Self {
            storable
        }
    }

    impl Persistable for TestStruct {}
    fidl_struct! {
        name: TestStruct,
        members: [
            value {
                ty: i32,
                offset_v1: 0,
                offset_v2: 0,
            },
        ],
        padding_v1: [],
        padding_v2: [],
        size_v1: 8,
        size_v2: 8,
        align_v1: 8,
        align_v2: 8,
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

    /// Verifies that a write call was sent to the fs with the given value.
    async fn verify_io_write(file_request_stream: &mut FileRequestStream, expected_value: i32) {
        match file_request_stream.next().await.unwrap() {
            Ok(FileRequest::Write { data, responder }) => {
                let input_value: TestStruct =
                    decode_persistent(&data).expect("Should be able to decode test struct data");
                assert_eq!(input_value.value, expected_value);
                let _ = responder.send(&mut Ok(data.len() as u64));
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    /// Verifies that a read call was sent to the io with the given value.
    async fn validate_io_get_and_respond(
        file_request_stream: &mut FileRequestStream,
        response: Vec<u8>,
    ) {
        match file_request_stream.next().await.unwrap() {
            Ok(FileRequest::Read { responder, .. }) => {
                responder.send(&mut Ok(response)).expect("unable to send response");
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    /// Verifies that a sync call was sent to the fs.
    async fn verify_io_sync(file_request_stream: &mut FileRequestStream) {
        match file_request_stream.next().await.unwrap() {
            Ok(FileRequest::Sync { responder }) => {
                let _ = responder.send(&mut Ok(()));
            } // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    /// Verifies that a sync call was sent to the fs, and send back a failure.
    async fn fail_io_sync(file_request_stream: &mut FileRequestStream) {
        match file_request_stream.next().await.unwrap() {
            Ok(FileRequest::Sync { responder }) => {
                let _ = responder.send(&mut Err(-1));
            } // expected
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_get() {
        let file_proxy = testing::spawn_file_proxy();
        let value_to_get = TestStruct { value: VALUE1 };
        let _ = file_proxy
            .write(&encode_persistent(&mut value_to_get.to_storable()).unwrap())
            .await
            .unwrap()
            .unwrap();
        let storage =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], move |_| Clone::clone(&file_proxy));
        let result = storage.get::<TestStruct>().await;

        assert_eq!(result.value, VALUE1);
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_get_default() {
        let (file_proxy, mut file_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        fasync::Task::spawn(async move {
            while let Some(req) = file_request_stream.try_next().await.unwrap() {
                if let FileRequest::Read { responder, .. } = req {
                    responder.send(&mut Err(zx::sys::ZX_ERR_NOT_FOUND)).unwrap();
                }
            }
        })
        .detach();

        let storage =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], move |_| Clone::clone(&file_proxy));
        let result = storage.get::<TestStruct>().await;

        assert_eq!(result.value, VALUE0);
    }

    // Test that an initial write to FidlStorage causes a write and sync to the fs
    // without any wait.
    #[test]
    fn test_first_write_syncs_immediately() {
        let written_value = VALUE2;
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");

        let (file_proxy, mut file_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let storage =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], move |_| Clone::clone(&file_proxy));

        // Write to device storage.
        let value_to_write = TestStruct { value: written_value };
        let write_future = storage.write(value_to_write);
        futures::pin_mut!(write_future);

        // Initial cache check is done if no read was ever performed.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

        {
            let respond_future = validate_io_get_and_respond(
                &mut file_request_stream,
                encode_persistent(&mut TestStruct { value: 0 }).unwrap(),
            );
            futures::pin_mut!(respond_future);
            advance_executor(&mut executor, &mut respond_future);
        }

        // There's a hold since multiple reads are done until no more data is read.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

        {
            let respond_future = validate_io_get_and_respond(&mut file_request_stream, vec![]);
            futures::pin_mut!(respond_future);
            advance_executor(&mut executor, &mut respond_future);
        }

        // Write request still waiting for the actual write.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

        // Set request is received immediately on write.
        {
            let set_value_future = verify_io_write(&mut file_request_stream, written_value);
            futures::pin_mut!(set_value_future);
            advance_executor(&mut executor, &mut set_value_future);
        }

        // Write request now complete.
        assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Ready(Result::Ok(_)));

        // Start listening for the sync request.
        let sync_future = verify_io_sync(&mut file_request_stream);
        futures::pin_mut!(sync_future);

        // Sync is received without a wait. Due to the way time works with executors, if there was
        // a delay, the test would stall since time never advances.
        advance_executor(&mut executor, &mut sync_future);
    }

    #[derive(Copy, Clone, PartialEq, Default)]
    struct WrongStruct;

    impl FidlStorageConvertible for WrongStruct {
        type Storable = Self;
        const KEY: &'static str = "WRONG_STRUCT";

        fn default_value() -> Self {
            Self
        }

        fn to_storable(self) -> Self::Storable {
            self
        }

        fn from_storable(storable: Self::Storable) -> Self {
            storable
        }
    }
    impl Persistable for WrongStruct {}
    fidl_empty_struct! { WrongStruct }

    // Test that attempting to write two kinds of structs to a storage instance that only supports
    // one results in a failure.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_write_with_mismatch_type_returns_error() {
        let file_proxy = testing::spawn_file_proxy();

        let storage =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], move |_| Clone::clone(&file_proxy));

        // Write successfully to storage once.
        let result = storage.write(TestStruct { value: VALUE2 }).await;
        assert!(result.is_ok());

        // Write to device storage again with a different type to validate that the type can't
        // be changed.
        let result = storage.write(WrongStruct).await;
        assert_matches!(result, Err(e) if e.to_string() == "Invalid data keyed by WRONG_STRUCT");
    }

    // Test that multiple writes to FidlStorage will cause a write each time, but will only
    // sync to the fs at an interval.
    #[test]
    fn test_multiple_write_debounce() {
        // Custom executor for this test so that we can advance the clock arbitrarily and verify the
        // state of the executor at any given point.
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
        executor.set_fake_time(Time::from_nanos(0));

        let (file_proxy, mut file_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let storage =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], move |_| Clone::clone(&file_proxy));

        let first_value = VALUE1;
        let second_value = VALUE2;

        // First write finishes immediately.
        {
            let value_to_write = TestStruct { value: first_value };
            let write_future = storage.write(value_to_write);
            futures::pin_mut!(write_future);

            // Initial cache check is done if no read was ever performed.
            assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

            {
                let respond_future = validate_io_get_and_respond(
                    &mut file_request_stream,
                    encode_persistent(&mut TestStruct { value: 0 }).unwrap(),
                );
                futures::pin_mut!(respond_future);
                advance_executor(&mut executor, &mut respond_future);
            }

            assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

            {
                let respond_future = validate_io_get_and_respond(&mut file_request_stream, vec![]);
                futures::pin_mut!(respond_future);
                advance_executor(&mut executor, &mut respond_future);
            }

            assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

            // First set request is received immediately on write.
            {
                let set_value_future = verify_io_write(&mut file_request_stream, first_value);
                futures::pin_mut!(set_value_future);
                advance_executor(&mut executor, &mut set_value_future);
            }

            assert_matches!(
                executor.run_until_stalled(&mut write_future),
                Poll::Ready(Result::Ok(_))
            );
        }

        // First sync request is received.
        {
            let sync_future = verify_io_sync(&mut file_request_stream);
            futures::pin_mut!(sync_future);
            advance_executor(&mut executor, &mut sync_future);
        }

        // Now we repeat the process with a second write request, which will need to advance the
        // fake time due to the timer.
        {
            let value_to_write = TestStruct { value: second_value };
            let write_future = storage.write(value_to_write);
            futures::pin_mut!(write_future);
            // Second write stalls on write.
            assert_matches!(executor.run_until_stalled(&mut write_future), Poll::Pending);

            // Second set request finishes immediately on write.
            {
                let set_value_future = verify_io_write(&mut file_request_stream, second_value);
                futures::pin_mut!(set_value_future);
                advance_executor(&mut executor, &mut set_value_future);
            }

            // Now that the fidl write is complete, the write is immediately ready.
            assert_matches!(
                executor.run_until_stalled(&mut write_future),
                Poll::Ready(Result::Ok(_))
            );
        }

        // Start waiting for sync request.
        let sync_future = verify_io_sync(&mut file_request_stream);
        futures::pin_mut!(sync_future);

        // TextExecutor stalls due to waiting on timer to finish.
        assert_matches!(executor.run_until_stalled(&mut sync_future), Poll::Pending);

        // Advance time to 1ms before the sync triggers.
        executor.set_fake_time(Time::from_nanos(
            ((MIN_FLUSH_INTERVAL_MS - 1) * 10_u64.pow(6)).try_into().unwrap(),
        ));

        // TextExecutor is still waiting on the time to finish.
        assert_matches!(executor.run_until_stalled(&mut sync_future), Poll::Pending);

        // Advance time so that the sync will trigger.
        executor.set_fake_time(Time::from_nanos(
            (MIN_FLUSH_INTERVAL_MS * 10_u64.pow(6)).try_into().unwrap(),
        ));

        // The fs receives a sync request after one timer cycle and the future terminates.
        advance_executor(&mut executor, &mut sync_future);
    }
}
