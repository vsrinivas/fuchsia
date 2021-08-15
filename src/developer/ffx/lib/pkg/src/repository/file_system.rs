// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{Error, RepositoryBackend, Resource},
    anyhow::Result,
    bytes::{Bytes, BytesMut},
    fidl_fuchsia_developer_bridge_ext::RepositorySpec,
    futures::{
        ready,
        stream::{self, BoxStream},
        AsyncRead, Stream, StreamExt,
    },
    log::{error, warn},
    notify::{immediate_watcher, RecursiveMode, Watcher as _},
    parking_lot::Mutex,
    std::{
        cmp::min,
        ffi::OsStr,
        io,
        path::{Component, Path, PathBuf},
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
        time::SystemTime,
    },
    tuf::{
        interchange::Json,
        repository::{
            FileSystemRepositoryBuilder as TufFileSystemRepositoryBuilder, RepositoryProvider,
        },
    },
};

/// Read files in chunks of this size off the local storage.
const CHUNK_SIZE: usize = 8_192;
/// Serve a repository from the file system.
#[derive(Debug)]
pub struct FileSystemRepository {
    repo_path: PathBuf,
}

impl FileSystemRepository {
    /// Construct a [FileSystemRepository].
    pub fn new(repo_path: PathBuf) -> Self {
        Self { repo_path }
    }
}

#[async_trait::async_trait]
impl RepositoryBackend for FileSystemRepository {
    fn spec(&self) -> RepositorySpec {
        RepositorySpec::FileSystem { path: self.repo_path.clone() }
    }

    async fn fetch(&self, resource_path: &str) -> Result<Resource, Error> {
        let file_path = sanitize_path(&self.repo_path, resource_path)?;

        let file = async_fs::File::open(&file_path).await?;
        let len = file.metadata().await?.len();

        Ok(Resource { len, stream: Box::pin(file_stream(file_path, len as usize, file)) })
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
        watcher.watch(&self.repo_path, RecursiveMode::NonRecursive)?;

        Ok(WatchStream { _watcher: watcher, receiver }.boxed())
    }

    async fn target_modification_time(&self, path: &str) -> Result<Option<SystemTime>> {
        let file_path = sanitize_path(&self.repo_path, path)?;
        Ok(Some(async_fs::metadata(&file_path).await?.modified()?))
    }

    fn get_tuf_repo(&self) -> Result<Box<(dyn RepositoryProvider<Json> + 'static)>, Error> {
        let repo = TufFileSystemRepositoryBuilder::<Json>::new(self.repo_path.clone()).build()?;

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
fn sanitize_path(repo_path: &Path, resource_path: &str) -> Result<PathBuf, Error> {
    let resource_path = Path::new(resource_path);

    for component in resource_path.components() {
        match component {
            Component::Normal(_) => {}
            _ => {
                warn!("invalid resource_path: {}", resource_path.display());
                return Err(Error::InvalidPath(resource_path.into()));
            }
        }
    }

    Ok(repo_path.join(resource_path))
}

/// Read a file and return a stream of [Bytes].
fn file_stream(
    path: PathBuf,
    mut len: usize,
    mut file: async_fs::File,
) -> impl Stream<Item = io::Result<Bytes>> {
    let mut buf = BytesMut::new();

    stream::poll_fn(move |cx| {
        if len == 0 {
            return Poll::Ready(None);
        }

        buf.resize(min(CHUNK_SIZE, len), 0);

        // Read a chunk from the file.
        let n = match ready!(Pin::new(&mut file).poll_read(cx, &mut buf)) {
            Ok(n) => n,
            Err(err) => {
                return Poll::Ready(Some(Err(err)));
            }
        };

        // If we read zero bytes, then the file changed size while we were streaming it.
        if n == 0 {
            error!("file ended before expected: {}", path.display());
            return Poll::Ready(None);
        }

        // Return the chunk read from the file. The file may have changed size during streaming, so
        // it's possible we could have read more than expected. If so, truncate the result to the
        // limited size.
        let mut chunk = buf.split_to(n).freeze();
        if n > len {
            chunk = chunk.split_to(len);
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
        fuchsia_async as fasync,
        futures::{FutureExt, StreamExt},
        matches::assert_matches,
        std::{
            fs::{self, File},
            io::{self, Write as _},
            time::Duration,
        },
    };

    async fn read(repo: &FileSystemRepository, path: &str) -> Result<Vec<u8>, Error> {
        let f = repo.fetch(path).await?;
        read_stream(f.stream).await
    }

    async fn read_stream<T>(mut stream: T) -> Result<Vec<u8>, Error>
    where
        T: Stream<Item = io::Result<Bytes>> + Unpin,
    {
        let mut body = vec![];
        while let Some(next) = stream.next().await {
            let chunk = next?;
            body.extend(&chunk);
        }

        Ok(body)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fetch_missing() {
        let d = tempfile::tempdir().unwrap();
        let repo = FileSystemRepository::new(d.path().to_path_buf());
        assert_matches!(repo.fetch("does-not-exist").await, Err(Error::NotFound));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fetch_empty() {
        let d = tempfile::tempdir().unwrap();
        let repo = FileSystemRepository::new(d.path().to_path_buf());

        let f = File::create(d.path().join("empty")).unwrap();
        drop(f);

        assert_matches!(read(&repo, "empty").await, Ok(body) if body == b"");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_timestamp() {
        let d = tempfile::tempdir().unwrap();
        let repo = FileSystemRepository::new(d.path().to_path_buf());

        let f = File::create(d.path().join("empty")).unwrap();
        let mtime = f.metadata().unwrap().modified().unwrap();
        drop(f);

        assert_eq!(mtime, repo.target_modification_time("empty").await.unwrap().unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fetch_small() {
        let d = tempfile::tempdir().unwrap();
        let repo = FileSystemRepository::new(d.path().to_path_buf());
        let body = b"hello world";
        let mut f = File::create(d.path().join("small")).unwrap();
        f.write(body).unwrap();
        drop(f);

        assert_matches!(read(&repo, "small").await, Ok(b) if b == body);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_fetch_chunks() {
        let d = tempfile::tempdir().unwrap();
        let repo = FileSystemRepository::new(d.path().to_path_buf());
        let chunks = [0, 1, CHUNK_SIZE - 1, CHUNK_SIZE, CHUNK_SIZE + 1, CHUNK_SIZE * 2 + 1];
        for size in &chunks {
            let path = format!("{}", size);
            let body = vec![0; *size];
            let mut f = File::create(d.path().join(&path)).unwrap();
            f.write(&body).unwrap();
            drop(f);

            let actual = read(&repo, &path).await.unwrap();
            assert_eq!(body, actual);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_reject_invalid_paths() {
        let d = tempfile::tempdir().unwrap();
        let repo = FileSystemRepository::new(d.path().to_path_buf());
        std::fs::create_dir(d.path().join("subdir")).unwrap();
        let f = std::fs::File::create(d.path().join("empty")).unwrap();
        drop(f);

        assert_matches!(read(&repo, "empty").await, Ok(body) if body == b"");
        assert_matches!(
            read(&repo, "subdir/../empty").await,
            Err(Error::InvalidPath(path)) if path == Path::new("subdir/../empty"));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_watch() {
        let d = tempfile::tempdir().unwrap();
        let repo = FileSystemRepository::new(d.path().to_path_buf());

        assert!(repo.supports_watch());
        let mut watch_stream = repo.watch().unwrap();

        // Try to read from the stream. This should not return anything since we haven't created a
        // file yet.
        futures::select! {
            _ = watch_stream.next().fuse() => panic!("should not have received an event"),
            _ = fasync::Timer::new(Duration::from_millis(10)).fuse() => (),
        };

        // Next, write to the file and make sure we observe an event.
        let timestamp_file = d.path().join("timestamp.json");
        fs::write(&timestamp_file, br#"{"version":1}"#).unwrap();

        futures::select! {
            result = watch_stream.next().fuse() => {
                assert_eq!(result, Some(()));
            },
            _ = fasync::Timer::new(Duration::from_secs(10)).fuse() => {
                panic!("wrote to timestamp.json, but did not get an event");
            },
        };

        // Write to the file again and make sure we receive another event.
        fs::write(&timestamp_file, br#"{"version":2}"#).unwrap();

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
