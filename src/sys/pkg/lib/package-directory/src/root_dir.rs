// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        meta_as_dir::MetaAsDir,
        meta_as_file::MetaAsFile,
        meta_file::{MetaFile, MetaFileLocation},
        meta_subdir::MetaSubdir,
        non_meta_subdir::NonMetaSubdir,
        Error,
    },
    anyhow::Context as _,
    async_trait::async_trait,
    async_utils::async_once::Once,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_fs::file::AsyncReadAtExt as _,
    fuchsia_pkg::MetaContents,
    fuchsia_zircon as zx,
    futures::stream::StreamExt as _,
    std::{collections::HashMap, sync::Arc},
    tracing::error,
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::io1::DerivedConnection, entry::EntryInfo,
            immutable::connection::io1::ImmutableConnection, traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

/// The root directory of Fuchsia package.
#[derive(Debug)]
pub struct RootDir<S: crate::NonMetaStorage> {
    pub(crate) non_meta_storage: S,
    pub(crate) hash: fuchsia_hash::Hash,
    pub(crate) meta_far: fio::FileProxy,
    // The keys are object relative path expressions.
    pub(crate) meta_files: HashMap<String, MetaFileLocation>,
    // The keys are object relative path expressions.
    pub(crate) non_meta_files: HashMap<String, fuchsia_hash::Hash>,
    meta_far_vmo: Once<zx::Vmo>,
}

impl<S: crate::NonMetaStorage> RootDir<S> {
    /// Loads the package metadata given by `hash` from `non_meta_storage`, returning an object
    /// representing the package, backed by `non_meta_storage`.
    pub async fn new(non_meta_storage: S, hash: fuchsia_hash::Hash) -> Result<Self, Error> {
        let meta_far = open_for_read(&non_meta_storage, &hash, fio::OpenFlags::DESCRIBE)
            .map_err(Error::OpenMetaFar)?;

        let mut async_reader =
            match fuchsia_archive::AsyncReader::new(fuchsia_fs::file::BufferedAsyncReadAt::new(
                fuchsia_fs::file::AsyncFile::from_proxy(Clone::clone(&meta_far)),
            ))
            .await
            {
                Ok(async_reader) => async_reader,
                Err(e) => {
                    if matches!(
                        meta_far.take_event_stream().next().await,
                        Some(Ok(fio::FileEvent::OnOpen_{s, ..}))
                            if s == zx::Status::NOT_FOUND.into_raw()
                    ) {
                        return Err(Error::MissingMetaFar);
                    }
                    return Err(Error::ArchiveReader(e));
                }
            };

        let reader_list = async_reader.list();

        let mut meta_files = HashMap::with_capacity(reader_list.len());

        for entry in reader_list {
            let path = std::str::from_utf8(entry.path())
                .map_err(|source| Error::NonUtf8MetaEntry {
                    source,
                    path: entry.path().to_owned(),
                })?
                .to_owned();
            if path.starts_with("meta/") {
                for (i, _) in path.match_indices("/").skip(1) {
                    if meta_files.contains_key(&path[..i]) {
                        return Err(Error::FileDirectoryCollision { path: path[..i].to_string() });
                    }
                }
                meta_files.insert(
                    path,
                    MetaFileLocation { offset: entry.offset(), length: entry.length() },
                );
            }
        }

        let meta_contents_bytes =
            async_reader.read_file(b"meta/contents").await.map_err(Error::ReadMetaContents)?;

        let non_meta_files = MetaContents::deserialize(&meta_contents_bytes[..])
            .map_err(Error::DeserializeMetaContents)?
            .into_contents();

        let meta_far_vmo = Default::default();

        Ok(RootDir { non_meta_storage, hash, meta_far, meta_files, non_meta_files, meta_far_vmo })
    }

    /// Returns the contents, if present, of the file at object relative path expression `path`.
    /// https://fuchsia.dev/fuchsia-src/concepts/process/namespaces?hl=en#object_relative_path_expressions
    pub async fn read_file(&self, path: &str) -> Result<Vec<u8>, ReadFileError> {
        if let Some(hash) = self.non_meta_files.get(path) {
            let blob = open_for_read(&self.non_meta_storage, hash, fio::OpenFlags::empty())
                .map_err(ReadFileError::Open)?;
            return fuchsia_fs::file::read(&blob).await.map_err(ReadFileError::Read);
        }

        if let Some(location) = self.meta_files.get(path) {
            let mut file = fuchsia_fs::file::AsyncFile::from_proxy(Clone::clone(&self.meta_far));
            let mut contents = vec![0; crate::u64_to_usize_safe(location.length)];
            let () = file
                .read_at_exact(location.offset, contents.as_mut_slice())
                .await
                .map_err(ReadFileError::PartialBlobRead)?;
            return Ok(contents);
        }

        Err(ReadFileError::NoFileAtPath { path: path.to_string() })
    }

    /// Returns `true` iff there is a file at `path`, an object relative path expression.
    /// https://fuchsia.dev/fuchsia-src/concepts/process/namespaces?hl=en#object_relative_path_expressions
    pub fn has_file(&self, path: &str) -> bool {
        self.non_meta_files.contains_key(path) || self.meta_files.contains_key(path)
    }

    /// Returns the hash of the package.
    pub fn hash(&self) -> &fuchsia_hash::Hash {
        &self.hash
    }

    /// Returns an iterator of the hashes of files stored externally to the package meta.far.
    /// May return duplicates.
    pub fn external_file_hashes(&self) -> impl ExactSizeIterator<Item = &fuchsia_hash::Hash> {
        self.non_meta_files.values()
    }

    /// Returns the path of the package as indicated by the "meta/package" file.
    pub async fn path(&self) -> Result<fuchsia_pkg::PackagePath, PathError> {
        Ok(fuchsia_pkg::MetaPackage::deserialize(&self.read_file("meta/package").await?[..])?
            .into_path())
    }

    /// Returns the subpackages of the package.
    pub async fn subpackages(&self) -> Result<fuchsia_pkg::MetaSubpackages, SubpackagesError> {
        let contents = match self.read_file(fuchsia_pkg::MetaSubpackages::PATH).await {
            Ok(contents) => contents,
            Err(ReadFileError::NoFileAtPath { .. }) => {
                return Ok(fuchsia_pkg::MetaSubpackages::default())
            }
            Err(e) => Err(e)?,
        };

        Ok(fuchsia_pkg::MetaSubpackages::deserialize(&*contents)?)
    }

    pub(crate) async fn meta_far_vmo(&self) -> Result<&zx::Vmo, anyhow::Error> {
        self.meta_far_vmo
            .get_or_try_init(async {
                let vmo = self
                    .meta_far
                    .get_backing_memory(fio::VmoFlags::READ)
                    .await
                    .context("meta.far .get_backing_memory() fidl error")?
                    .map_err(zx::Status::from_raw)
                    .context("meta.far .get_backing_memory protocol error")?;
                Ok(vmo)
            })
            .await
    }
}

#[derive(thiserror::Error, Debug)]
pub enum ReadFileError {
    #[error("opening blob")]
    Open(#[source] fuchsia_fs::node::OpenError),

    #[error("reading blob")]
    Read(#[source] fuchsia_fs::file::ReadError),

    #[error("reading part of a blob")]
    PartialBlobRead(#[source] std::io::Error),

    #[error("no file exists at path: {path:?}")]
    NoFileAtPath { path: String },
}

#[derive(thiserror::Error, Debug)]
pub enum SubpackagesError {
    #[error("reading manifest")]
    Read(#[from] ReadFileError),

    #[error("parsing manifest")]
    Parse(#[from] fuchsia_pkg::MetaSubpackagesError),
}

#[derive(thiserror::Error, Debug)]
pub enum PathError {
    #[error("reading meta/package")]
    Read(#[from] ReadFileError),

    #[error("parsing meta/package")]
    Parse(#[from] fuchsia_pkg::MetaPackageError),
}

impl<S: crate::NonMetaStorage> vfs::directory::entry::DirectoryEntry for RootDir<S> {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        path: VfsPath,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        let flags = flags & !fio::OpenFlags::POSIX_WRITABLE;

        if path.is_empty() {
            if flags.intersects(
                fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::CREATE
                    | fio::OpenFlags::CREATE_IF_ABSENT
                    | fio::OpenFlags::TRUNCATE
                    | fio::OpenFlags::APPEND,
            ) {
                let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
                return;
            }

            let () = ImmutableConnection::create_connection(scope, self, flags, server_end);
            return;
        }

        // vfs::path::Path::as_str() is an object relative path expression [1], except that it may:
        //   1. have a trailing "/"
        //   2. be exactly "."
        //   3. be longer than 4,095 bytes
        // The .is_empty() check above rules out "." and the following line removes the possible
        // trailing "/".
        // [1] https://fuchsia.dev/fuchsia-src/concepts/process/namespaces?hl=en#object_relative_path_expressions
        let canonical_path = path.as_ref().strip_suffix("/").unwrap_or_else(|| path.as_ref());

        if canonical_path == "meta" {
            // This branch is done here instead of in MetaAsDir so that Clone'ing MetaAsDir yields
            // MetaAsDir. See the MetaAsDir::open impl for more.
            if open_meta_as_file(flags, mode) {
                let () = Arc::new(MetaAsFile::new(self)).open(
                    scope,
                    flags,
                    mode,
                    VfsPath::dot(),
                    server_end,
                );
            } else {
                let () = Arc::new(MetaAsDir::new(self)).open(
                    scope,
                    flags,
                    mode,
                    VfsPath::dot(),
                    server_end,
                );
            }
            return;
        }

        if canonical_path.starts_with("meta/") {
            if let Some(meta_file) = self.meta_files.get(canonical_path).copied() {
                let () = Arc::new(MetaFile::new(self, meta_file)).open(
                    scope,
                    flags,
                    mode,
                    VfsPath::dot(),
                    server_end,
                );
                return;
            }

            let subdir_prefix = canonical_path.to_string() + "/";
            for k in self.meta_files.keys() {
                if k.starts_with(&subdir_prefix) {
                    let () = Arc::new(MetaSubdir::new(self, subdir_prefix)).open(
                        scope,
                        flags,
                        mode,
                        VfsPath::dot(),
                        server_end,
                    );
                    return;
                }
            }

            let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
            return;
        }

        if let Some(blob) = self.non_meta_files.get(canonical_path) {
            let () =
                self.non_meta_storage.open(blob, flags, mode, server_end).unwrap_or_else(|e| {
                    error!("Error forwarding content blob open to blobfs: {:#}", anyhow::anyhow!(e))
                });
            return;
        }

        let subdir_prefix = canonical_path.to_string() + "/";
        for k in self.non_meta_files.keys() {
            if k.starts_with(&subdir_prefix) {
                let () = Arc::new(NonMetaSubdir::new(self, subdir_prefix)).open(
                    scope,
                    flags,
                    mode,
                    VfsPath::dot(),
                    server_end,
                );
                return;
            }
        }

        let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
    }
}

#[async_trait]
impl<S: crate::NonMetaStorage> vfs::directory::entry_container::Directory for RootDir<S> {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
        zx::Status,
    > {
        vfs::directory::read_dirents::read_dirents(
            // Add "meta/placeholder" file so the "meta" dir is included in the results
            &crate::get_dir_children(
                self.non_meta_files.keys().map(|s| s.as_str()).chain(["meta/placeholder"]),
                "",
            ),
            pos,
            sink,
        )
        .await
    }

    fn register_watcher(
        self: Arc<Self>,
        _: ExecutionScope,
        _: fio::WatchMask,
        _: vfs::directory::entry_container::DirectoryWatcher,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so no need to do anything here.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_DIRECTORY
                | vfs::common::rights_to_posix_mode_bits(
                    true, // read
                    true, // write
                    true, // execute
                ),
            id: 1,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }
}

// Behavior copied from pkgfs.
fn open_meta_as_file(flags: fio::OpenFlags, mode: u32) -> bool {
    let mode_type = mode & fio::MODE_TYPE_MASK;
    let open_as_file = mode_type == fio::MODE_TYPE_FILE;
    let open_as_dir = mode_type == fio::MODE_TYPE_DIRECTORY
        || flags.intersects(fio::OpenFlags::DIRECTORY | fio::OpenFlags::NODE_REFERENCE);
    open_as_file || !open_as_dir
}

// Open a non-meta file by hash with flags of `OPEN_RIGHT_READABLE | additional_flags` and return
// the proxy.
fn open_for_read(
    non_meta_storage: &impl crate::NonMetaStorage,
    blob: &fuchsia_hash::Hash,
    additional_flags: fio::OpenFlags,
) -> Result<fio::FileProxy, fuchsia_fs::node::OpenError> {
    let (file, server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>()
        .map_err(fuchsia_fs::node::OpenError::CreateProxy)?;
    let () = non_meta_storage.open(
        blob,
        fio::OpenFlags::RIGHT_READABLE | additional_flags,
        0,
        server_end.into_channel().into(),
    )?;
    Ok(file)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::anyhow,
        assert_matches::assert_matches,
        fidl::endpoints::{create_proxy, Proxy as _},
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        pretty_assertions::assert_eq,
        std::convert::TryInto as _,
        vfs::directory::{entry::DirectoryEntry, entry_container::Directory},
    };

    struct TestEnv {
        _blobfs_fake: FakeBlobfs,
    }

    impl TestEnv {
        async fn with_subpackages_content(
            subpackages_content: Option<&[u8]>,
        ) -> (Self, RootDir<blobfs::Client>) {
            let mut pkg = PackageBuilder::new("base-package-0")
                .add_resource_at("resource", "blob-contents".as_bytes())
                .add_resource_at("dir/file", "bloblob".as_bytes())
                .add_resource_at("meta/file", "meta-contents0".as_bytes())
                .add_resource_at("meta/dir/file", "meta-contents1".as_bytes());
            if let Some(subpackages_content) = subpackages_content {
                pkg = pkg.add_resource_at(fuchsia_pkg::MetaSubpackages::PATH, subpackages_content);
            }
            let pkg = pkg.build().await.unwrap();
            let (metafar_blob, content_blobs) = pkg.contents();
            let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
            blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
            for (hash, bytes) in content_blobs {
                blobfs_fake.add_blob(hash, bytes);
            }

            let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
            (Self { _blobfs_fake: blobfs_fake }, root_dir)
        }

        async fn new() -> (Self, RootDir<blobfs::Client>) {
            Self::with_subpackages_content(None).await
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_missing_meta_far_error() {
        let (_blobfs_fake, blobfs_client) = FakeBlobfs::new();
        assert_matches!(
            RootDir::new(blobfs_client, [0; 32].into()).await,
            Err(Error::MissingMetaFar)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_rejects_invalid_utf8() {
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        let mut meta_far = vec![];
        let () = fuchsia_archive::write(
            &mut meta_far,
            std::collections::BTreeMap::from_iter([(
                b"\xff",
                (0, Box::new("".as_bytes()) as Box<dyn std::io::Read>),
            )]),
        )
        .unwrap();
        let hash = fuchsia_merkle::MerkleTree::from_reader(&*meta_far).unwrap().root();
        let () = blobfs_fake.add_blob(hash, meta_far);

        assert_matches!(
            RootDir::new(blobfs_client, hash).await,
            Err(Error::NonUtf8MetaEntry{path, ..})
                if path == vec![255]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_initializes_maps() {
        let (_env, root_dir) = TestEnv::new().await;

        let meta_files = HashMap::from([
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 148 }),
            (String::from("meta/package"), MetaFileLocation { offset: 20480, length: 39 }),
            (String::from("meta/file"), MetaFileLocation { offset: 12288, length: 14 }),
            (String::from("meta/dir/file"), MetaFileLocation { offset: 8192, length: 14 }),
            (
                String::from("meta/fuchsia.abi/abi-revision"),
                MetaFileLocation { offset: 16384, length: 8 },
            ),
        ]);
        assert_eq!(root_dir.meta_files, meta_files);

        let non_meta_files: HashMap<String, fuchsia_hash::Hash> = [
            (
                String::from("resource"),
                "bd905f783ceae4c5ba8319703d7505ab363733c2db04c52c8405603a02922b15"
                    .parse::<fuchsia_hash::Hash>()
                    .unwrap(),
            ),
            (
                String::from("dir/file"),
                "5f615dd575994fcbcc174974311d59de258d93cd523d5cb51f0e139b53c33201"
                    .parse::<fuchsia_hash::Hash>()
                    .unwrap(),
            ),
        ]
        .iter()
        .cloned()
        .collect();
        assert_eq!(root_dir.non_meta_files, non_meta_files);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn rejects_meta_file_collisions() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("meta/dir/file", "meta-contents0".as_bytes())
            .add_resource_at_ignore_path_collisions("meta/dir", "meta-contents1".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, _) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);

        match RootDir::new(blobfs_client, metafar_blob.merkle).await {
            Ok(_) => panic!("this should not be reached!"),
            Err(Error::FileDirectoryCollision { path }) => {
                assert_eq!(path, "meta/dir".to_string());
            }
            Err(e) => panic!("Expected collision error, receieved {:?}", e),
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_file() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(root_dir.read_file("resource").await.unwrap().as_slice(), b"blob-contents");
        assert_eq!(root_dir.read_file("meta/file").await.unwrap().as_slice(), b"meta-contents0");
        assert_matches!(
            root_dir.read_file("missing").await.unwrap_err(),
            ReadFileError::NoFileAtPath{path} if path == "missing"
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn has_file() {
        let (_env, root_dir) = TestEnv::new().await;

        assert!(root_dir.has_file("resource"));
        assert!(root_dir.has_file("meta/file"));
        assert_eq!(root_dir.has_file("missing"), false);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn external_file_hashes() {
        let (_env, root_dir) = TestEnv::new().await;

        let mut actual = root_dir.external_file_hashes().copied().collect::<Vec<_>>();
        actual.sort();
        assert_eq!(
            actual,
            vec![
                "5f615dd575994fcbcc174974311d59de258d93cd523d5cb51f0e139b53c33201".parse().unwrap(),
                "bd905f783ceae4c5ba8319703d7505ab363733c2db04c52c8405603a02922b15".parse().unwrap()
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn path() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(
            root_dir.path().await.unwrap(),
            "base-package-0/0".parse::<fuchsia_pkg::PackagePath>().unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn subpackages_present() {
        let subpackages = fuchsia_pkg::MetaSubpackages::from_iter([(
            fuchsia_url::RelativePackageUrl::parse("subpackage-name").unwrap(),
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap(),
        )]);
        let mut subpackages_bytes = vec![];
        let () = subpackages.serialize(&mut subpackages_bytes).unwrap();
        let (_env, root_dir) = TestEnv::with_subpackages_content(Some(&*subpackages_bytes)).await;

        assert_eq!(root_dir.subpackages().await.unwrap(), subpackages);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn subpackages_absent() {
        let (_env, root_dir) = TestEnv::with_subpackages_content(None).await;

        assert_eq!(root_dir.subpackages().await.unwrap(), fuchsia_pkg::MetaSubpackages::default());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn subpackages_error() {
        let (_env, root_dir) = TestEnv::with_subpackages_content(Some(b"invalid-json")).await;

        assert_matches!(root_dir.subpackages().await, Err(SubpackagesError::Parse(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_attrs() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(
            Directory::get_attrs(&root_dir).await.unwrap(),
            fio::NodeAttributes {
                mode: fio::MODE_TYPE_DIRECTORY | 0o700,
                id: 1,
                content_size: 0,
                storage_size: 0,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_entry_info() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&root_dir),
            EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_read_dirents() {
        let (_env, root_dir) = TestEnv::new().await;

        let (pos, sealed) = Directory::read_dirents(
            &root_dir,
            &TraversalPosition::Start,
            Box::new(crate::tests::FakeSink::new(4)),
        )
        .await
        .expect("read_dirents failed");

        assert_eq!(
            crate::tests::FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                ("dir".to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                ("meta".to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                ("resource".to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File))
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_register_watcher_not_supported() {
        let (_env, root_dir) = TestEnv::new().await;

        let (_client, server) = fidl::endpoints::create_endpoints().unwrap();

        assert_eq!(
            Directory::register_watcher(
                Arc::new(root_dir),
                ExecutionScope::new(),
                fio::WatchMask::empty(),
                server.try_into().unwrap(),
            ),
            Err(zx::Status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_close() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(Directory::close(&root_dir), Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_unsets_posix_writable() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        let () = crate::verify_open_adjusts_flags(
            &(root_dir as Arc<dyn DirectoryEntry>),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::POSIX_WRITABLE,
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_invalid_flags() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for forbidden_flag in [
            fio::OpenFlags::RIGHT_WRITABLE,
            fio::OpenFlags::CREATE,
            fio::OpenFlags::CREATE_IF_ABSENT,
            fio::OpenFlags::TRUNCATE,
            fio::OpenFlags::APPEND,
        ] {
            let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                fio::OpenFlags::DESCRIBE | forbidden_flag,
                0,
                VfsPath::dot(),
                server_end.into_channel().into(),
            );

            assert_matches!(
                proxy.take_event_stream().next().await,
                Some(Ok(fio::DirectoryEvent::OnOpen_{ s, info: None}))
                    if s == zx::Status::NOT_SUPPORTED.into_raw()
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_self() {
        let (_env, root_dir) = TestEnv::new().await;
        let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

        DirectoryEntry::open(
            Arc::new(root_dir),
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
            vec![
                fuchsia_fs::directory::DirEntry {
                    name: "dir".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::Directory
                },
                fuchsia_fs::directory::DirEntry {
                    name: "meta".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::Directory
                },
                fuchsia_fs::directory::DirEntry {
                    name: "resource".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::File
                }
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_non_meta_file() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["resource", "resource/"] {
            let (proxy, server_end) = create_proxy().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end,
            );

            assert_eq!(
                fuchsia_fs::file::read(&fio::FileProxy::from_channel(
                    proxy.into_channel().unwrap()
                ))
                .await
                .unwrap(),
                b"blob-contents".to_vec()
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_meta_as_file() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["meta", "meta/"] {
            let (proxy, server_end) = create_proxy::<fio::FileMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_FILE,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                fuchsia_fs::file::read(&proxy).await.unwrap(),
                root_dir.hash.to_string().as_bytes()
            );

            // Cloning meta_as_file yields meta_as_file
            let (cloned_proxy, server_end) = create_proxy::<fio::FileMarker>().unwrap();
            let () = proxy
                .clone(fio::OpenFlags::RIGHT_READABLE, server_end.into_channel().into())
                .unwrap();
            assert_eq!(
                fuchsia_fs::file::read(&cloned_proxy).await.unwrap(),
                root_dir.hash.to_string().as_bytes()
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_meta_as_dir() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["meta", "meta/"] {
            let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_DIRECTORY,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
                vec![
                    fuchsia_fs::directory::DirEntry {
                        name: "contents".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::File
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "dir".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::Directory
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "file".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::File
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "fuchsia.abi".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::Directory
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "package".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::File
                    },
                ]
            );

            // Cloning meta_as_dir yields meta_as_dir
            let (cloned_proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
            let () =
                proxy.clone(fio::OpenFlags::empty(), server_end.into_channel().into()).unwrap();
            assert_eq!(
                fuchsia_fs::directory::readdir(&cloned_proxy).await.unwrap(),
                vec![
                    fuchsia_fs::directory::DirEntry {
                        name: "contents".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::File
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "dir".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::Directory
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "file".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::File
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "fuchsia.abi".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::Directory
                    },
                    fuchsia_fs::directory::DirEntry {
                        name: "package".to_string(),
                        kind: fuchsia_fs::directory::DirentKind::File
                    },
                ]
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_meta_file() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["meta/file", "meta/file/"] {
            let (proxy, server_end) = create_proxy::<fio::FileMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(fuchsia_fs::file::read(&proxy).await.unwrap(), b"meta-contents0".to_vec());
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_meta_subdir() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["meta/dir", "meta/dir/"] {
            let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
                vec![fuchsia_fs::directory::DirEntry {
                    name: "file".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::File
                }]
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_non_meta_subdir() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["dir", "dir/"] {
            let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                fio::OpenFlags::RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
                vec![fuchsia_fs::directory::DirEntry {
                    name: "file".to_string(),
                    kind: fuchsia_fs::directory::DirentKind::File
                }]
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn meta_far_vmo() {
        let (_env, root_dir) = TestEnv::new().await;

        // VMO is readable
        let vmo = root_dir.meta_far_vmo().await.unwrap();
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, fuchsia_archive::MAGIC_INDEX_VALUE);

        // Accessing the VMO caches it
        let _: &zx::Vmo = root_dir
            .meta_far_vmo
            .get_or_try_init(futures::future::err(anyhow!("vmo should be cached")))
            .await
            .unwrap();

        // Accessing the VMO through the cached path works
        let vmo = root_dir.meta_far_vmo().await.unwrap();
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, fuchsia_archive::MAGIC_INDEX_VALUE);
    }

    fn arb_open_flags() -> impl proptest::strategy::Strategy<Value = fio::OpenFlags> {
        use proptest::strategy::Strategy as _;

        proptest::arbitrary::any::<u32>().prop_map(fio::OpenFlags::from_bits_truncate)
    }

    proptest::proptest! {
        #![proptest_config(proptest::test_runner::Config {
            failure_persistence:
                Some(Box::new(proptest::test_runner::FileFailurePersistence::Off)),
            ..Default::default()
        })]
        #[test]
        fn open_meta_as_file_file_first_priority(flags in arb_open_flags(), mode: u32) {
            let mode_with_file = (mode & !fio::MODE_TYPE_MASK) | fio::MODE_TYPE_FILE;
            proptest::prop_assert!(open_meta_as_file(flags, mode_with_file));
        }

        #[test]
        fn open_meta_as_file_dir_second_priority(flags in arb_open_flags(), mode: u32) {
            let mode_with_dir = (mode & !fio::MODE_TYPE_MASK) | fio::MODE_TYPE_DIRECTORY;
            proptest::prop_assert!(!open_meta_as_file(flags, mode_with_dir));

            let mode_without_file = if mode & fio::MODE_TYPE_MASK == fio::MODE_TYPE_FILE {
                mode & !fio::MODE_TYPE_FILE
            } else {
                mode
            };
            proptest::prop_assert!(!open_meta_as_file(flags | fio::OpenFlags::DIRECTORY, mode_without_file));
            proptest::prop_assert!(!open_meta_as_file(flags | fio::OpenFlags::NODE_REFERENCE, mode_without_file));
        }

        #[test]
        fn open_meta_as_file_file_fallback(flags in arb_open_flags(), mode: u32) {
            let mode = mode & !(fio::MODE_TYPE_FILE | fio::MODE_TYPE_DIRECTORY);
            let flags = flags & !(fio::OpenFlags::DIRECTORY | fio::OpenFlags::NODE_REFERENCE);
            proptest::prop_assert!(open_meta_as_file(flags, mode));
        }
    }
}
