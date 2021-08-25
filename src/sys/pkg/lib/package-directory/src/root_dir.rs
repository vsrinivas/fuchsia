// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        meta::Meta,
        meta_file::{MetaFile, MetaFileLocation},
        meta_subdir::MetaSubdir,
        non_meta_subdir::NonMetaSubdir,
        Error,
    },
    anyhow::{anyhow, Context as _},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileProxy, NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN,
        MODE_TYPE_DIRECTORY, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_WRITABLE, VMO_FLAG_READ,
    },
    fuchsia_archive::AsyncReader,
    fuchsia_pkg::MetaContents,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    once_cell::sync::OnceCell,
    std::{collections::HashMap, sync::Arc},
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            entry::EntryInfo,
            entry_container::AsyncGetEntry,
            immutable::connection::io1::{ImmutableConnection, ImmutableConnectionClient},
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

pub(crate) struct RootDir {
    pub(crate) blobfs: blobfs::Client,
    pub(crate) hash: fuchsia_hash::Hash,
    pub(crate) meta_far: FileProxy,
    // The keys are object relative path expressions.
    pub(crate) meta_files: HashMap<String, MetaFileLocation>,
    // The keys are object relative path expressions.
    pub(crate) non_meta_files: HashMap<String, fuchsia_hash::Hash>,
    meta_far_vmo: OnceCell<zx::Vmo>,
}

impl RootDir {
    pub(crate) async fn new(
        blobfs: blobfs::Client,
        hash: fuchsia_hash::Hash,
    ) -> Result<Self, Error> {
        let meta_far = blobfs.open_blob_for_read_no_describe(&hash).map_err(Error::OpenMetaFar)?;

        let reader = io_util::file::AsyncFile::from_proxy(Clone::clone(&meta_far));
        let mut async_reader = AsyncReader::new(reader).await.map_err(Error::ArchiveReader)?;
        let reader_list = async_reader.list();

        let mut meta_files = HashMap::with_capacity(reader_list.size_hint().0);

        for entry in reader_list {
            if entry.path().starts_with("meta/") {
                meta_files.insert(
                    String::from(entry.path()),
                    MetaFileLocation { offset: entry.offset(), length: entry.length() },
                );
            }
        }

        let meta_contents_bytes =
            async_reader.read_file("meta/contents").await.map_err(Error::ReadMetaContents)?;

        let non_meta_files = MetaContents::deserialize(&meta_contents_bytes[..])
            .map_err(Error::DeserializeMetaContents)?
            .into_contents();

        let meta_far_vmo = OnceCell::new();

        Ok(RootDir { blobfs, hash, meta_far, meta_files, non_meta_files, meta_far_vmo })
    }

    pub(crate) async fn meta_far_vmo(&self) -> Result<&zx::Vmo, anyhow::Error> {
        Ok(if let Some(vmo) = self.meta_far_vmo.get() {
            vmo
        } else {
            let (status, buffer) = self
                .meta_far
                .get_buffer(VMO_FLAG_READ)
                .await
                .context("meta.far .get_buffer() fidl error")?;
            let () = zx::Status::ok(status).context("meta.far .get_buffer protocol error")?;
            if let Some(buffer) = buffer {
                self.meta_far_vmo.get_or_init(|| buffer.vmo)
            } else {
                anyhow::bail!("meta.far get_buffer call succeeded but returned no VMO");
            }
        })
    }
}

impl vfs::directory::entry::DirectoryEntry for RootDir {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: VfsPath,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if path.is_empty() {
            if flags
                & (OPEN_RIGHT_WRITABLE
                    | OPEN_RIGHT_ADMIN
                    | OPEN_FLAG_CREATE
                    | OPEN_FLAG_CREATE_IF_ABSENT
                    | OPEN_FLAG_TRUNCATE
                    | OPEN_FLAG_APPEND)
                != 0
            {
                let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
                return;
            }

            let () = ImmutableConnection::create_connection(
                scope,
                OpenDirectory::new(self as Arc<dyn ImmutableConnectionClient>),
                flags,
                server_end,
            );
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
            let () = Arc::new(Meta::new(self)).open(scope, flags, mode, VfsPath::dot(), server_end);
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
            let () = self.blobfs.forward_open(blob, flags, mode, server_end).unwrap_or_else(|e| {
                fx_log_err!("Error forwarding content blob open to blobfs: {:#}", anyhow!(e))
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
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl vfs::directory::entry_container::Directory for RootDir {
    // Used for linking which is not supported.
    fn get_entry<'a>(self: Arc<Self>, _: &'a str) -> AsyncGetEntry<'a> {
        AsyncGetEntry::from(zx::Status::NOT_SUPPORTED)
    }

    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
        zx::Status,
    > {
        crate::read_dirents(
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
        _: u32,
        _: fidl::AsyncChannel,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so no need to do anything here.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<NodeAttributes, zx::Status> {
        Ok(NodeAttributes {
            mode: MODE_TYPE_DIRECTORY
                | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ false),
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy, Proxy as _},
        fidl::{AsyncChannel, Channel},
        fidl_fuchsia_io::{
            DirectoryMarker, FileMarker, DIRENT_TYPE_FILE, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE,
            OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
        },
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        futures::stream::StreamExt as _,
        matches::assert_matches,
        vfs::directory::{entry::DirectoryEntry, entry_container::Directory},
    };

    struct TestEnv {
        _blobfs_fake: FakeBlobfs,
    }

    impl TestEnv {
        async fn new() -> (Self, RootDir) {
            let pkg = PackageBuilder::new("base-package-0")
                .add_resource_at("resource", "blob-contents".as_bytes())
                .add_resource_at("dir/file", "bloblob".as_bytes())
                .add_resource_at("meta/file", "meta-contents0".as_bytes())
                .add_resource_at("meta/dir/file", "meta-contents1".as_bytes())
                .build()
                .await
                .unwrap();
            let (metafar_blob, content_blobs) = pkg.contents();
            let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
            blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
            for content in content_blobs {
                blobfs_fake.add_blob(content.merkle, content.contents);
            }

            let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
            (Self { _blobfs_fake: blobfs_fake }, root_dir)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn new_initializes_maps() {
        let (_env, root_dir) = TestEnv::new().await;

        let meta_files: HashMap<String, MetaFileLocation> = [
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 148 }),
            (String::from("meta/package"), MetaFileLocation { offset: 16384, length: 39 }),
            (String::from("meta/file"), MetaFileLocation { offset: 12288, length: 14 }),
            (String::from("meta/dir/file"), MetaFileLocation { offset: 8192, length: 14 }),
        ]
        .iter()
        .cloned()
        .collect();
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
    async fn directory_entry_can_hardlink() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(DirectoryEntry::can_hardlink(&root_dir), false);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_attrs() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(
            Directory::get_attrs(&root_dir).await.unwrap(),
            NodeAttributes {
                mode: MODE_TYPE_DIRECTORY
                    | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ false),
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
            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_entry() {
        let (_env, root_dir) = TestEnv::new().await;

        match Directory::get_entry(Arc::new(root_dir), "") {
            AsyncGetEntry::Future(_) => panic!("RootDir::get_entry should immediately fail"),
            AsyncGetEntry::Immediate(res) => {
                assert_eq!(res.err().unwrap(), zx::Status::NOT_SUPPORTED)
            }
        }
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
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("dir".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("meta".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("resource".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_register_watcher_not_supported() {
        let (_env, root_dir) = TestEnv::new().await;

        assert_eq!(
            Directory::register_watcher(
                Arc::new(root_dir),
                ExecutionScope::new(),
                0,
                AsyncChannel::from_channel(Channel::create().unwrap().0).unwrap()
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
    async fn directory_entry_open_rejects_invalid_flags() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for forbidden_flag in [
            OPEN_RIGHT_WRITABLE,
            OPEN_RIGHT_ADMIN,
            OPEN_FLAG_CREATE,
            OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_FLAG_TRUNCATE,
            OPEN_FLAG_APPEND,
        ] {
            let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                OPEN_FLAG_DESCRIBE | forbidden_flag,
                0,
                VfsPath::dot(),
                server_end.into_channel().into(),
            );

            assert_matches!(
                proxy.take_event_stream().next().await,
                Some(Ok(fidl_fuchsia_io::DirectoryEvent::OnOpen_{ s, info: None}))
                    if s == zx::Status::NOT_SUPPORTED.into_raw()
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_self() {
        let (_env, root_dir) = TestEnv::new().await;
        let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();

        DirectoryEntry::open(
            Arc::new(root_dir),
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![
                files_async::DirEntry {
                    name: "dir".to_string(),
                    kind: files_async::DirentKind::Directory
                },
                files_async::DirEntry {
                    name: "meta".to_string(),
                    kind: files_async::DirentKind::Directory
                },
                files_async::DirEntry {
                    name: "resource".to_string(),
                    kind: files_async::DirentKind::File
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
                OPEN_RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end,
            );

            assert_eq!(
                io_util::file::read(&FileProxy::from_channel(proxy.into_channel().unwrap()))
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
            let (proxy, server_end) = create_proxy::<FileMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                OPEN_RIGHT_READABLE,
                MODE_TYPE_FILE,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                io_util::file::read(&proxy).await.unwrap(),
                root_dir.hash.to_string().as_bytes()
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_meta_as_dir() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["meta", "meta/"] {
            let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                OPEN_RIGHT_READABLE,
                MODE_TYPE_DIRECTORY,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                files_async::readdir(&proxy).await.unwrap(),
                vec![
                    files_async::DirEntry {
                        name: "contents".to_string(),
                        kind: files_async::DirentKind::File
                    },
                    files_async::DirEntry {
                        name: "dir".to_string(),
                        kind: files_async::DirentKind::Directory
                    },
                    files_async::DirEntry {
                        name: "file".to_string(),
                        kind: files_async::DirentKind::File
                    },
                    files_async::DirEntry {
                        name: "package".to_string(),
                        kind: files_async::DirentKind::File
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
            let (proxy, server_end) = create_proxy::<FileMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                OPEN_RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(io_util::file::read(&proxy).await.unwrap(), b"meta-contents0".to_vec());
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_meta_subdir() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["meta/dir", "meta/dir/"] {
            let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                OPEN_RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                files_async::readdir(&proxy).await.unwrap(),
                vec![files_async::DirEntry {
                    name: "file".to_string(),
                    kind: files_async::DirentKind::File
                }]
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_non_meta_subdir() {
        let (_env, root_dir) = TestEnv::new().await;
        let root_dir = Arc::new(root_dir);

        for path in ["dir", "dir/"] {
            let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();

            DirectoryEntry::open(
                Arc::clone(&root_dir),
                ExecutionScope::new(),
                OPEN_RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(
                files_async::readdir(&proxy).await.unwrap(),
                vec![files_async::DirEntry {
                    name: "file".to_string(),
                    kind: files_async::DirentKind::File
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
        assert!(root_dir.meta_far_vmo.get().is_some());

        // Accessing the VMO through the cached path works
        let vmo = root_dir.meta_far_vmo().await.unwrap();
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, fuchsia_archive::MAGIC_INDEX_VALUE);
    }
}
