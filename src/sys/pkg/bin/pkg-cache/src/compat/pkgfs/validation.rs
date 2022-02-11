// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::BitFlags as _,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, INO_UNKNOWN, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_POSIX_DEPRECATED,
        OPEN_FLAG_POSIX_EXECUTABLE, OPEN_FLAG_POSIX_WRITABLE, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    std::{
        collections::{BTreeMap, HashSet},
        sync::Arc,
    },
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

/// The pkgfs /ctl/validation directory, except it contains only the "missing" file (e.g. does not
/// have the "present" file).
pub(crate) struct Validation {
    blobfs: blobfs::Client,
    base_blobs: HashSet<fuchsia_hash::Hash>,
}

impl Validation {
    pub(crate) fn new(blobfs: blobfs::Client, base_blobs: HashSet<fuchsia_hash::Hash>) -> Self {
        Self { blobfs, base_blobs }
    }

    // The contents of the "missing" file. The hex-encoded hashes of all the base blobs missing
    // from blobfs, separated and terminated by the newline character, '\n'.
    async fn make_missing_contents(&self) -> Vec<u8> {
        let mut missing = self
            .blobfs
            .filter_to_missing_blobs(&self.base_blobs)
            .await
            .into_iter()
            .collect::<Vec<_>>();
        missing.sort();
        missing.into_iter().map(|hash| format!("{hash}\n")).collect::<String>().into_bytes()
    }
}

impl vfs::directory::entry::DirectoryEntry for Validation {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: VfsPath,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let flags = flags.unset(
            OPEN_FLAG_POSIX_WRITABLE | OPEN_FLAG_POSIX_EXECUTABLE | OPEN_FLAG_POSIX_DEPRECATED,
        );
        if path.is_empty() {
            if flags
                & (OPEN_RIGHT_WRITABLE
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

            let () = ImmutableConnection::create_connection(scope, self, flags, server_end);
            return;
        }

        if path.as_ref() == "missing" {
            let () = scope.clone().spawn(async move {
                let () = vfs::file::vmo::asynchronous::read_only_const(
                    self.make_missing_contents().await.as_slice(),
                )
                .open(scope, flags, mode, VfsPath::dot(), server_end);
            });
            return;
        }

        let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
}

#[async_trait]
impl vfs::directory::entry_container::Directory for Validation {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
        zx::Status,
    > {
        super::read_dirents(
            &BTreeMap::from([("missing".to_string(), super::DirentType::File)]),
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
            mode: MODE_TYPE_DIRECTORY,
            id: 1,
            content_size: 1,
            storage_size: 1,
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
        assert_matches::assert_matches,
        blobfs_ramdisk::BlobfsRamdisk,
        fidl::{AsyncChannel, Channel},
        fidl_fuchsia_io::{
            DirectoryMarker, FileMarker, DIRENT_TYPE_FILE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
        },
        futures::stream::StreamExt as _,
        vfs::directory::{entry::DirectoryEntry, entry_container::Directory},
    };

    struct TestEnv {
        _blobfs: BlobfsRamdisk,
    }

    impl TestEnv {
        async fn new() -> (Self, Validation) {
            Self::with_base_blobs_and_blobfs_contents(HashSet::new(), std::iter::empty()).await
        }

        async fn with_base_blobs_and_blobfs_contents(
            base_blobs: HashSet<fuchsia_hash::Hash>,
            blobfs_contents: impl IntoIterator<Item = (fuchsia_hash::Hash, Vec<u8>)>,
        ) -> (Self, Validation) {
            let blobfs = BlobfsRamdisk::start().unwrap();
            for (hash, contents) in blobfs_contents.into_iter() {
                blobfs.add_blob_from(&hash, contents.as_slice()).unwrap()
            }
            let validation = Validation::new(blobfs.client(), base_blobs);
            (Self { _blobfs: blobfs }, validation)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_unsets_posix_flags() {
        let (_env, validation) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

        Arc::new(validation).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE
                | OPEN_FLAG_POSIX_WRITABLE
                | OPEN_FLAG_POSIX_EXECUTABLE
                | OPEN_FLAG_POSIX_DEPRECATED,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, OPEN_RIGHT_READABLE);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_disallowed_flags() {
        let (_env, validation) = TestEnv::new().await;
        let validation = Arc::new(validation);

        for forbidden_flag in [
            OPEN_RIGHT_WRITABLE,
            OPEN_RIGHT_EXECUTABLE,
            OPEN_FLAG_CREATE,
            OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_FLAG_TRUNCATE,
            OPEN_FLAG_APPEND,
        ] {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
            DirectoryEntry::open(
                Arc::clone(&validation),
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
        let (_env, validation) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();

        Arc::new(validation).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![files_async::DirEntry {
                name: "missing".to_string(),
                kind: files_async::DirentKind::File
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_missing() {
        let (_env, validation) = TestEnv::with_base_blobs_and_blobfs_contents(
            HashSet::from([[0; 32].into()]),
            std::iter::empty(),
        )
        .await;
        let validation = Arc::new(validation);

        let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        Arc::clone(&validation).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            0,
            VfsPath::validate_and_split("missing").unwrap(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            io_util::file::read(&proxy).await.unwrap(),
            b"0000000000000000000000000000000000000000000000000000000000000000\n".to_vec()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_entry_info() {
        let (_env, validation) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&validation),
            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_read_dirents() {
        let (_env, validation) = TestEnv::new().await;

        let (pos, sealed) = validation
            .read_dirents(
                &TraversalPosition::Start,
                Box::new(crate::compat::pkgfs::testing::FakeSink::new(3)),
            )
            .await
            .expect("read_dirents failed");
        assert_eq!(
            crate::compat::pkgfs::testing::FakeSink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("missing".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_register_watcher_not_supported() {
        let (_env, validation) = TestEnv::new().await;

        assert_eq!(
            Directory::register_watcher(
                Arc::new(validation),
                ExecutionScope::new(),
                0,
                AsyncChannel::from_channel(Channel::create().unwrap().0).unwrap()
            ),
            Err(zx::Status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_attrs() {
        let (_env, validation) = TestEnv::new().await;

        assert_eq!(
            Directory::get_attrs(&validation).await.unwrap(),
            NodeAttributes {
                mode: MODE_TYPE_DIRECTORY,
                id: 1,
                content_size: 1,
                storage_size: 1,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_close() {
        let (_env, validation) = TestEnv::new().await;

        assert_eq!(Directory::close(&validation), Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_missing_contents_empty() {
        let (_env, validation) = TestEnv::new().await;

        assert_eq!(validation.make_missing_contents().await, Vec::<u8>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_missing_contents_missing_blob() {
        let (_env, validation) = TestEnv::with_base_blobs_and_blobfs_contents(
            HashSet::from([[0; 32].into()]),
            std::iter::empty(),
        )
        .await;

        assert_eq!(
            validation.make_missing_contents().await,
            b"0000000000000000000000000000000000000000000000000000000000000000\n".to_vec()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_missing_contents_two_missing_blob() {
        let (_env, validation) = TestEnv::with_base_blobs_and_blobfs_contents(
            HashSet::from([[0; 32].into(), [1; 32].into()]),
            std::iter::empty(),
        )
        .await;

        assert_eq!(
            validation.make_missing_contents().await,
            b"0000000000000000000000000000000000000000000000000000000000000000\n\
              0101010101010101010101010101010101010101010101010101010101010101\n"
                .to_vec(),
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_missing_contents_irrelevant_blobfs_blob() {
        let blob = vec![0u8, 1u8];
        let hash = fuchsia_merkle::MerkleTree::from_reader(blob.as_slice()).unwrap().root();
        let (_env, validation) =
            TestEnv::with_base_blobs_and_blobfs_contents(HashSet::new(), [(hash, blob)]).await;

        assert_eq!(validation.make_missing_contents().await, Vec::<u8>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_missing_contents_present_blob() {
        let blob = vec![0u8, 1u8];
        let hash = fuchsia_merkle::MerkleTree::from_reader(blob.as_slice()).unwrap().root();
        let (_env, validation) =
            TestEnv::with_base_blobs_and_blobfs_contents(HashSet::from([hash]), [(hash, blob)])
                .await;

        assert_eq!(validation.make_missing_contents().await, Vec::<u8>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn make_missing_contents_present_blob_missing_blob() {
        let blob = vec![0u8, 1u8];
        let hash = fuchsia_merkle::MerkleTree::from_reader(blob.as_slice()).unwrap().root();
        let mut missing_hash = <[u8; 32]>::from(hash);
        missing_hash[0] = !missing_hash[0];
        let missing_hash = missing_hash.into();

        let (_env, validation) = TestEnv::with_base_blobs_and_blobfs_contents(
            HashSet::from([hash, missing_hash]),
            [(hash, blob)],
        )
        .await;

        assert_eq!(
            validation.make_missing_contents().await,
            format!("{missing_hash}\n").into_bytes()
        );
    }
}
