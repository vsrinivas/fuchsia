// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_io::{
        DirectoryProxy, FileProxy, MAX_BUF, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    futures::{future::BoxFuture, prelude::*},
    io_util::file::AsyncReader,
    std::{convert::TryInto as _, marker::PhantomData, path::Path},
    tuf::{
        crypto::{HashAlgorithm, HashValue},
        interchange::DataInterchange,
        metadata::{MetadataPath, MetadataVersion, TargetDescription, TargetPath},
        repository::{RepositoryProvider, RepositoryStorage},
    },
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Mode {
    ReadOnly,
    WriteOnly,
}

pub struct FuchsiaFileSystemRepository<D>
where
    D: DataInterchange,
{
    repo_proxy: DirectoryProxy,
    mode: Mode,
    _interchange: PhantomData<D>,
}

impl<D> FuchsiaFileSystemRepository<D>
where
    D: DataInterchange,
{
    #[cfg(test)]
    pub fn new(repo_proxy: DirectoryProxy) -> Self {
        Self { repo_proxy, mode: Mode::ReadOnly, _interchange: PhantomData }
    }

    #[cfg(test)]
    fn from_temp_dir(temp: &tempfile::TempDir) -> Self {
        Self::new(
            io_util::directory::open_in_namespace(
                temp.path().to_str().unwrap(),
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            )
            .unwrap(),
        )
    }

    // Switches the repo to write only mode, there's no way back.
    #[cfg(test)]
    pub fn switch_to_write_only_mode(&mut self) {
        self.mode = Mode::WriteOnly;
    }

    async fn fetch_path(&self, path: String) -> tuf::Result<Box<dyn AsyncRead + Send + Unpin>> {
        if self.mode == Mode::WriteOnly {
            return Err(make_opaque_error(anyhow!("attempt to read in write only mode")));
        }

        let file_proxy =
            io_util::directory::open_file(&self.repo_proxy, &path, OPEN_RIGHT_READABLE)
                .await
                .map_err(|err| match err {
                    io_util::node::OpenError::OpenError(zx::Status::NOT_FOUND) => {
                        tuf::Error::NotFound
                    }
                    _ => make_opaque_error(anyhow!("opening '{}': {:?}", path, err)),
                })?;

        let reader: Box<dyn AsyncRead + Send + Unpin> = Box::new(
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
        if self.mode == Mode::ReadOnly {
            return Err(make_opaque_error(anyhow!("attempt to write in read only mode")));
        }

        if let Some(parent) = Path::new(&path).parent() {
            // This is needed because if there's no "/" in `path`, .parent() will return Some("")
            // instead of None.
            if !parent.as_os_str().is_empty() {
                let _sub_dir = io_util::create_sub_directories(&self.repo_proxy, parent)
                    .context("creating sub directories")
                    .map_err(make_opaque_error)?;
            }
        }

        let (temp_path, temp_proxy) = io_util::directory::create_randomly_named_file(
            &self.repo_proxy,
            &path,
            OPEN_RIGHT_WRITABLE,
        )
        .await
        .with_context(|| format!("creating file: {}", path))
        .map_err(make_opaque_error)?;

        write_all(&temp_proxy, reader).await.map_err(make_opaque_error)?;

        let status =
            temp_proxy.sync().await.context("sending sync request").map_err(make_opaque_error)?;
        zx::Status::ok(status).context("syncing file").map_err(make_opaque_error)?;
        io_util::file::close(temp_proxy)
            .await
            .context("closing file")
            .map_err(make_opaque_error)?;

        io_util::directory::rename(&self.repo_proxy, &temp_path, &path)
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
    file: &FileProxy,
    reader: &mut (dyn AsyncRead + Send + Unpin),
) -> Result<(), Error> {
    let mut buf = vec![0; MAX_BUF.try_into().unwrap()];
    loop {
        let read_len = reader.read(&mut buf).await?;
        if read_len == 0 {
            return Ok(());
        }
        io_util::file::write(file, &buf[..read_len]).await?;
    }
}

fn get_metadata_path<D: DataInterchange>(
    meta_path: &MetadataPath,
    version: &MetadataVersion,
) -> String {
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
    D: DataInterchange + Sync + Send,
{
    fn fetch_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        _max_length: Option<usize>,
        _hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin>>> {
        let path = get_metadata_path::<D>(meta_path, version);
        self.fetch_path(path).boxed()
    }

    fn fetch_target<'a>(
        &'a self,
        target_path: &'a TargetPath,
        _target_description: &'a TargetDescription,
    ) -> BoxFuture<'a, tuf::Result<Box<dyn AsyncRead + Send + Unpin>>> {
        let path = get_target_path(target_path);
        self.fetch_path(path).boxed()
    }
}

impl<D> RepositoryStorage<D> for FuchsiaFileSystemRepository<D>
where
    D: DataInterchange + Sync + Send,
{
    fn store_metadata<'a>(
        &'a self,
        meta_path: &'a MetadataPath,
        version: &'a MetadataVersion,
        metadata: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
    ) -> BoxFuture<'a, tuf::Result<()>> {
        let path = get_metadata_path::<D>(meta_path, version);
        self.store_path(path, metadata).boxed()
    }

    fn store_target<'a>(
        &'a self,
        target: &'a mut (dyn AsyncRead + Send + Unpin + 'a),
        target_path: &'a TargetPath,
    ) -> BoxFuture<'a, tuf::Result<()>> {
        let path = get_target_path(target_path);
        self.store_path(path, target).boxed()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async as fasync,
        futures::io::Cursor,
        tempfile::tempdir,
        tuf::{crypto::HashAlgorithm, interchange::Json, metadata::TargetDescription},
    };

    fn get_random_buffer() -> Vec<u8> {
        use rand::prelude::*;

        let mut rng = rand::thread_rng();
        let len = rng.gen_range(1, 100);
        let mut buffer = vec![0; len];
        rng.fill_bytes(&mut buffer);
        buffer
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_store_and_fetch_path() {
        let temp = tempdir().unwrap();
        let mut repo = FuchsiaFileSystemRepository::<Json>::from_temp_dir(&temp);
        // Intentionally duplicate test cases to make sure we can overwrite existing file.
        for path in ["file", "a/b", "1/2/3", "a/b"] {
            let expected_data = get_random_buffer();

            repo.mode = Mode::WriteOnly;
            let mut cursor = Cursor::new(&expected_data);
            repo.store_path(path.to_string(), &mut cursor).await.unwrap();

            repo.mode = Mode::ReadOnly;
            let mut data = Vec::new();
            repo.fetch_path(path.to_string()).await.unwrap().read_to_end(&mut data).await.unwrap();
            assert_eq!(data, expected_data);
        }

        for path in ["", ".", "/", "./a", "../a", "a/", "a//b", "a/./b", "a/../b"] {
            repo.mode = Mode::WriteOnly;
            let mut cursor = Cursor::new(&path);
            let store_result = repo.store_path(path.to_string(), &mut cursor).await;
            assert!(store_result.is_err(), "path = {}", path);

            repo.mode = Mode::ReadOnly;
            assert!(repo.fetch_path(path.to_string()).await.is_err(), "path = {}", path);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_metadata() {
        let temp = tempdir().unwrap();
        let expected_data = get_random_buffer();
        std::fs::create_dir(temp.path().join("metadata")).unwrap();
        std::fs::write(temp.path().join("metadata/root.json"), &expected_data).unwrap();
        let repo = FuchsiaFileSystemRepository::<Json>::from_temp_dir(&temp);
        let mut result = repo
            .fetch_metadata(&MetadataPath::new("root").unwrap(), &MetadataVersion::None, None, None)
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
        let repo = FuchsiaFileSystemRepository::<Json>::from_temp_dir(&temp);
        let target_description =
            TargetDescription::from_reader(&expected_data[..], &[HashAlgorithm::Sha256]).unwrap();
        let mut result = repo
            .fetch_target(&TargetPath::new("foo".into()).unwrap(), &target_description)
            .await
            .unwrap();

        let mut data = Vec::new();
        result.read_to_end(&mut data).await.unwrap();
        assert_eq!(data, expected_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_store_metadata() {
        let temp = tempdir().unwrap();
        let expected_data = get_random_buffer();
        let mut repo = FuchsiaFileSystemRepository::<Json>::from_temp_dir(&temp);
        repo.switch_to_write_only_mode();
        let mut cursor = Cursor::new(&expected_data);
        repo.store_metadata(
            &MetadataPath::new("root").unwrap(),
            &MetadataVersion::Number(0),
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
        let mut repo = FuchsiaFileSystemRepository::<Json>::from_temp_dir(&temp);
        repo.switch_to_write_only_mode();
        let mut cursor = Cursor::new(&expected_data);
        repo.store_target(&mut cursor, &TargetPath::new("foo/bar".into()).unwrap()).await.unwrap();

        let data = std::fs::read(temp.path().join("targets/foo/bar")).unwrap();
        assert_eq!(data, expected_data);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fetch_fail_when_write_only() {
        let temp = tempdir().unwrap();
        let mut repo = FuchsiaFileSystemRepository::<Json>::from_temp_dir(&temp);
        std::fs::write(temp.path().join("foo"), get_random_buffer()).unwrap();

        let mut data = Vec::new();
        repo.fetch_path("foo".to_string()).await.unwrap().read_to_end(&mut data).await.unwrap();

        repo.switch_to_write_only_mode();

        assert!(repo.fetch_path("foo".to_string()).await.is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_store_fail_when_read_only() {
        let temp = tempdir().unwrap();
        let mut repo = FuchsiaFileSystemRepository::<Json>::from_temp_dir(&temp);

        let mut cursor = Cursor::new(get_random_buffer());
        let result = repo.store_path("foo".to_string(), &mut cursor).await;
        assert!(result.is_err());

        repo.switch_to_write_only_mode();

        repo.store_path("foo".to_string(), &mut cursor).await.unwrap();
    }
}
