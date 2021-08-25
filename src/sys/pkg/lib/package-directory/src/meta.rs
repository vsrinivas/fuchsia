// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        meta_file::MetaFile, meta_subdir::MetaSubdir, root_dir::RootDir, u64_to_usize_safe,
        usize_to_u64_safe,
    },
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MODE_TYPE_DIRECTORY,
        MODE_TYPE_FILE, MODE_TYPE_MASK, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    std::sync::Arc,
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            entry::EntryInfo,
            entry_container::AsyncGetEntry,
            immutable::connection::io1::{
                ImmutableConnection as DirImmutableConnection,
                ImmutableConnectionClient as DirImmutableConnectionClient,
            },
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

pub(crate) struct Meta {
    root_dir: Arc<RootDir>,
}

impl Meta {
    pub(crate) fn new(root_dir: Arc<RootDir>) -> Self {
        Meta { root_dir }
    }

    fn file_size(&self) -> u64 {
        crate::usize_to_u64_safe(self.root_dir.hash.to_string().as_bytes().len())
    }
}

// Behavior copied from pkgfs.
fn open_meta_as_file(flags: u32, mode: u32) -> bool {
    let mode_type = mode & MODE_TYPE_MASK;
    let open_as_file = mode_type == MODE_TYPE_FILE;
    let open_as_dir = mode_type == MODE_TYPE_DIRECTORY
        || flags & (OPEN_FLAG_DIRECTORY | OPEN_FLAG_NODE_REFERENCE) != 0;
    open_as_file || !open_as_dir
}

impl vfs::directory::entry::DirectoryEntry for Meta {
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
                    | OPEN_RIGHT_EXECUTABLE
                    | OPEN_FLAG_CREATE
                    | OPEN_FLAG_CREATE_IF_ABSENT
                    | OPEN_FLAG_TRUNCATE
                    | OPEN_FLAG_APPEND)
                != 0
            {
                let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
                return;
            }

            if open_meta_as_file(flags, mode) {
                let () = vfs::file::connection::io1::FileConnection::<Self>::create_connection(
                    scope.clone(),
                    vfs::file::connection::util::OpenFile::new(self, scope),
                    flags,
                    server_end,
                    // readable/writable/executable do not override the flags, they tell the
                    // FileConnection if it's ever valid to open the file with that right.
                    true,  /*=readable*/
                    false, /*=writable*/
                    false, /*=executable*/
                );
            } else {
                let () = DirImmutableConnection::create_connection(
                    scope,
                    OpenDirectory::new(self as Arc<dyn DirImmutableConnectionClient>),
                    flags,
                    server_end,
                );
            }
            return;
        }

        // <path as vfs::path::Path>::as_str() is an object relative path expression [1], except
        // that it may:
        //   1. have a trailing "/"
        //   2. be exactly "."
        //   3. be longer than 4,095 bytes
        // The .is_empty() check above rules out "." and the following line removes the possible
        // trailing "/".
        // [1] https://fuchsia.dev/fuchsia-src/concepts/process/namespaces?hl=en#object_relative_path_expressions
        let file_path =
            format!("meta/{}", path.as_ref().strip_suffix("/").unwrap_or_else(|| path.as_ref()));

        if let Some(location) = self.root_dir.meta_files.get(&file_path).copied() {
            let () = Arc::new(MetaFile::new(Arc::clone(&self.root_dir), location)).open(
                scope,
                flags,
                mode,
                VfsPath::dot(),
                server_end,
            );
            return;
        }

        let directory_path = file_path + "/";
        for k in self.root_dir.meta_files.keys() {
            if k.starts_with(&directory_path) {
                let () = Arc::new(MetaSubdir::new(Arc::clone(&self.root_dir), directory_path))
                    .open(scope, flags, mode, VfsPath::dot(), server_end);
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
impl vfs::directory::entry_container::Directory for Meta {
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
            &crate::get_dir_children(self.root_dir.meta_files.keys().map(|s| s.as_str()), "meta/"),
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
            content_size: usize_to_u64_safe(self.root_dir.meta_files.len()),
            storage_size: usize_to_u64_safe(self.root_dir.meta_files.len()),
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }
}

#[async_trait]
impl vfs::file::File for Meta {
    async fn open(&self, _flags: u32) -> Result<(), zx::Status> {
        Ok(())
    }

    async fn read_at(&self, offset: u64, buffer: &mut [u8]) -> Result<u64, zx::Status> {
        let contents = self.root_dir.hash.to_string();
        let offset = std::cmp::min(u64_to_usize_safe(offset), contents.len());
        let count = std::cmp::min(buffer.len(), contents.len() - offset);
        let () = buffer[..count].copy_from_slice(&contents.as_bytes()[offset..offset + count]);
        Ok(usize_to_u64_safe(count))
    }

    async fn write_at(&self, _offset: u64, _content: &[u8]) -> Result<u64, zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn append(&self, _content: &[u8]) -> Result<(u64, u64), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn truncate(&self, _length: u64) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn get_buffer(
        &self,
        _mode: vfs::file::SharingMode,
        _flags: u32,
    ) -> Result<Option<fidl_fuchsia_mem::Buffer>, zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn get_size(&self) -> Result<u64, zx::Status> {
        Ok(self.file_size())
    }

    async fn get_attrs(&self) -> Result<NodeAttributes, zx::Status> {
        Ok(NodeAttributes {
            mode: MODE_TYPE_FILE
                | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ false),
            id: 1,
            content_size: self.file_size(),
            storage_size: self.file_size(),
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    async fn set_attrs(&self, _flags: u32, _attrs: NodeAttributes) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }

    async fn sync(&self) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::{AsyncChannel, Channel},
        fidl_fuchsia_io::{
            DirectoryMarker, FileMarker, DIRENT_TYPE_FILE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
            VMO_FLAG_READ,
        },
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        futures::stream::StreamExt as _,
        matches::assert_matches,
        proptest::{prelude::ProptestConfig, prop_assert, proptest},
        std::convert::TryInto as _,
        vfs::{
            directory::{entry::DirectoryEntry, entry_container::Directory},
            file::File,
        },
    };

    proptest! {
        #![proptest_config(ProptestConfig {
            failure_persistence:
                Some(Box::new(proptest::test_runner::FileFailurePersistence::Off)),
            ..ProptestConfig::default()
        })]
        #[test]
        fn open_meta_as_file_file_first_priority(flags: u32, mode: u32) {
            let mode_with_file = (mode & !MODE_TYPE_MASK) | MODE_TYPE_FILE;
            prop_assert!(open_meta_as_file(flags, mode_with_file));
        }

        #[test]
        fn open_meta_as_file_dir_second_priority(flags: u32, mode: u32) {
            let mode_with_dir = (mode & !MODE_TYPE_MASK) | MODE_TYPE_DIRECTORY;
            prop_assert!(!open_meta_as_file(flags, mode_with_dir));

            let mode_without_file = if mode & MODE_TYPE_MASK == MODE_TYPE_FILE {
                mode & !MODE_TYPE_FILE
            } else {
                mode
            };
            prop_assert!(!open_meta_as_file(flags | OPEN_FLAG_DIRECTORY, mode_without_file));
            prop_assert!(!open_meta_as_file(flags | OPEN_FLAG_NODE_REFERENCE, mode_without_file));
        }

        #[test]
        fn open_meta_as_file_file_fallback(mut flags: u32, mut mode: u32) {
            mode = mode & !(MODE_TYPE_FILE | MODE_TYPE_DIRECTORY);
            flags = flags & !(OPEN_FLAG_DIRECTORY | OPEN_FLAG_NODE_REFERENCE);
            prop_assert!(open_meta_as_file(flags, mode));
        }
    }

    struct TestEnv {
        _blobfs_fake: FakeBlobfs,
    }

    impl TestEnv {
        async fn new() -> (Self, Meta) {
            let pkg = PackageBuilder::new("pkg")
                .add_resource_at("meta/dir/file", &b"contents"[..])
                .build()
                .await
                .unwrap();
            let (metafar_blob, _) = pkg.contents();
            let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
            blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
            let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
            let meta = Meta::new(Arc::new(root_dir));
            (Self { _blobfs_fake: blobfs_fake }, meta)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_size() {
        let (_env, meta) = TestEnv::new().await;
        assert_eq!(meta.file_size(), 64);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_disallowed_flags() {
        let (_env, meta) = TestEnv::new().await;
        let meta = Arc::new(meta);

        for forbidden_flag in [
            OPEN_RIGHT_WRITABLE,
            OPEN_RIGHT_ADMIN,
            OPEN_RIGHT_EXECUTABLE,
            OPEN_FLAG_CREATE,
            OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_FLAG_TRUNCATE,
            OPEN_FLAG_APPEND,
        ] {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
            DirectoryEntry::open(
                Arc::clone(&meta),
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
    async fn directory_entry_open_self_as_dir() {
        let (_env, meta) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

        Arc::new(meta).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            VfsPath::dot(),
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
                    name: "package".to_string(),
                    kind: files_async::DirentKind::File
                }
            ]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_self_as_file() {
        let (_env, meta) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        let hash = meta.root_dir.hash.to_string();

        Arc::new(meta).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_FILE,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(io_util::file::read(&proxy).await.unwrap(), hash.as_bytes());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_file() {
        let (_env, meta) = TestEnv::new().await;
        let meta = Arc::new(meta);

        for path in ["dir/file", "dir/file/"] {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
            Arc::clone(&meta).open(
                ExecutionScope::new(),
                OPEN_RIGHT_READABLE,
                0,
                VfsPath::validate_and_split(path).unwrap(),
                server_end.into_channel().into(),
            );

            assert_eq!(io_util::file::read(&proxy).await.unwrap(), b"contents".to_vec());
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_directory() {
        let (_env, meta) = TestEnv::new().await;
        let meta = Arc::new(meta);

        for path in ["dir", "dir/"] {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
            Arc::clone(&meta).open(
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
    async fn directory_entry_entry_info() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&meta),
            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_can_hardlink() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(DirectoryEntry::can_hardlink(&meta), false);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_entry_not_supported() {
        let (_env, meta) = TestEnv::new().await;

        match Directory::get_entry(Arc::new(meta), "") {
            AsyncGetEntry::Future(_) => panic!("Meta::get_entry should immediately fail"),
            AsyncGetEntry::Immediate(res) => {
                assert_eq!(res.err().unwrap(), zx::Status::NOT_SUPPORTED)
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_read_dirents() {
        let (_env, meta) = TestEnv::new().await;

        let (pos, sealed) = meta
            .read_dirents(&TraversalPosition::Start, Box::new(crate::tests::FakeSink::new(4)))
            .await
            .expect("read_dirents failed");
        assert_eq!(
            crate::tests::FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("contents".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                ("dir".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("package".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_register_watcher_not_supported() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(
            Directory::register_watcher(
                Arc::new(meta),
                ExecutionScope::new(),
                0,
                AsyncChannel::from_channel(Channel::create().unwrap().0).unwrap()
            ),
            Err(zx::Status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_attrs() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(
            Directory::get_attrs(&meta).await.unwrap(),
            NodeAttributes {
                mode: MODE_TYPE_DIRECTORY
                    | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ false),
                id: 1,
                content_size: 3,
                storage_size: 3,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_close() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(Directory::close(&meta), Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_open() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(File::open(&meta, 0).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_caps_offset() {
        let (_env, meta) = TestEnv::new().await;
        let mut buffer = vec![0u8];
        assert_eq!(
            File::read_at(
                &meta,
                (meta.root_dir.hash.to_string().as_bytes().len() + 1).try_into().unwrap(),
                buffer.as_mut()
            )
            .await,
            Ok(0)
        );
        assert_eq!(buffer.as_slice(), &[0]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_caps_count() {
        let (_env, meta) = TestEnv::new().await;
        let mut buffer = vec![0u8; 2];
        assert_eq!(
            File::read_at(
                &meta,
                (meta.root_dir.hash.to_string().as_bytes().len() - 1).try_into().unwrap(),
                buffer.as_mut()
            )
            .await,
            Ok(1)
        );
        assert_eq!(
            buffer.as_slice(),
            &[*meta.root_dir.hash.to_string().as_bytes().last().unwrap(), 0]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at() {
        let (_env, meta) = TestEnv::new().await;
        let content_len = meta.root_dir.hash.to_string().as_bytes().len();
        let mut buffer = vec![0u8; content_len];

        assert_eq!(File::read_at(&meta, 0, buffer.as_mut()).await, Ok(64));
        assert_eq!(buffer.as_slice(), meta.root_dir.hash.to_string().as_bytes());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_write_at() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(File::write_at(&meta, 0, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_append() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(File::append(&meta, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_truncate() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(File::truncate(&meta, 0).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_buffer() {
        let (_env, meta) = TestEnv::new().await;

        use vfs::file::SharingMode::*;
        for sharing_mode in [Private, Shared] {
            for flag in [0, VMO_FLAG_READ] {
                assert_eq!(
                    File::get_buffer(&meta, sharing_mode, flag).await.err().unwrap(),
                    zx::Status::NOT_SUPPORTED
                );
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_size() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(File::get_size(&meta).await, Ok(64));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_attrs() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(
            File::get_attrs(&meta).await,
            Ok(NodeAttributes {
                mode: MODE_TYPE_FILE
                    | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ false),
                id: 1,
                content_size: 64,
                storage_size: 64,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_set_attrs() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(
            File::set_attrs(
                &meta,
                0,
                NodeAttributes {
                    mode: 0,
                    id: 0,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 0,
                    creation_time: 0,
                    modification_time: 0,
                },
            )
            .await,
            Err(zx::Status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_close() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(File::close(&meta).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_sync() {
        let (_env, meta) = TestEnv::new().await;

        assert_eq!(File::sync(&meta).await, Err(zx::Status::NOT_SUPPORTED));
    }
}
