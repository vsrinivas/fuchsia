// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::UpdateState;
use anyhow::{bail, format_err, Context, Error};
use fidl::encoding::{decode_persistent, encode_persistent, Persistable};
use fidl::Status;
use fidl_fuchsia_io::DirectoryProxy;
use fuchsia_async::{Task, Time, Timer};
use fuchsia_fs::file::ReadError;
use fuchsia_fs::node::OpenError;
use fuchsia_fs::OpenFlags;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::channel::mpsc::{UnboundedReceiver, UnboundedSender};
use futures::future::OptionFuture;
use futures::lock::{Mutex, MutexGuard};
use futures::{FutureExt, StreamExt};
use std::any::Any;
use std::collections::HashMap;
use std::sync::Arc;
use zx::Duration;

/// Minimum amount of time between flushing to disk, in milliseconds. The flush call triggers
/// file I/O which is slow.
// TODO(fxbug.dev/95380) Investigate if this value should be updated for fidl-based storage.
const MIN_FLUSH_INTERVAL_MS: i64 = 500;
const MAX_FLUSH_INTERVAL_MS: i64 = 1_800_000; // 30 minutes
const MIN_FLUSH_DURATION: Duration = Duration::from_millis(MIN_FLUSH_INTERVAL_MS);

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

    storage_dir: DirectoryProxy,
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

    /// File path that will be used to write a temporary file when syncing to disk. After syncing,
    /// this file is deleted.
    ///
    /// The approach used for syncing is:
    /// * Write data to temp file
    /// * Rename temp file to permanent file
    /// * Delete temp file.
    ///
    /// This ensures that even if there's a power cut, the data in the permanent file is never
    /// partially written.
    temp_file_path: String,

    /// File path to used for permanent file storage on disk.
    file_path: String,
}

impl CachedStorage {
    /// Triggers a sync on the file proxy.
    async fn sync(&mut self, storage_dir: &DirectoryProxy) -> Result<(), Error> {
        let file_proxy = fuchsia_fs::directory::open_file(
            storage_dir,
            &self.temp_file_path,
            OpenFlags::CREATE
                | OpenFlags::TRUNCATE
                | OpenFlags::RIGHT_READABLE
                | OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .with_context(|| format!("unable to open {:?} for writing", self.temp_file_path))?;
        fuchsia_fs::file::write(&file_proxy, self.current_data.as_ref().unwrap())
            .await
            .context("failed to write data to file")?;
        file_proxy
            .close()
            .await
            .context("failed to call close on temp file")?
            .map_err(zx::Status::from_raw)?;
        fuchsia_fs::directory::rename(storage_dir, &self.temp_file_path, &self.file_path)
            .await
            .context("failed to rename temp file to permanent file")
    }
}

impl FidlStorage {
    /// Construct a fidl storage from:
    /// * The iterable item, which will produce the keys for storage
    /// * A generator function that will produce a file proxy for each key. It will return the temp
    ///     file path and final file path for storing the data for this key.
    ///
    /// On success, returns the FidlStorage as well as the list of background synchronizing tasks.
    /// The background tasks can be awaited or detached.
    pub(crate) async fn with_file_proxy<I, G>(
        iter: I,
        storage_dir: DirectoryProxy,
        files_generator: G,
    ) -> Result<(Self, Vec<Task<()>>), Error>
    where
        I: IntoIterator<Item = &'static str>,
        G: Fn(&'static str) -> Result<(String, String), Error>,
    {
        let mut typed_storage_map = HashMap::new();
        let iter = iter.into_iter();
        typed_storage_map.reserve(iter.size_hint().0);
        let mut sync_tasks = Vec::with_capacity(iter.size_hint().0);
        for key in iter {
            // Generate a separate file proxy for each key.
            let (flush_sender, flush_receiver) = futures::channel::mpsc::unbounded::<()>();
            let (temp_file_path, file_path) =
                files_generator(key).context("failed to generate file")?;

            let cached_storage = Arc::new(Mutex::new(CachedStorage {
                current_data: None,
                temp_file_path,
                file_path,
            }));
            let storage =
                TypedStorage { flush_sender, cached_storage: Arc::clone(&cached_storage) };

            // Each key has an independent flush queue.
            let sync_task = Task::spawn(Self::synchronize_task(
                Clone::clone(&storage_dir),
                cached_storage,
                flush_receiver,
            ));
            sync_tasks.push(sync_task);
            let _ = typed_storage_map.insert(key, storage);
        }
        Ok((
            FidlStorage {
                caching_enabled: true,
                debounce_writes: true,
                typed_storage_map,
                storage_dir,
            },
            sync_tasks,
        ))
    }

    async fn synchronize_task(
        storage_dir: DirectoryProxy,
        cached_storage: Arc<Mutex<CachedStorage>>,
        flush_receiver: UnboundedReceiver<()>,
    ) {
        let mut has_pending_flush = false;

        // The time of the last flush. Initialized to MIN_FLUSH_INTERVAL_MS before the
        // current time so that the first flush always goes through, no matter the
        // timing.
        let mut last_flush: Time = Time::now() - MIN_FLUSH_DURATION;

        // Timer for flush cooldown. OptionFuture allows us to wait on the future even
        // if it's None.
        let mut next_flush_timer: OptionFuture<Timer> = None.into();
        let mut next_flush_timer_fuse = next_flush_timer.fuse();
        let mut retries = 0;
        let mut retrying = false;

        let flush_fuse = flush_receiver.fuse();

        futures::pin_mut!(flush_fuse);
        loop {
            futures::select! {
                _ = flush_fuse.select_next_some() => {
                    // Flush currently unable to complete. Don't prevent exponential
                    // backoff from occurring.
                    if retrying {
                        continue;
                    }

                    // Received a request to do a flush.
                    let now = Time::now();
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
                    next_flush_timer = Some(Timer::new(next_flush_time)).into();
                    next_flush_timer_fuse = next_flush_timer.fuse();
                }

                _ = next_flush_timer_fuse => {
                    // Timer triggered, check for pending syncs.
                    if has_pending_flush {
                        let mut cached_storage = cached_storage.lock().await;

                        // If the sync fails, exponentionally backoff the syncs until a
                        // maximum wait time.
                        if let Err(e) = cached_storage.sync(&storage_dir).await {
                            retrying = true;
                            let flush_duration = Duration::from_millis(
                                2_i64.saturating_pow(retries)
                                    .saturating_mul(MIN_FLUSH_INTERVAL_MS)
                                    .min(MAX_FLUSH_INTERVAL_MS)
                            );
                            let next_flush_time = Time::now() + flush_duration;
                            fx_log_err!(
                                "Failed to sync write to disk for {:?}, delaying by {:?}, \
                                    caused by: {:?}",
                                cached_storage.file_path,
                                flush_duration,
                                e
                            );

                            // Reset the timer so we can try again in the future
                            next_flush_timer = Some(Timer::new(next_flush_time)).into();
                            next_flush_timer_fuse = next_flush_timer.fuse();
                            retries += 1;
                            continue;
                        }
                        last_flush = Time::now();
                        has_pending_flush = false;
                        retrying = false;
                        retries = 0;
                    }
                }

                complete => break,
            }
        }
    }

    #[cfg(test)]
    // TODO(fxbug.dev/91407) Remove allow once all tests have been migrated to fidl storage.
    #[allow(dead_code)]
    fn set_caching_enabled(&mut self, enabled: bool) {
        self.caching_enabled = enabled;
    }

    #[cfg(test)]
    // TODO(fxbug.dev/91407) Remove allow once all tests have been migrated to fidl storage.
    #[allow(dead_code)]
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
        let cached_value = match cached_storage.current_data.as_ref() {
            Some(cached_value) => Some(cached_value),
            None => {
                let file_proxy = fuchsia_fs::directory::open_file(
                    &self.storage_dir,
                    &cached_storage.file_path,
                    OpenFlags::RIGHT_READABLE,
                )
                .await;
                bytes = match file_proxy {
                    Ok(file_proxy) => match fuchsia_fs::file::read(&file_proxy).await {
                        Ok(bytes) => Some(bytes),
                        Err(ReadError::Open(OpenError::OpenError(e))) if e == Status::NOT_FOUND => {
                            None
                        }
                        Err(e) => {
                            bail!("failed to get value from fidl storage for {:?}: {:?}", key, e)
                        }
                    },
                    Err(OpenError::OpenError(Status::NOT_FOUND)) => None,
                    Err(e) => bail!("unable to read data on disk for {:?}: {:?}", key, e),
                };
                bytes.as_ref()
            }
        };

        Ok(if cached_value.map(|c| *c != new_value).unwrap_or(true) {
            cached_storage.current_data = Some(new_value);
            if !self.debounce_writes {
                // Not debouncing writes for testing, just sync immediately.
                cached_storage
                    .sync(&self.storage_dir)
                    .await
                    .with_context(|| format!("Failed to sync data for key {:?}", key))?;
            } else {
                typed_storage.flush_sender.unbounded_send(()).with_context(|| {
                    format!("flush_sender failed to send flush message, associated key is {}", key)
                })?;
            }
            UpdateState::Updated
        } else {
            UpdateState::Unchanged
        })
    }

    /// Write `new_value` to storage. The write will be persisted to disk at a set interval.
    pub async fn write<T>(&self, new_value: T) -> Result<UpdateState, Error>
    where
        T: FidlStorageConvertible,
    {
        let new_value = encode_persistent(&mut new_value.to_storable())?;
        self.inner_write(T::KEY, new_value).await
    }

    async fn get_inner(&self, key: &'static str) -> MutexGuard<'_, CachedStorage> {
        let typed_storage = self
            .typed_storage_map
            .get(key)
            // TODO(fxbug.dev/113292) Replace this with an error result.
            .unwrap_or_else(|| panic!("Invalid data keyed by {}", key));
        let mut cached_storage = typed_storage.cached_storage.lock().await;
        if cached_storage.current_data.is_none() || !self.caching_enabled {
            if let Some(file_proxy) = match fuchsia_fs::directory::open_file(
                &self.storage_dir,
                &cached_storage.file_path,
                OpenFlags::RIGHT_READABLE,
            )
            .await
            {
                Ok(file_proxy) => Some(file_proxy),
                Err(OpenError::OpenError(Status::NOT_FOUND)) => None,
                // TODO(fxbug.dev/113292) Replace this with an error result.
                Err(e) => panic!("failed to open file for {:?}: {:?}", key, e),
            } {
                let data = match fuchsia_fs::file::read(&file_proxy).await {
                    Ok(data) => Some(data),
                    Err(ReadError::ReadError(Status::NOT_FOUND)) => None,
                    // TODO(fxbug.dev/113292) Replace this with an error result.
                    Err(e) => panic!("failed to get fidl data from disk for {:?}: {:?}", key, e),
                };

                cached_storage.current_data = data;
            }
        }

        cached_storage
    }

    /// Gets the latest value cached locally, or loads the value from storage.
    /// Doesn't support multiple concurrent callers of the same struct.
    pub async fn get<T>(&self) -> T
    where
        T: FidlStorageConvertible,
    {
        self.get_inner(T::KEY)
            .await
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
mod tests {
    use super::*;
    use assert_matches::assert_matches;
    use fasync::TestExecutor;
    use fidl::endpoints::{create_proxy, ServerEnd};
    use fidl::{fidl_empty_struct, fidl_struct, Vmo};
    use fidl_fuchsia_io::DirectoryMarker;
    use fuchsia_async as fasync;
    use std::task::Poll;
    use test_case::test_case;
    use vfs::directory::entry::DirectoryEntry;
    use vfs::directory::mutable::simple::tree_constructor;
    use vfs::execution_scope::ExecutionScope;
    use vfs::file::vmo::asynchronous::test_utils::simple_init_vmo_with_capacity;
    use vfs::file::vmo::read_write;
    use vfs::mut_pseudo_directory;

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

    fn serve_vfs_dir(
        root: Arc<impl DirectoryEntry>,
    ) -> (DirectoryProxy, Arc<Mutex<HashMap<String, Vmo>>>) {
        let vmo_map = Arc::new(Mutex::new(HashMap::new()));
        let fs_scope = ExecutionScope::build()
            .entry_constructor(tree_constructor(move |_, _| {
                Ok(read_write(simple_init_vmo_with_capacity(b"", 100)))
            }))
            .new();
        let (client, server) = create_proxy::<DirectoryMarker>().unwrap();
        root.open(
            fs_scope,
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server.into_channel()),
        );
        (client, vmo_map)
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get() {
        let value_to_get = TestStruct { value: VALUE1 };
        let content = encode_persistent(&mut value_to_get.to_storable()).unwrap();
        let content_len = content.len();
        let fs = mut_pseudo_directory! {
            "xyz.pfidl" => read_write(simple_init_vmo_with_capacity(
                &content,
                content_len as u64
            ))
        };
        let (storage_dir, _vmo_map) = serve_vfs_dir(fs);
        let (storage, sync_tasks) =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], storage_dir, move |_| {
                Ok((String::from("xyz_temp.pfidl"), String::from("xyz.pfidl")))
            })
            .await
            .expect("should be able to generate file");
        for task in sync_tasks {
            task.detach();
        }
        let result = storage.get::<TestStruct>().await;

        assert_eq!(result.value, VALUE1);
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_default() {
        let fs = mut_pseudo_directory! {};
        let (storage_dir, _vmo_map) = serve_vfs_dir(fs);

        let (storage, sync_tasks) =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], storage_dir, move |_| {
                Ok((String::from("xyz_temp.pfidl"), String::from("xyz.pfidl")))
            })
            .await
            .expect("file proxy should be created");
        for task in sync_tasks {
            task.detach();
        }
        let result = storage.get::<TestStruct>().await;

        assert_eq!(result.value, VALUE0);
    }

    #[test]
    fn test_first_write_syncs_immediately() {
        let written_value = VALUE1;
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
        executor.set_fake_time(Time::from_nanos(0));

        let fs = mut_pseudo_directory! {};
        let (storage_dir, _vmo_map) = serve_vfs_dir(fs);

        let storage_fut = FidlStorage::with_file_proxy(
            vec![TestStruct::KEY],
            Clone::clone(&storage_dir),
            move |_| Ok((String::from("xyz_temp.pfidl"), String::from("xyz.pfidl"))),
        );
        futures::pin_mut!(storage_fut);

        let (storage, sync_tasks) =
            if let Poll::Ready(storage) = executor.run_until_stalled(&mut storage_fut) {
                storage.expect("file proxy should be created")
            } else {
                panic!("storage creation stalled");
            };

        assert_eq!(sync_tasks.len(), 1);
        let sync_task = sync_tasks.into_iter().next().unwrap();
        futures::pin_mut!(sync_task);

        // Write to device storage.
        let value_to_write = TestStruct { value: written_value };
        let write_future = storage.write(value_to_write);
        futures::pin_mut!(write_future);

        // Initial cache check is done if no read was ever performed.
        assert_matches!(
            executor.run_until_stalled(&mut write_future),
            Poll::Ready(Result::Ok(UpdateState::Updated))
        );

        // Storage is not yet ready.
        let open_fut =
            fuchsia_fs::directory::open_file(&storage_dir, "xyz.pfidl", OpenFlags::RIGHT_READABLE);
        futures::pin_mut!(open_fut);

        let result = executor.run_until_stalled(&mut open_fut);
        assert_matches!(
            result,
            Poll::Ready(Result::Err(OpenError::OpenError(zx::Status::NOT_FOUND)))
        );

        // Wake the zero timer.
        assert!(executor.wake_expired_timers());
        let _ = executor.run_until_stalled(&mut sync_task);

        // Validate that the file has been synced.
        let open_fut =
            fuchsia_fs::directory::open_file(&storage_dir, "xyz.pfidl", OpenFlags::RIGHT_READABLE);
        futures::pin_mut!(open_fut);

        let result = executor.run_until_stalled(&mut open_fut);
        let file = if let Poll::Ready(Result::Ok(file)) = result {
            file
        } else {
            panic!("result is not ready: {:?}", result);
        };

        // Validate the value matches what was set.
        let read_fut = fuchsia_fs::file::read_fidl::<TestStruct>(&file);
        futures::pin_mut!(read_fut);

        let result = executor.run_until_stalled(&mut read_fut);
        let data = if let Poll::Ready(Result::Ok(data)) = result {
            data
        } else {
            panic!("result is not ready: {:?}", result);
        };

        assert_eq!(data, value_to_write);
    }

    #[test]
    fn test_second_write_syncs_after_interval() {
        let written_value = VALUE1;
        let second_value = VALUE2;
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
        executor.set_fake_time(Time::from_nanos(0));

        let fs = mut_pseudo_directory! {};
        let (storage_dir, _vmo_map) = serve_vfs_dir(fs);

        let storage_fut = FidlStorage::with_file_proxy(
            vec![TestStruct::KEY],
            Clone::clone(&storage_dir),
            move |_| Ok((String::from("xyz_temp.pfidl"), String::from("xyz.pfidl"))),
        );
        futures::pin_mut!(storage_fut);

        let (storage, sync_tasks) =
            if let Poll::Ready(storage) = executor.run_until_stalled(&mut storage_fut) {
                storage.expect("file proxy should be created")
            } else {
                panic!("storage creation stalled");
            };

        let sync_task = sync_tasks.into_iter().next().unwrap();
        futures::pin_mut!(sync_task);

        // Write to device storage.
        let value_to_write = TestStruct { value: written_value };
        let write_future = storage.write(value_to_write);
        futures::pin_mut!(write_future);

        // Initial cache check is done if no read was ever performed.
        assert_matches!(
            executor.run_until_stalled(&mut write_future),
            Poll::Ready(Result::Ok(UpdateState::Updated))
        );

        // Storage is not yet ready.
        let open_fut =
            fuchsia_fs::directory::open_file(&storage_dir, "xyz.pfidl", OpenFlags::RIGHT_READABLE);
        futures::pin_mut!(open_fut);

        let result = executor.run_until_stalled(&mut open_fut);
        assert_matches!(
            result,
            Poll::Ready(Result::Err(OpenError::OpenError(zx::Status::NOT_FOUND)))
        );

        // Move executor past the sync interval.
        assert!(executor.wake_expired_timers());
        let _ = executor.run_until_stalled(&mut sync_task);

        // Validate that the file has been synced.
        let open_fut =
            fuchsia_fs::directory::open_file(&storage_dir, "xyz.pfidl", OpenFlags::RIGHT_READABLE);
        futures::pin_mut!(open_fut);

        let result = executor.run_until_stalled(&mut open_fut);
        let file = if let Poll::Ready(Result::Ok(file)) = result {
            file
        } else {
            panic!("result is not ready: {:?}", result);
        };

        // Validate the value matches what was set.
        let read_fut = fuchsia_fs::file::read_fidl::<TestStruct>(&file);
        futures::pin_mut!(read_fut);

        let result = executor.run_until_stalled(&mut read_fut);
        let data = if let Poll::Ready(Result::Ok(data)) = result {
            data
        } else {
            panic!("result is not ready: {:?}", result);
        };

        assert_eq!(data, value_to_write);

        // Write second time to device storage.
        let value_to_write2 = TestStruct { value: second_value };
        let write_future = storage.write(value_to_write2);
        futures::pin_mut!(write_future);

        // Initial cache check is done if no read was ever performed.
        assert_matches!(
            executor.run_until_stalled(&mut write_future),
            Poll::Ready(Result::Ok(UpdateState::Updated))
        );

        // Storage is not yet ready.
        let open_fut =
            fuchsia_fs::directory::open_file(&storage_dir, "xyz.pfidl", OpenFlags::RIGHT_READABLE);
        futures::pin_mut!(open_fut);

        // Should still equal old value.
        let result = executor.run_until_stalled(&mut open_fut);
        let file = if let Poll::Ready(Result::Ok(file)) = result {
            file
        } else {
            panic!("result is not ready: {:?}", result);
        };

        let read_fut = fuchsia_fs::file::read_fidl::<TestStruct>(&file);
        futures::pin_mut!(read_fut);

        let result = executor.run_until_stalled(&mut read_fut);
        let data = if let Poll::Ready(Result::Ok(data)) = result {
            data
        } else {
            panic!("result is not ready: {:?}", result);
        };

        assert_eq!(data, value_to_write);

        // Move executor to just before sync interval.
        executor.set_fake_time(Time::from_nanos(MIN_FLUSH_INTERVAL_MS * 1_000_000 - 1));
        assert!(!executor.wake_expired_timers());

        // Move executor to just after sync interval. It should run now.
        executor.set_fake_time(Time::from_nanos(MIN_FLUSH_INTERVAL_MS * 1_000_000));
        assert!(executor.wake_expired_timers());
        let _ = executor.run_until_stalled(&mut sync_task);

        // Validate that the file has been synced.
        let open_fut =
            fuchsia_fs::directory::open_file(&storage_dir, "xyz.pfidl", OpenFlags::RIGHT_READABLE);
        futures::pin_mut!(open_fut);

        let result = executor.run_until_stalled(&mut open_fut);
        let file = if let Poll::Ready(Result::Ok(file)) = result {
            file
        } else {
            panic!("result is not ready: {:?}", result);
        };

        // Validate the value matches what was set.
        let read_fut = fuchsia_fs::file::read_fidl::<TestStruct>(&file);
        futures::pin_mut!(read_fut);

        let result = executor.run_until_stalled(&mut read_fut);
        let data = if let Poll::Ready(Result::Ok(data)) = result {
            data
        } else {
            panic!("result is not ready: {:?}", result);
        };

        assert_eq!(data, value_to_write2);
    }

    #[derive(Copy, Clone, PartialEq, Default, Debug)]
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
    #[fasync::run_until_stalled(test)]
    async fn test_write_with_mismatch_type_returns_error() {
        let fs = mut_pseudo_directory! {};
        let (storage_dir, _vmo_map) = serve_vfs_dir(fs);

        let (storage, sync_tasks) =
            FidlStorage::with_file_proxy(vec![TestStruct::KEY], storage_dir, move |_| {
                Ok((String::from("xyz_temp.pfidl"), String::from("xyz.pfidl")))
            })
            .await
            .expect("file proxy should be created");
        for task in sync_tasks {
            task.detach();
        }

        // Write successfully to storage once.
        let result = storage.write(TestStruct { value: VALUE2 }).await;
        assert!(result.is_ok());

        // Write to device storage again with a different type to validate that the type can't
        // be changed.
        let result = storage.write(WrongStruct).await;
        assert_matches!(result, Err(e) if e.to_string() == "Invalid data keyed by WRONG_STRUCT");
    }

    macro_rules! run_to_ready {
        ($executor:expr, $fut:expr $(, $msg:expr $(,)?)? $(,)?) => {
            {
                let fut = $fut;
                futures::pin_mut!(fut);
                match $executor.run_until_stalled(&mut fut) {
                    Poll::Ready(result) => result,
                    Poll::Pending => run_to_ready!(@msg $($msg)?),
                }
            }
        };
        (@msg $msg:expr) => {
            panic!($msg)
        };
        (@msg) => {
            panic!("expected ready")
        }
    }

    macro_rules! assert_file {
        ($executor:expr, $storage_dir:expr, $file_name:literal, $expected_contents:expr) => {
            let open_fut = fuchsia_fs::directory::open_file(
                &$storage_dir,
                $file_name,
                OpenFlags::RIGHT_READABLE,
            );
            let file = run_to_ready!($executor, open_fut).expect("opening file");

            // Validate the value matches what was set.
            let read_fut = fuchsia_fs::file::read_fidl::<TestStruct>(&file);
            let data = run_to_ready!($executor, read_fut).expect("reading file");
            assert_eq!(data, $expected_contents);
        };
    }

    // Test that multiple writes to FidlStorage will cause a write each time, but will only
    // sync to the fs at an interval.
    #[test]
    fn test_multiple_write_debounce() {
        // Custom executor for this test so that we can advance the clock arbitrarily and verify the
        // state of the executor at any given point.
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
        executor.set_fake_time(Time::from_nanos(0));

        let fs = mut_pseudo_directory! {};
        let (storage_dir, _vmo_map) = serve_vfs_dir(fs);

        let storage_fut = FidlStorage::with_file_proxy(
            vec![TestStruct::KEY],
            Clone::clone(&storage_dir),
            move |_| Ok((String::from("xyz_temp.pfidl"), String::from("xyz.pfidl"))),
        );
        let (storage, sync_tasks) =
            run_to_ready!(executor, storage_fut, "storage creation stalled")
                .expect("file proxy should be created");
        let mut sync_task = sync_tasks.into_iter().next().unwrap();

        let first_value = VALUE1;
        let second_value = VALUE2;
        let third_value = VALUE0;

        // First write finishes immediately.
        let value_to_write = TestStruct { value: first_value };
        // Initial cache check is done if no read was ever performed.
        let result = run_to_ready!(executor, storage.write(value_to_write));
        assert_matches!(result, Result::Ok(UpdateState::Updated));

        // Storage is not yet ready.
        let result = run_to_ready!(
            executor,
            fuchsia_fs::directory::open_file(&storage_dir, "xyz.pfidl", OpenFlags::RIGHT_READABLE)
        );
        assert_matches!(result, Result::Err(OpenError::OpenError(zx::Status::NOT_FOUND)));

        // Wake the initial time without advancing the clock. Confirms that the first write is
        // "immediate".
        assert!(executor.wake_expired_timers());
        let _ = executor.run_until_stalled(&mut sync_task);

        // Validate that the file has been synced.
        assert_file!(executor, storage_dir, "xyz.pfidl", value_to_write);

        // Write second time to device storage.
        let value_to_write2 = TestStruct { value: second_value };
        let result = run_to_ready!(executor, storage.write(value_to_write2));
        // Value is marked as updated after the write.
        assert_matches!(result, Result::Ok(UpdateState::Updated));

        // Validate the updated values are still returned from the storage cache.
        let data = run_to_ready!(executor, storage.get::<TestStruct>());
        assert_eq!(data, value_to_write2);

        // But the data has not been persisted to disk.
        assert_file!(executor, storage_dir, "xyz.pfidl", value_to_write);

        // Now write a third time before advancing the clock.
        let value_to_write3 = TestStruct { value: third_value };
        let result = run_to_ready!(executor, storage.write(value_to_write3));
        // Value is marked as updated after the write.
        assert_matches!(result, Result::Ok(UpdateState::Updated));

        // Validate the updated values are still returned from the storage cache.
        let data = run_to_ready!(executor, storage.get::<TestStruct>());
        assert_eq!(data, value_to_write3);

        // But the data has still not been persistend to disk.
        assert_file!(executor, storage_dir, "xyz.pfidl", value_to_write);

        // Move clock to just before sync interval.
        executor.set_fake_time(Time::from_nanos(MIN_FLUSH_INTERVAL_MS * 1_000_000 - 1));
        assert!(!executor.wake_expired_timers());

        // And validate that the data has still not been synced to disk.
        assert_file!(executor, storage_dir, "xyz.pfidl", value_to_write);

        // Move executor to just after sync interval.
        executor.set_fake_time(Time::from_nanos(MIN_FLUSH_INTERVAL_MS * 1_000_000));
        assert!(executor.wake_expired_timers());
        let _ = executor.run_until_stalled(&mut sync_task);

        // Validate that the file has finally been synced.
        assert_file!(executor, storage_dir, "xyz.pfidl", value_to_write3);
    }

    fn serve_full_vfs_dir(
        root: Arc<impl DirectoryEntry>,
        recovers_after: usize,
    ) -> (DirectoryProxy, Arc<Mutex<HashMap<String, Vmo>>>) {
        let attempts = std::sync::Mutex::new(0);
        let vmo_map = Arc::new(Mutex::new(HashMap::new()));
        let fs_scope = ExecutionScope::build()
            .entry_constructor(tree_constructor(move |_, file_name| {
                let mut attempts_guard = attempts.lock().unwrap();
                if file_name == "abc_tmp.pfidl" && *attempts_guard < recovers_after {
                    *attempts_guard += 1;
                    println!("Force failing attempt {}", *attempts_guard);
                    Err(fidl::Status::NO_SPACE)
                } else {
                    Ok(read_write(simple_init_vmo_with_capacity(b"", 100)))
                }
            }))
            .new();
        let (client, server) = create_proxy::<DirectoryMarker>().unwrap();
        root.open(
            fs_scope,
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server.into_channel()),
        );
        (client, vmo_map)
    }

    // Tests that syncing can recover after a failed write. The test cases list the number of failed
    // attempts and the maximum amount of time waited from the previous write.
    #[test_case(1, 500)]
    #[test_case(2, 1_000)]
    #[test_case(3, 2_000)]
    #[test_case(4, 4_000)]
    #[test_case(5, 8_000)]
    #[test_case(6, 16_000)]
    #[test_case(7, 32_000)]
    #[test_case(8, 64_000)]
    #[test_case(9, 128_000)]
    #[test_case(10, 256_000)]
    #[test_case(11, 512_000)]
    #[test_case(12, 1_024_000)]
    #[test_case(13, 1_800_000)]
    #[test_case(14, 1_800_000)]
    fn test_exponential_backoff(retry_count: usize, max_wait_time: usize) {
        let mut executor = TestExecutor::new_with_fake_time().expect("Failed to create executor");
        executor.set_fake_time(Time::from_nanos(0));

        let fs = mut_pseudo_directory! {};
        // This served directory will allow writes after `retry_count` failed attempts.
        let (storage_dir, _) = serve_full_vfs_dir(fs, retry_count);

        let expected_data = vec![1];
        let cached_storage = Arc::new(Mutex::new(CachedStorage {
            current_data: Some(expected_data.clone()),
            temp_file_path: "abc_tmp.pfidl".to_owned(),
            file_path: "abc.pfidl".to_owned(),
        }));

        let (sender, receiver) = futures::channel::mpsc::unbounded();

        // Call spawn in a future since we have to be in an executor context to call spawn.
        let task = fasync::Task::spawn(FidlStorage::synchronize_task(
            Clone::clone(&storage_dir),
            Arc::clone(&cached_storage),
            receiver,
        ));
        futures::pin_mut!(task);

        executor.set_fake_time(Time::from_nanos(0));
        sender.unbounded_send(()).expect("can send flush signal");
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);

        let mut clock_nanos = 0;
        // (2^i) * 500 = exponential backoff.
        // 1,000,000 = convert ms to ns.
        for new_duration in (0..retry_count).map(|i| {
            (2_i64.pow(i as u32) * MIN_FLUSH_INTERVAL_MS).min(max_wait_time as i64) * 1_000_000
                - (i == retry_count - 1) as i64
        }) {
            executor.set_fake_time(Time::from_nanos(clock_nanos));
            assert!(executor.wake_expired_timers());
            // Task should not complete while retrying.
            assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);

            // Check that files don't exist.
            let result = run_to_ready!(
                executor,
                fuchsia_fs::directory::open_file(
                    &storage_dir,
                    "abc_tmp.pfidl",
                    OpenFlags::RIGHT_READABLE
                )
            );
            assert_matches!(result, Result::Err(OpenError::OpenError(zx::Status::NOT_FOUND)));
            let result = run_to_ready!(
                executor,
                fuchsia_fs::directory::open_file(
                    &storage_dir,
                    "abc.pfidl",
                    OpenFlags::RIGHT_READABLE
                )
            );
            assert_matches!(result, Result::Err(OpenError::OpenError(zx::Status::NOT_FOUND)));

            clock_nanos += new_duration;
        }

        executor.set_fake_time(Time::from_nanos(clock_nanos));
        // At this point the clock should be 1ns before the timer, so it shouldn't wake.
        assert!(!executor.wake_expired_timers());
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);

        // Check that files don't exist.
        let result = run_to_ready!(
            executor,
            fuchsia_fs::directory::open_file(
                &storage_dir,
                "abc_tmp.pfidl",
                OpenFlags::RIGHT_READABLE
            )
        );
        assert_matches!(result, Result::Err(OpenError::OpenError(zx::Status::NOT_FOUND)));
        let result = run_to_ready!(
            executor,
            fuchsia_fs::directory::open_file(&storage_dir, "abc.pfidl", OpenFlags::RIGHT_READABLE)
        );
        assert_matches!(result, Result::Err(OpenError::OpenError(zx::Status::NOT_FOUND)));

        // Now pass the timer where we can read the result.
        clock_nanos += 1;
        executor.set_fake_time(Time::from_nanos(clock_nanos));
        assert!(executor.wake_expired_timers());
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Pending);

        // Check that the file now has data.
        let open_fut =
            fuchsia_fs::directory::open_file(&storage_dir, "abc.pfidl", OpenFlags::RIGHT_READABLE);
        let file = run_to_ready!(executor, open_fut).expect("opening file");

        // Validate the value matches what was in the cache.
        let read_fut = fuchsia_fs::file::read(&file);
        let data = run_to_ready!(executor, read_fut).expect("reading file");
        assert_eq!(data, expected_data);

        drop(sender);
        // Ensure the task can properly exit.
        assert_eq!(executor.run_until_stalled(&mut task), Poll::Ready(()));
    }
}
