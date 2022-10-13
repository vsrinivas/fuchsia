// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    std::{
        collections::{BTreeMap, HashSet},
        sync::Arc,
    },
    tracing::{error, info},
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
        info!("checking if any of the {} base package blobs are missing", self.base_blobs.len());

        let mut missing = self
            .blobfs
            .filter_to_missing_blobs(&self.base_blobs)
            .await
            .into_iter()
            .collect::<Vec<_>>();
        missing.sort();

        if missing.is_empty() {
            info!(total = self.base_blobs.len(), "all base package blobs were found");
        } else {
            error!(total = missing.len(), "base package blobs are missing");
        }

        missing.into_iter().map(|hash| format!("{hash}\n")).collect::<String>().into_bytes()
    }
}

impl vfs::directory::entry::DirectoryEntry for Validation {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        path: VfsPath,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        let flags =
            flags.difference(fio::OpenFlags::POSIX_WRITABLE | fio::OpenFlags::POSIX_EXECUTABLE);
        if path.is_empty() {
            if flags.intersects(
                fio::OpenFlags::RIGHT_WRITABLE
                    | fio::OpenFlags::RIGHT_EXECUTABLE
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
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
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
        _: fio::WatchMask,
        _: vfs::directory::entry_container::DirectoryWatcher,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so no need to do anything here.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_DIRECTORY,
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
        futures::stream::StreamExt as _,
        std::convert::TryInto as _,
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
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

        Arc::new(validation).open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::POSIX_WRITABLE
                | fio::OpenFlags::POSIX_EXECUTABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        let (status, flags) = proxy.get_flags().await.unwrap();
        let () = zx::Status::ok(status).unwrap();
        assert_eq!(flags, fio::OpenFlags::RIGHT_READABLE);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_disallowed_flags() {
        let (_env, validation) = TestEnv::new().await;
        let validation = Arc::new(validation);

        for forbidden_flag in [
            fio::OpenFlags::RIGHT_WRITABLE,
            fio::OpenFlags::RIGHT_EXECUTABLE,
            fio::OpenFlags::CREATE,
            fio::OpenFlags::CREATE_IF_ABSENT,
            fio::OpenFlags::TRUNCATE,
            fio::OpenFlags::APPEND,
        ] {
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            DirectoryEntry::open(
                Arc::clone(&validation),
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
        let (_env, validation) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();

        Arc::new(validation).open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            fuchsia_fs::directory::readdir(&proxy).await.unwrap(),
            vec![fuchsia_fs::directory::DirEntry {
                name: "missing".to_string(),
                kind: fuchsia_fs::directory::DirentKind::File
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

        let (proxy, server_end) = fidl::endpoints::create_proxy::<fio::FileMarker>().unwrap();
        Arc::clone(&validation).open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            0,
            VfsPath::validate_and_split("missing").unwrap(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            fuchsia_fs::file::read(&proxy).await.unwrap(),
            b"0000000000000000000000000000000000000000000000000000000000000000\n".to_vec()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_entry_info() {
        let (_env, validation) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&validation),
            EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
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
                (".".to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)),
                ("missing".to_string(), EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File)),
            ]
        );
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_register_watcher_not_supported() {
        let (_env, validation) = TestEnv::new().await;

        let (_client, server) = fidl::endpoints::create_endpoints().unwrap();

        assert_eq!(
            Directory::register_watcher(
                Arc::new(validation),
                ExecutionScope::new(),
                fio::WatchMask::empty(),
                server.try_into().unwrap(),
            ),
            Err(zx::Status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_get_attrs() {
        let (_env, validation) = TestEnv::new().await;

        assert_eq!(
            Directory::get_attrs(&validation).await.unwrap(),
            fio::NodeAttributes {
                mode: fio::MODE_TYPE_DIRECTORY,
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
