// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_io as fio,
    fuchsia_fs::file::AsyncReader,
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, prelude::*},
    std::{convert::TryInto as _, marker::PhantomData, path::Path},
    tuf::{
        metadata::{MetadataPath, MetadataVersion, TargetPath},
        pouf::Pouf,
        repository::{RepositoryProvider, RepositoryStorage},
    },
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Mode {
    // TODO(fxbug.dev/83257): change this to ReadOnly.
    ReadWrite,
    WriteOnly,
}

pub struct FuchsiaFileSystemRepository<D>
where
    D: Pouf,
{
    repo_proxy: fio::DirectoryProxy,
    _pouf: PhantomData<D>,
}

impl<D> FuchsiaFileSystemRepository<D>
where
    D: Pouf,
{
    pub fn new(repo_proxy: fio::DirectoryProxy) -> Self {
        Self { repo_proxy, _pouf: PhantomData }
    }

    #[cfg(test)]
    fn from_temp_dir(temp: &tempfile::TempDir) -> Self {
        Self::new(
            fuchsia_fs::directory::open_in_namespace(
                temp.path().to_str().unwrap(),
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            )
            .unwrap(),
        )
    }

    async fn fetch_path<'a>(
        &'a self,
        path: String,
        not_found_err: tuf::Error,
    ) -> tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>> {
        let file_proxy = fuchsia_fs::directory::open_file(
            &self.repo_proxy,
            &path,
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .map_err(|err| match err {
            fuchsia_fs::node::OpenError::OpenError(zx::Status::NOT_FOUND) => not_found_err,
            _ => make_opaque_error(anyhow!("opening '{}': {:?}", path, err)),
        })?;

        let reader: Box<dyn AsyncRead + Send + Unpin + 'a> = Box::new(
            AsyncReader::from_proxy(file_proxy)
                .context("creating AsyncReader for file")
                .map_err(make_opaque_error)?,
        );

        Ok(reader)
    }

    async fn store_path(
        &self,
        path: String,
        reader: &mut (dyn AsyncRead + Send + Unpin),
    ) -> tuf::Result<()> {
        if let Some(parent) = Path::new(&path).parent() {
            // This is needed because if there's no "/" in `path`, .parent() will return Some("")
            // instead of None.
            if !parent.as_os_str().is_empty() {
                let _sub_dir = fuchsia_fs::directory::create_directory_recursive(
                    &self.repo_proxy,
                    parent.to_str().ok_or_else(|| make_opaque_error(anyhow!("Invalid path")))?,
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                )
                .await
                .context("creating sub directories")
                .map_err(make_opaque_error)?;
            }
        }

        let (temp_path, temp_proxy) = fuchsia_fs::directory::create_randomly_named_file(
            &self.repo_proxy,
            &path,
            fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .with_context(|| format!("creating file: {}", path))
        .map_err(make_opaque_error)?;

        write_all(&temp_proxy, reader).await.map_err(make_opaque_error)?;

        let () = temp_proxy
            .sync()
            .await
            .context("sending sync request")
            .map_err(make_opaque_error)?
            .map_err(zx::Status::from_raw)
            .context("syncing file")
            .map_err(make_opaque_error)?;
        fuchsia_fs::file::close(temp_proxy)
            .await
            .context("closing file")
            .map_err(make_opaque_error)?;

        fuchsia_fs::directory::rename(&self.repo_proxy, &temp_path, &path)
            .await
            .context("renaming files")
            .map_err(make_opaque_error)
    }
}

fn make_opaque_error(e: Error) -> tuf::Error {
    tuf::Error::Opaque(format!("{:#}", e))
}

// Read everything from `reader` and write it to the file proxy.
async fn write_all(
    file: &fio::FileProxy,
    reader: &mut (dyn AsyncRead + Send + Unpin),
) -> Result<(), Error> {
    let mut buf = vec![0; fio::MAX_BUF.try_into().unwrap()];
    loop {
        let read_len = reader.read(&mut buf).await?;
        if read_len == 0 {
            return Ok(());
        }
        fuchsia_fs::file::write(file, &buf[..read_len]).await?;
    }
}

fn get_metadata_path<D: Pouf>(meta_path: &MetadataPath, version: MetadataVersion) -> String {
    let mut path = vec!["metadata"];
    let components = meta_path.components::<D>(version);
    path.extend(components.iter().map(|s| s.as_str()));
    path.join("/")
}

fn get_target_path(target_path: &TargetPath) -> String {
    let mut path = vec!["targets"];
    let components = target_path.components();
    path.extend(components.iter().map(|s| s.as_str()));
    path.join("/")
}

impl<D> RepositoryProvider<D> for FuchsiaFileSystemRepository<D>
where
    D: Pouf + Sync + Send,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = get_metadata_path::<D>(meta_path, version);
        self.fetch_path(path, tuf::Error::MetadataNotFound { path: meta_path.clone(), version })
            .boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        let path = get_target_path(target_path);
        self.fetch_path(path, tuf::Error::TargetNotFound(target_path.clone())).boxed()
    }
}

impl<D> RepositoryStorage<D> for FuchsiaFileSystemRepository<D>
where
    D: Pouf + Sync + Send,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        let path = get_metadata_path::<D>(meta_path, version);
        self.store_path(path, metadata).boxed()
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        let path = get_target_path(target_path);
        self.store_path(path, target).boxed()
    }
}

pub struct RWRepository<D, R> {
    inner: R,
    mode: Mode,
    _phantom: PhantomData<D>,
}

impl<D, R> RWRepository<D, R> {
    pub fn new(repo: R) -> Self {
        Self { inner: repo, mode: Mode::ReadWrite, _phantom: PhantomData }
    }

    pub fn switch_to_write_only_mode(&mut self) {
        self.mode = Mode::WriteOnly;
    }
}

impl<D, R> RepositoryStorage<D> for RWRepository<D, R>
where
    D: Pouf + Sync + Send,
    R: RepositoryStorage<D>,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        self.inner.store_metadata(meta_path, version, metadata)
    }

    fn store_target<'a>(
        &'a self,
        target_path: &TargetPath,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        self.inner.store_target(target_path, target)
    }
}

impl<D, R> RepositoryProvider<D> for RWRepository<D, R>
where
    D: Pouf + Sync + Send,
    R: RepositoryProvider<D>,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &MetadataPath,
        version: MetadataVersion,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        if self.mode == Mode::WriteOnly {
            return future::ready(Err(make_opaque_error(anyhow!(
                "attempt to read in write only mode"
            ))))
            .boxed();
        }
        self.inner.fetch_metadata(meta_path, version)
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &TargetPath,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin + 'a>>> {
        if self.mode == Mode::WriteOnly {
            return future::ready(Err(make_opaque_error(anyhow!(
                "attempt to read in write only mode"
            ))))
            .boxed();
        }
        self.inner.fetch_target(target_path)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_async as fasync, futures::io::Cursor, tempfile::tempdir, tuf::pouf::Pouf1,
    };

    fn get_random_buffer() -> Vec<u8> {
        use rand::prelude::*;

        let mut rng = rand::thread_rng();
        let len = rng.gen_range(1..100);
        let mut buffer = vec![0; len];
        rng.fill_bytes(&mut buffer);
        buffer
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_store_and_fetch_path() {
        let temp = tempdir().unwrap();
        let repo = FuchsiaFileSystemRepository::<Pouf1>::from_temp_dir(&temp);
        // Intentionally duplicate test cases to make sure we can overwrite existing file.
        for path in ["file", "a/b", "1/2/3", "a/b"] {
            let expected_data = get_random_buffer();

            let mut cursor = Cursor::new(&expected_data);
            repo.store_path(path.to_string(), &mut cursor).await.unwrap();

            let mut data = Vec::new();
            repo.fetch_path(
                path.to_string(),
                tuf::Error::MetadataNotFound {
                    path: MetadataPath::root(),
                    version: MetadataVersion::None,
                },
            )
            .await
            .unwrap()
            .read_to_end(&mut data)
            .await
            .unwrap();
            assert_eq!(data, expected_data);
        }

        for path in ["", ".", "/", "./a", "../a", "a/", "a//b", "a/./b", "a/../b"] {
            let mut cursor = Cursor::new(&path);
            let store_result = repo.store_path(path.to_string(), &mut cursor).await;
            assert!(store_result.is_err(), "path = {}", path);

            assert!(
                repo.fetch_path(
                    path.to_string(),
                    tuf::Error::MetadataNotFound {
                        path: MetadataPath::root(),
                        version: MetadataVersion::None
                    }
                )
                .await
                .is_err(),
                "path = {}",
                path
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_metadata() {
        let temp = tempdir().unwrap();
        let expected_data = get_random_buffer();
        std::fs::create_dir(temp.path().join("metadata")).unwrap();
        std::fs::write(temp.path().join("metadata/root.json"), &expected_data).unwrap();
        let repo = FuchsiaFileSystemRepository::<Pouf1>::from_temp_dir(&temp);
        let mut result = repo
            .fetch_metadata(&MetadataPath::new("root").unwrap(), MetadataVersion::None)
            .await
            .unwrap();

        let mut data = Vec::new();
        result.read_to_end(&mut data).await.unwrap();
        assert_eq!(data, expected_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_target() {
        let temp = tempdir().unwrap();
        let expected_data = get_random_buffer();
        std::fs::create_dir_all(temp.path().join("targets")).unwrap();
        std::fs::write(temp.path().join("targets/foo"), &expected_data).unwrap();
        let repo = FuchsiaFileSystemRepository::<Pouf1>::from_temp_dir(&temp);
        let mut result = repo.fetch_target(&TargetPath::new("foo").unwrap()).await.unwrap();

        let mut data = Vec::new();
        result.read_to_end(&mut data).await.unwrap();
        assert_eq!(data, expected_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_store_metadata() {
        let temp = tempdir().unwrap();
        let expected_data = get_random_buffer();
        let repo = FuchsiaFileSystemRepository::<Pouf1>::from_temp_dir(&temp);
        let mut cursor = Cursor::new(&expected_data);
        repo.store_metadata(
            &MetadataPath::new("root").unwrap(),
            MetadataVersion::Number(0),
            &mut cursor,
        )
        .await
        .unwrap();

        let data = std::fs::read(temp.path().join("metadata/0.root.json")).unwrap();
        assert_eq!(data, expected_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_store_target() {
        let temp = tempdir().unwrap();
        let expected_data = get_random_buffer();
        let repo = FuchsiaFileSystemRepository::<Pouf1>::from_temp_dir(&temp);
        let mut cursor = Cursor::new(&expected_data);
        repo.store_target(&TargetPath::new("foo/bar").unwrap(), &mut cursor).await.unwrap();

        let data = std::fs::read(temp.path().join("targets/foo/bar")).unwrap();
        assert_eq!(data, expected_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_fail_when_write_only() {
        let temp = tempdir().unwrap();
        let repo = FuchsiaFileSystemRepository::<Pouf1>::from_temp_dir(&temp);
        let mut repo = RWRepository::new(repo);
        std::fs::create_dir(temp.path().join("metadata")).unwrap();
        std::fs::write(temp.path().join("metadata/foo.json"), get_random_buffer()).unwrap();

        let mut data = Vec::new();
        repo.fetch_metadata(&MetadataPath::new("foo").unwrap(), MetadataVersion::None)
            .await
            .unwrap()
            .read_to_end(&mut data)
            .await
            .unwrap();

        repo.switch_to_write_only_mode();

        assert!(repo
            .fetch_metadata(&MetadataPath::new("foo").unwrap(), MetadataVersion::None)
            .await
            .is_err());
    }
}
