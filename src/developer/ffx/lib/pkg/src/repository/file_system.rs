// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{manager::RepositorySpec, Error, RepositoryBackend, Resource},
    anyhow::Result,
    bytes::{Bytes, BytesMut},
    futures::{ready, stream, AsyncRead, Stream},
    log::{error, warn},
    std::{
        cmp::min,
        io,
        path::{Component, Path, PathBuf},
        pin::Pin,
        task::Poll,
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

    fn get_tuf_repo(&self) -> Result<Box<(dyn RepositoryProvider<Json> + 'static)>, Error> {
        TufFileSystemRepositoryBuilder::<Json>::new(self.repo_path.clone())
            .build()
            .map(|r| Box::new(r) as Box<dyn RepositoryProvider<Json>>)
            .map_err(|e| anyhow::anyhow!(e).into())
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
        futures::StreamExt,
        matches::assert_matches,
        std::{
            fs::File,
            io::{self, Write as _},
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
}
