// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        range::{ContentRange, Range},
        repo_storage::RepoStorage,
        repository::{Error, RepositoryBackend, Resource},
    },
    anyhow::Result,
    bytes::{Bytes, BytesMut},
    camino::{Utf8Component, Utf8Path, Utf8PathBuf},
    fidl_fuchsia_developer_ffx_ext::RepositorySpec,
    futures::{
        io::SeekFrom,
        ready,
        stream::{self, BoxStream},
        AsyncRead, AsyncSeekExt, Stream, StreamExt,
    },
    notify::{immediate_watcher, RecursiveMode, Watcher as _},
    parking_lot::Mutex,
    std::{
        cmp::min,
        ffi::OsStr,
        io,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
        time::SystemTime,
    },
    tracing::{error, warn},
    tuf::{
        interchange::Json,
        repository::{
            FileSystemRepositoryBuilder as TufFileSystemRepositoryBuilder, RepositoryProvider,
            RepositoryStorageProvider,
        },
    },
};

/// Read files in chunks of this size off the local storage.
// Note: this is internally public to allow repository tests to check they work across chunks.
pub(crate) const CHUNK_SIZE: usize = 8_192;

/// Serve a repository from the file system.
#[derive(Debug, Clone)]
pub struct FileSystemRepository {
    metadata_repo_path: Utf8PathBuf,
    blob_repo_path: Utf8PathBuf,
}

impl FileSystemRepository {
    /// Construct a [FileSystemRepository].
    pub fn new(metadata_repo_path: Utf8PathBuf, blob_repo_path: Utf8PathBuf) -> Self {
        Self { metadata_repo_path, blob_repo_path }
    }

    async fn fetch(
        &self,
        repo_path: &Utf8Path,
        resource_path: &str,
        range: Range,
    ) -> Result<Resource, Error> {
        let file_path = sanitize_path(repo_path, resource_path)?;
        let mut file = async_fs::File::open(&file_path).await?;
        let total_len = file.metadata().await?.len();

        let content_range = match range {
            Range::Full => ContentRange::Full { complete_len: total_len },
            Range::Inclusive { first_byte_pos, last_byte_pos } => {
                if first_byte_pos > last_byte_pos
                    || first_byte_pos >= total_len
                    || last_byte_pos >= total_len
                {
                    return Err(Error::RangeNotSatisfiable);
                }

                file.seek(SeekFrom::Start(first_byte_pos)).await?;

                ContentRange::Inclusive { first_byte_pos, last_byte_pos, complete_len: total_len }
            }
            Range::From { first_byte_pos } => {
                if first_byte_pos >= total_len {
                    return Err(Error::RangeNotSatisfiable);
                }

                file.seek(SeekFrom::Start(first_byte_pos)).await?;

                ContentRange::Inclusive {
                    first_byte_pos,
                    last_byte_pos: total_len - 1,
                    complete_len: total_len,
                }
            }
            Range::Suffix { len } => {
                if len > total_len {
                    return Err(Error::RangeNotSatisfiable);
                }
                let start = total_len - len;
                file.seek(SeekFrom::Start(start)).await?;

                ContentRange::Inclusive {
                    first_byte_pos: start,
                    last_byte_pos: total_len - 1,
                    complete_len: total_len,
                }
            }
        };

        let content_len = content_range.content_len();

        Ok(Resource { content_range, stream: Box::pin(file_stream(file_path, content_len, file)) })
    }
}

#[async_trait::async_trait]
impl RepositoryBackend for FileSystemRepository {
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::FileSystem {
            metadata_repo_path: self.metadata_repo_path.clone(),
            blob_repo_path: self.blob_repo_path.clone(),
        }
    }

    async fn fetch_metadata(&self, resource_path: &str, range: Range) -> Result<Resource, Error> {
        self.fetch(&self.metadata_repo_path, resource_path, range).await
    }

    async fn fetch_blob(&self, resource_path: &str, range: Range) -> Result<Resource, Error> {
        self.fetch(&self.blob_repo_path, resource_path, range).await
    }

    fn supports_watch(&self) -> bool {
        true
    }

    fn watch(&self) -> Result<BoxStream<'static, ()>> {
        // Since all we are doing is signaling that the timestamp file is changed, it's it's fine
        // if the channel is full, since that just means we haven't consumed our notice yet.
        let (sender, receiver) = futures::channel::mpsc::channel(1);

        // FIXME(https://github.com/notify-rs/notify/pull/333): `immediate_watcher` takes an `Fn`
        // closure, which means it could theoretically call it concurrently (although the current
        // implementation does not do this). `sender` requires mutability, so we need to use
        // interior mutability. Notify may change to use `FnMut` in #333, which would remove our
        // need for interior mutability.
        //
        // We use a Mutex over a RefCell in case notify starts using the closure concurrently down
        // the road without us noticing.
        let sender = Arc::new(Mutex::new(sender));

        let mut watcher = immediate_watcher(move |event: notify::Result<notify::Event>| {
            let event = match event {
                Ok(event) => event,
                Err(err) => {
                    warn!("error receving notify event: {}", err);
                    return;
                }
            };

            // Send an event if any applied to timestamp.json.
            let timestamp_name = OsStr::new("timestamp.json");
            if event.paths.iter().any(|p| p.file_name() == Some(timestamp_name)) {
                if let Err(e) = sender.lock().try_send(()) {
                    if e.is_full() {
                        // It's okay to ignore a full channel, since that just means that the other
                        // side of the channel still has an outstanding notice, which should be the
                        // same effect if we re-sent the event.
                    } else if !e.is_disconnected() {
                        warn!("Error sending event: {:?}", e);
                    }
                }
            }
        })?;

        // Watch the repo path instead of directly watching timestamp.json to avoid
        // https://github.com/notify-rs/notify/issues/165.
        watcher.watch(&self.metadata_repo_path, RecursiveMode::NonRecursive)?;

        Ok(WatchStream { _watcher: watcher, receiver }.boxed())
    }

    async fn blob_len(&self, path: &str) -> Result<u64> {
        let file_path = sanitize_path(&self.blob_repo_path, path)?;
        Ok(async_fs::metadata(&file_path).await?.len())
    }

    async fn blob_modification_time(&self, path: &str) -> Result<Option<SystemTime>> {
        let file_path = sanitize_path(&self.blob_repo_path, path)?;
        Ok(Some(async_fs::metadata(&file_path).await?.modified()?))
    }

    fn get_tuf_repo(
        &self,
    ) -> Result<Box<(dyn RepositoryProvider<Json> + Send + Sync + 'static)>, Error> {
        let repo =
            TufFileSystemRepositoryBuilder::<Json>::new(self.metadata_repo_path.clone()).build()?;

        Ok(Box::new(repo))
    }
}

impl RepoStorage for FileSystemRepository {
    fn get_tuf_repo_storage(
        &self,
    ) -> Result<Box<dyn RepositoryStorageProvider<Json> + Send + Sync>> {
        let repo =
            TufFileSystemRepositoryBuilder::<Json>::new(self.metadata_repo_path.clone()).build()?;

        Ok(Box::new(repo))
    }
}

#[pin_project::pin_project]
struct WatchStream {
    _watcher: notify::RecommendedWatcher,
    #[pin]
    receiver: futures::channel::mpsc::Receiver<()>,
}

impl Stream for WatchStream {
    type Item = ();
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.project().receiver.poll_next(cx)
    }
}

/// Make sure the resource is inside the repo_path.
fn sanitize_path(repo_path: &Utf8Path, resource_path: &str) -> Result<Utf8PathBuf, Error> {
    let resource_path = Utf8Path::new(resource_path);

    let mut parts = vec![];
    for component in resource_path.components() {
        match component {
            Utf8Component::Normal(part) => {
                parts.push(part);
            }
            _ => {
                warn!("invalid resource_path: {}", resource_path);
                return Err(Error::InvalidPath(resource_path.into()));
            }
        }
    }

    let path = parts.into_iter().collect::<Utf8PathBuf>();
    Ok(repo_path.join(path))
}

/// Read a file and return a stream of [Bytes].
fn file_stream(
    path: Utf8PathBuf,
    mut len: u64,
    mut file: async_fs::File,
) -> impl Stream<Item = io::Result<Bytes>> {
    let mut buf = BytesMut::new();

    stream::poll_fn(move |cx| {
        if len == 0 {
            return Poll::Ready(None);
        }

        buf.resize(min(CHUNK_SIZE, len as usize), 0);

        // Read a chunk from the file.
        let n = match ready!(Pin::new(&mut file).poll_read(cx, &mut buf)) {
            Ok(n) => n as u64,
            Err(err) => {
                return Poll::Ready(Some(Err(err)));
            }
        };

        // If we read zero bytes, then the file changed size while we were streaming it.
        if n == 0 {
            error!("file ended before expected: {}", path);
            return Poll::Ready(None);
        }

        // Return the chunk read from the file. The file may have changed size during streaming, so
        // it's possible we could have read more than expected. If so, truncate the result to the
        // limited size.
        let mut chunk = buf.split_to(n as usize).freeze();
        if n > len {
            chunk = chunk.split_to(len as usize);
            len = 0;
        } else {
            len -= n;
        }

        Poll::Ready(Some(Ok(chunk)))
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::repository::repo_tests::{self, TestEnv as _},
        assert_matches::assert_matches,
        fuchsia_async as fasync,
        futures::{FutureExt, StreamExt},
        std::{fs::File, io::Write as _, time::Duration},
    };
    struct TestEnv {
        _tmp: tempfile::TempDir,
        metadata_path: Utf8PathBuf,
        blob_path: Utf8PathBuf,
        repo: FileSystemRepository,
    }

    impl TestEnv {
        fn new() -> Self {
            let tmp = tempfile::tempdir().unwrap();
            let dir = Utf8Path::from_path(tmp.path()).unwrap();
            let metadata_path = dir.join("metadata");
            let blob_path = dir.join("blobs");
            std::fs::create_dir(&metadata_path).unwrap();
            std::fs::create_dir(&blob_path).unwrap();

            Self {
                _tmp: tmp,
                metadata_path: metadata_path.clone(),
                blob_path: blob_path.clone(),
                repo: FileSystemRepository::new(metadata_path, blob_path),
            }
        }
    }

    #[async_trait::async_trait]
    impl repo_tests::TestEnv for TestEnv {
        fn supports_range(&self) -> bool {
            true
        }

        fn write_metadata(&self, path: &str, bytes: &[u8]) {
            let file_path = self.metadata_path.join(path);
            let mut f = File::create(file_path).unwrap();
            f.write_all(bytes).unwrap();
        }

        fn write_blob(&self, path: &str, bytes: &[u8]) {
            let file_path = self.blob_path.join(path);
            let mut f = File::create(file_path).unwrap();
            f.write_all(bytes).unwrap();
        }

        fn repo(&self) -> &dyn RepositoryBackend {
            &self.repo
        }
    }

    repo_tests::repo_test_suite! {
        env = TestEnv::new();
        chunk_size = CHUNK_SIZE;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_blob_modification_time() {
        let env = TestEnv::new();

        let f = File::create(env.blob_path.join("empty-blob")).unwrap();
        let blob_mtime = f.metadata().unwrap().modified().unwrap();
        drop(f);

        assert_matches!(
            env.repo.blob_modification_time("empty-blob").await,
            Ok(Some(t)) if t == blob_mtime
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reject_invalid_paths() {
        let env = TestEnv::new();
        env.write_metadata("empty", b"");

        assert_matches!(repo_tests::read_metadata(&env, "empty", Range::Full).await, Ok(body) if body == b"");
        assert_matches!(repo_tests::read_metadata(&env, "subdir/../empty", Range::Full).await,
            Err(Error::InvalidPath(path)) if path == Utf8Path::new("subdir/../empty")
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let env = TestEnv::new();

        // We support watch.
        assert!(env.repo.supports_watch());

        let mut watch_stream = env.repo.watch().unwrap();

        // Try to read from the stream. This should not return anything since we haven't created a
        // file yet.
        futures::select! {
            _ = watch_stream.next().fuse() => panic!("should not have received an event"),
            _ = fasync::Timer::new(Duration::from_millis(10)).fuse() => (),
        };

        // Next, write to the file and make sure we observe an event.
        env.write_metadata("timestamp.json", br#"{"version":1}"#);

        futures::select! {
            result = watch_stream.next().fuse() => {
                assert_eq!(result, Some(()));
            },
            _ = fasync::Timer::new(Duration::from_secs(10)).fuse() => {
                panic!("wrote to timestamp.json, but did not get an event");
            },
        };

        // Write to the file again and make sure we receive another event.
        env.write_metadata("timestamp.json", br#"{"version":2}"#);

        futures::select! {
            result = watch_stream.next().fuse() => {
                assert_eq!(result, Some(()));
            },
            _ = fasync::Timer::new(Duration::from_secs(10)).fuse() => {
                panic!("wrote to timestamp.json, but did not get an event");
            },
        };

        // FIXME(https://github.com/notify-rs/notify/pull/337): On OSX, notify uses a
        // crossbeam-channel in `Drop` to shut down the interior thread. Unfortunately this can
        // trip over an issue where OSX will tear down the thread local storage before shutting
        // down the thread, which can trigger a panic. To avoid this issue, sleep a little bit
        // after shutting down our stream.
        drop(watch_stream);
        fasync::Timer::new(Duration::from_millis(100)).await;
    }
}
