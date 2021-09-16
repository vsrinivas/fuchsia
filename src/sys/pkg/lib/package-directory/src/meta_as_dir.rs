// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{meta_file::MetaFile, meta_subdir::MetaSubdir, root_dir::RootDir, usize_to_u64_safe},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    std::sync::Arc,
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            entry::EntryInfo,
            immutable::connection::io1::{ImmutableConnection, ImmutableConnectionClient},
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

pub(crate) struct MetaAsDir {
    root_dir: Arc<RootDir>,
}

impl MetaAsDir {
    pub(crate) fn new(root_dir: Arc<RootDir>) -> Self {
        MetaAsDir { root_dir }
    }
}

impl vfs::directory::entry::DirectoryEntry for MetaAsDir {
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

            // Only MetaAsDir can be obtained from Open calls to MetaAsDir. To obtain MetaAsFile,
            // the Open call must be made on RootDir. This is consistent with pkgfs behavior and is
            // needed so that Clone'ing MetaAsDir results in MetaAsDir, because VFS handles Clone
            // by calling Open with a path of ".", a mode of 0, and mostly unmodified flags and
            // that combination of arguments would normally result in MetaAsFile being used.
            let () = ImmutableConnection::create_connection(
                scope,
                OpenDirectory::new(self as Arc<dyn ImmutableConnectionClient>),
                flags,
                server_end,
            );
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
}

#[async_trait]
impl vfs::directory::entry_container::Directory for MetaAsDir {
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::{AsyncChannel, Channel},
        fidl_fuchsia_io::{
            DirectoryMarker, FileMarker, DIRENT_TYPE_FILE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
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
        async fn new() -> (Self, MetaAsDir) {
            let pkg = PackageBuilder::new("pkg")
                .add_resource_at("meta/dir/file", &b"contents"[..])
                .build()
                .await
                .unwrap();
            let (metafar_blob, _) = pkg.contents();
            let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
            blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
            let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
            let meta_as_dir = MetaAsDir::new(Arc::new(root_dir));
            (Self { _blobfs_fake: blobfs_fake }, meta_as_dir)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_disallowed_flags() {
        let (_env, meta_as_dir) = TestEnv::new().await;
        let meta_as_dir = Arc::new(meta_as_dir);

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
                Arc::clone(&meta_as_dir),
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
        let (_env, meta_as_dir) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

        Arc::new(meta_as_dir).open(
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
    async fn directory_entry_open_file() {
        let (_env, meta_as_dir) = TestEnv::new().await;
        let meta_as_dir = Arc::new(meta_as_dir);

        for path in ["dir/file", "dir/file/"] {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
            Arc::clone(&meta_as_dir).open(
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
        let (_env, meta_as_dir) = TestEnv::new().await;
        let meta_as_dir = Arc::new(meta_as_dir);

        for path in ["dir", "dir/"] {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
            Arc::clone(&meta_as_dir).open(
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
        let (_env, meta_as_dir) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&meta_as_dir),
            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_read_dirents() {
        let (_env, meta_as_dir) = TestEnv::new().await;

        let (pos, sealed) = meta_as_dir
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
        let (_env, meta_as_dir) = TestEnv::new().await;

        assert_eq!(
            Directory::register_watcher(
                Arc::new(meta_as_dir),
                ExecutionScope::new(),
                0,
                AsyncChannel::from_channel(Channel::create().unwrap().0).unwrap()
            ),
            Err(zx::Status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_attrs() {
        let (_env, meta_as_dir) = TestEnv::new().await;

        assert_eq!(
            Directory::get_attrs(&meta_as_dir).await.unwrap(),
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
        let (_env, meta_as_dir) = TestEnv::new().await;

        assert_eq!(Directory::close(&meta_as_dir), Ok(()));
    }
}
