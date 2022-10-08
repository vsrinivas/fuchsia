// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        range::{ContentRange, Range},
        repository::{Error, RepoProvider, RepoStorage, Resource},
        util::file_stream,
    },
    anyhow::Result,
    async_fs::DirBuilder,
    camino::{Utf8Component, Utf8Path, Utf8PathBuf},
    fidl_fuchsia_developer_ffx_ext::RepositorySpec,
    fuchsia_merkle::Hash,
    futures::{
        future::BoxFuture, io::SeekFrom, stream::BoxStream, AsyncRead, AsyncSeekExt as _,
        FutureExt as _, Stream, StreamExt as _,
    },
    notify::{immediate_watcher, RecursiveMode, Watcher as _},
    std::{
        ffi::OsStr,
        pin::Pin,
        sync::Mutex,
        task::{Context, Poll},
        time::SystemTime,
    },
    tempfile::{NamedTempFile, TempPath},
    tracing::warn,
    tuf::{
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf1,
        repository::{
            FileSystemRepository as TufFileSystemRepository,
            FileSystemRepositoryBuilder as TufFileSystemRepositoryBuilder,
            RepositoryProvider as TufRepositoryProvider, RepositoryStorage as TufRepositoryStorage,
        },
    },
};

/// Describes how package blobs should be copied into the repository.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
pub enum CopyMode {
    /// Copy package blobs into the repository.
    ///
    /// This will create a Copy-on-Write (reflink) on file systems that support it.
    #[default]
    Copy,

    /// Create hard links from the package blobs into the repository.
    HardLink,
}

/// A builder to create a repository contained on the local file system.
pub struct FileSystemRepositoryBuilder {
    metadata_repo_path: Utf8PathBuf,
    blob_repo_path: Utf8PathBuf,
    copy_mode: CopyMode,
}

impl FileSystemRepositoryBuilder {
    /// Creates a [FileSystemRepositoryBuilder] where the TUF metadata is stored in
    /// `metadata_repo_path`, and the blobs are stored in `blob_repo_path`.
    pub fn new(metadata_repo_path: Utf8PathBuf, blob_repo_path: Utf8PathBuf) -> Self {
        FileSystemRepositoryBuilder {
            metadata_repo_path,
            blob_repo_path,
            copy_mode: CopyMode::Copy,
        }
    }

    /// Select which [CopyMode] to use when copying files into the repository.
    pub fn copy_mode(mut self, copy_mode: CopyMode) -> Self {
        self.copy_mode = copy_mode;
        self
    }

    /// Build a [FileSystemRepository].
    pub fn build(self) -> FileSystemRepository {
        FileSystemRepository {
            metadata_repo_path: self.metadata_repo_path.clone(),
            blob_repo_path: self.blob_repo_path,
            copy_mode: self.copy_mode,
            tuf_repo: TufFileSystemRepositoryBuilder::new(self.metadata_repo_path).build(),
        }
    }
}

/// Serve a repository from the file system.
#[derive(Debug)]
pub struct FileSystemRepository {
    metadata_repo_path: Utf8PathBuf,
    blob_repo_path: Utf8PathBuf,
    copy_mode: CopyMode,
    tuf_repo: TufFileSystemRepository<Pouf1>,
}

impl FileSystemRepository {
    /// Construct a [FileSystemRepositoryBuilder].
    pub fn builder(
        metadata_repo_path: Utf8PathBuf,
        blob_repo_path: Utf8PathBuf,
    ) -> FileSystemRepositoryBuilder {
        FileSystemRepositoryBuilder::new(metadata_repo_path, blob_repo_path)
    }

    /// Construct a [FileSystemRepository].
    pub fn new(metadata_repo_path: Utf8PathBuf, blob_repo_path: Utf8PathBuf) -> Self {
        Self::builder(metadata_repo_path, blob_repo_path).build()
    }

    fn fetch<'a>(
        &'a self,
        repo_path: &Utf8Path,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        let file_path = sanitize_path(repo_path, resource_path);
        async move {
            let file_path = file_path?;
            let mut file = async_fs::File::open(&file_path).await.map_err(|err| {
                if err.kind() == std::io::ErrorKind::NotFound {
                    Error::NotFound
                } else {
                    Error::Io(err)
                }
            })?;

            let total_len = file.metadata().await.map_err(Error::Io)?.len();

            let content_range = match range {
                Range::Full => ContentRange::Full { complete_len: total_len },
                Range::Inclusive { first_byte_pos, last_byte_pos } => {
                    if first_byte_pos > last_byte_pos
                        || first_byte_pos >= total_len
                        || last_byte_pos >= total_len
                    {
                        return Err(Error::RangeNotSatisfiable);
                    }

                    file.seek(SeekFrom::Start(first_byte_pos)).await.map_err(Error::Io)?;

                    ContentRange::Inclusive {
                        first_byte_pos,
                        last_byte_pos,
                        complete_len: total_len,
                    }
                }
                Range::From { first_byte_pos } => {
                    if first_byte_pos >= total_len {
                        return Err(Error::RangeNotSatisfiable);
                    }

                    file.seek(SeekFrom::Start(first_byte_pos)).await.map_err(Error::Io)?;

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
                    file.seek(SeekFrom::Start(start)).await.map_err(Error::Io)?;

                    ContentRange::Inclusive {
                        first_byte_pos: start,
                        last_byte_pos: total_len - 1,
                        complete_len: total_len,
                    }
                }
            };

            let content_len = content_range.content_len();

            Ok(Resource { content_range, stream: Box::pin(file_stream(content_len, file)) })
        }
        .boxed()
    }
}

impl RepoProvider for FileSystemRepository {
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::FileSystem {
            metadata_repo_path: self.metadata_repo_path.clone(),
            blob_repo_path: self.blob_repo_path.clone(),
        }
    }

    fn fetch_metadata_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.fetch(&self.metadata_repo_path, resource_path, range)
    }

    fn fetch_blob_range<'a>(
        &'a self,
        resource_path: &str,
        range: Range,
    ) -> BoxFuture<'a, Result<Resource, Error>> {
        self.fetch(&self.blob_repo_path, resource_path, range)
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
        let sender = Mutex::new(sender);

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
                if let Err(e) = sender.lock().unwrap().try_send(()) {
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

    fn blob_len<'a>(&'a self, path: &str) -> BoxFuture<'a, Result<u64>> {
        let file_path = sanitize_path(&self.blob_repo_path, path);
        async move {
            let file_path = file_path?;
            Ok(async_fs::metadata(&file_path).await?.len())
        }
        .boxed()
    }

    fn blob_modification_time<'a>(
        &'a self,
        path: &str,
    ) -> BoxFuture<'a, Result<Option<SystemTime>>> {
        let file_path = sanitize_path(&self.blob_repo_path, path);
        async move {
            let file_path = file_path?;
            Ok(Some(async_fs::metadata(&file_path).await?.modified()?))
        }
        .boxed()
    }
}

impl TufRepositoryProvider<Pouf1> for FileSystemRepository {
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.tuf_repo.fetch_metadata(meta_path, version)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        self.tuf_repo.fetch_target(target_path)
    }
}

impl TufRepositoryStorage<Pouf1> for FileSystemRepository {
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        self.tuf_repo.store_metadata(meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        self.tuf_repo.store_target(target_path, target)
    }
}

impl RepoStorage for FileSystemRepository {
    fn store_blob<'a>(&'a self, hash: &Hash, src: &Utf8Path) -> BoxFuture<'a, Result<()>> {
        let src = src.to_path_buf();
        let dst = sanitize_path(&self.blob_repo_path, &hash.to_string());

        async move {
            let dst = dst?;

            match self.copy_mode {
                CopyMode::Copy => {
                    let temp_path = create_temp_file(&dst).await?;
                    async_fs::copy(src, &temp_path).await?;
                    temp_path.persist(dst)?;
                }
                CopyMode::HardLink => {
                    async_fs::hard_link(src, &dst).await?;
                }
            }

            Ok(())
        }
        .boxed()
    }
}

async fn create_temp_file(path: &Utf8Path) -> Result<TempPath> {
    let temp_file = if let Some(parent) = path.parent() {
        DirBuilder::new().recursive(true).create(parent).await?;

        NamedTempFile::new_in(parent)?
    } else {
        NamedTempFile::new_in(".")?
    };

    Ok(temp_file.into_temp_path())
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            repository::repo_tests::{self, TestEnv as _},
            util::CHUNK_SIZE,
        },
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

        fn repo(&self) -> &dyn RepoProvider {
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

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_store_blob_copy() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let metadata_repo_path = dir.join("metadata");
        let blob_repo_path = dir.join("blobs");
        std::fs::create_dir(&metadata_repo_path).unwrap();
        std::fs::create_dir(&blob_repo_path).unwrap();

        let repo = FileSystemRepository::builder(metadata_repo_path, blob_repo_path.clone())
            .copy_mode(CopyMode::Copy)
            .build();

        // Store the blob.
        let contents = b"hello world";
        let path = dir.join("my-blob");
        std::fs::write(&path, contents).unwrap();

        let hash = fuchsia_merkle::from_slice(contents).root();
        assert_matches!(repo.store_blob(&hash, &path).await, Ok(()));

        // Make sure we can read it back.
        let blob_path = blob_repo_path.join(hash.to_string());
        let actual = std::fs::read(blob_path).unwrap();
        assert_eq!(&actual, &contents[..]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_store_blob_hard_link() {
        let tmp = tempfile::tempdir().unwrap();
        let dir = Utf8Path::from_path(tmp.path()).unwrap();

        let metadata_repo_path = dir.join("metadata");
        let blob_repo_path = dir.join("blobs");
        std::fs::create_dir(&metadata_repo_path).unwrap();
        std::fs::create_dir(&blob_repo_path).unwrap();

        let repo = FileSystemRepository::builder(metadata_repo_path, blob_repo_path.clone())
            .copy_mode(CopyMode::HardLink)
            .build();

        // Store the blob.
        let contents = b"hello world";
        let path = dir.join("my-blob");
        std::fs::write(&path, contents).unwrap();

        let hash = fuchsia_merkle::from_slice(contents).root();
        assert_matches!(repo.store_blob(&hash, &path).await, Ok(()));

        // Make sure we can read it back.
        let blob_path = blob_repo_path.join(hash.to_string());
        let actual = std::fs::read(&blob_path).unwrap();
        assert_eq!(&actual, &contents[..]);

        #[cfg(target_family = "unix")]
        async fn check_links(blob_path: &Utf8Path) {
            use std::os::unix::fs::MetadataExt as _;

            assert_eq!(std::fs::metadata(blob_path).unwrap().nlink(), 2);
        }

        #[cfg(not(target_family = "unix"))]
        async fn check_links(_blob_path: &Utf8Path) {}

        // Make sure the hard link count was incremented.
        check_links(&blob_path).await;
    }
}
