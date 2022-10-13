// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::root_dir::RootDir,
    anyhow::Context as _,
    async_trait::async_trait,
    fidl::{endpoints::ServerEnd, HandleBased as _},
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    once_cell::sync::OnceCell,
    std::sync::Arc,
    tracing::error,
    vfs::{
        common::send_on_open_with_error, directory::entry::EntryInfo,
        execution_scope::ExecutionScope, path::Path as VfsPath,
    },
};

/// Location of MetaFile contents within a meta.far
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct MetaFileLocation {
    pub(crate) offset: u64,
    pub(crate) length: u64,
}

pub(crate) struct MetaFile<S: crate::NonMetaStorage> {
    root_dir: Arc<RootDir<S>>,
    location: MetaFileLocation,
    vmo: OnceCell<zx::Vmo>,
}

impl<S: crate::NonMetaStorage> MetaFile<S> {
    pub(crate) fn new(root_dir: Arc<RootDir<S>>, location: MetaFileLocation) -> Self {
        MetaFile { root_dir, location, vmo: OnceCell::new() }
    }

    async fn vmo(&self) -> Result<&zx::Vmo, anyhow::Error> {
        Ok(if let Some(vmo) = self.vmo.get() {
            vmo
        } else {
            let far_vmo = self.root_dir.meta_far_vmo().await.context("getting far vmo")?;
            // The FAR spec requires 4 KiB alignment of content chunks [1], so offset will
            // always be page-aligned, because pages are required [2] to be a power of 2 and at
            // least 4 KiB.
            // [1] https://fuchsia.dev/fuchsia-src/concepts/source_code/archive_format#content_chunk
            // [2] https://fuchsia.dev/fuchsia-src/reference/syscalls/system_get_page_size
            // TODO(fxbug.dev/82006) Need to manually zero the end of the VMO if
            // zx_system_get_page_size() > 4K.
            assert_eq!(zx::system_get_page_size(), 4096);
            let vmo = far_vmo
                .create_child(
                    zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE | zx::VmoChildOptions::NO_WRITE,
                    self.location.offset,
                    self.location.length,
                )
                .context("creating MetaFile VMO")?;
            self.vmo.get_or_init(|| vmo)
        })
    }
}

impl<S: crate::NonMetaStorage> vfs::directory::entry::DirectoryEntry for MetaFile<S> {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: fio::OpenFlags,
        _mode: u32,
        path: VfsPath,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if !path.is_empty() {
            let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_DIR);
            return;
        }

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

        let () = vfs::file::connection::io1::create_connection(
            scope.clone(),
            self,
            flags,
            server_end,
            // readable/writable do not override what's set in flags, they merely tell the
            // FileConnection that it's valid to open the file readable/writable.
            true,  /*=readable*/
            false, /*=writable*/
            false, /*=executable*/
        );
    }

    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File)
    }
}

#[async_trait]
impl<S: crate::NonMetaStorage> vfs::file::File for MetaFile<S> {
    async fn open(&self, _flags: fio::OpenFlags) -> Result<(), zx::Status> {
        Ok(())
    }

    async fn truncate(&self, _length: u64) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn get_backing_memory(&self, flags: fio::VmoFlags) -> Result<zx::Vmo, zx::Status> {
        if flags.intersects(
            fio::VmoFlags::WRITE | fio::VmoFlags::EXECUTE | fio::VmoFlags::SHARED_BUFFER,
        ) {
            return Err(zx::Status::NOT_SUPPORTED);
        }

        let vmo = self.vmo().await.map_err(|e: anyhow::Error| {
            error!("Failed to get MetaFile VMO during get_backing_memory: {:#}", e);
            zx::Status::INTERNAL
        })?;

        if flags.contains(fio::VmoFlags::PRIVATE_CLONE) {
            let vmo = vmo
                .create_child(
                    zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE | zx::VmoChildOptions::NO_WRITE,
                    0, /*offset*/
                    self.location.length,
                )
                .map_err(|e: zx::Status| {
                    error!("Failed to create private child VMO during get_backing_memory: {:#}", e);
                    e
                })?;
            Ok(vmo)
        } else {
            let rights = zx::Rights::BASIC
                | zx::Rights::MAP
                | zx::Rights::PROPERTY
                | if flags.contains(fio::VmoFlags::READ) {
                    zx::Rights::READ
                } else {
                    zx::Rights::NONE
                };
            let vmo = vmo.duplicate_handle(rights).map_err(|e: zx::Status| {
                error!("Failed to clone VMO handle during get_backing_memory: {:#}", e);
                e
            })?;
            Ok(vmo)
        }
    }

    async fn get_size(&self) -> Result<u64, zx::Status> {
        Ok(self.location.length)
    }

    async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_FILE
                | vfs::common::rights_to_posix_mode_bits(
                    true,  // read
                    true,  // write
                    false, // execute
                ),
            id: 1,
            content_size: self.location.length,
            storage_size: self.location.length,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    async fn set_attrs(
        &self,
        _flags: fio::NodeAttributeFlags,
        _attrs: fio::NodeAttributes,
    ) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn close(&self) -> Result<(), zx::Status> {
        Ok(())
    }

    async fn sync(&self) -> Result<(), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }
}

#[async_trait]
impl<S: crate::NonMetaStorage> vfs::file::FileIo for MetaFile<S> {
    async fn read_at(&self, offset_chunk: u64, buffer: &mut [u8]) -> Result<u64, zx::Status> {
        let offset_chunk = std::cmp::min(offset_chunk, self.location.length);
        let offset_far = offset_chunk + self.location.offset;
        let count = std::cmp::min(
            crate::usize_to_u64_safe(buffer.len()),
            self.location.length - offset_chunk,
        );
        let bytes = self
            .root_dir
            .meta_far
            .read_at(count, offset_far)
            .await
            .map_err(|e: fidl::Error| {
                error!("meta.far read_at fidl error: {:#}", e);
                zx::Status::INTERNAL
            })?
            .map_err(zx::Status::from_raw)
            .map_err(|e: zx::Status| {
                error!("meta.far read_at protocol error: {:#}", e);
                e
            })?;
        let () = buffer[..bytes.len()].copy_from_slice(&bytes);
        Ok(crate::usize_to_u64_safe(bytes.len()))
    }

    async fn write_at(&self, _offset: u64, _content: &[u8]) -> Result<u64, zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }

    async fn append(&self, _content: &[u8]) -> Result<(u64, u64), zx::Status> {
        Err(zx::Status::NOT_SUPPORTED)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::{endpoints::Proxy as _, AsHandleRef as _},
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        futures::stream::StreamExt as _,
        std::convert::{TryFrom as _, TryInto as _},
        vfs::{
            directory::entry::DirectoryEntry,
            file::{File, FileIo},
        },
    };

    const TEST_FILE_CONTENTS: [u8; 4] = [0, 1, 2, 3];

    struct TestEnv {
        _blobfs_fake: FakeBlobfs,
    }

    impl TestEnv {
        async fn new() -> (Self, MetaFile<blobfs::Client>) {
            let pkg = PackageBuilder::new("pkg")
                .add_resource_at("meta/file", &TEST_FILE_CONTENTS[..])
                .build()
                .await
                .unwrap();
            let (metafar_blob, _) = pkg.contents();
            let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
            blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
            let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
            let location = *root_dir.meta_files.get("meta/file").unwrap();
            (TestEnv { _blobfs_fake: blobfs_fake }, MetaFile::new(Arc::new(root_dir), location))
        }
    }

    fn node_to_file_proxy(proxy: fio::NodeProxy) -> fio::FileProxy {
        fio::FileProxy::from_channel(proxy.into_channel().unwrap())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn vmo() {
        let (_env, meta_file) = TestEnv::new().await;

        // VMO is readable
        let vmo = meta_file.vmo().await.unwrap();
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, [0, 1, 2, 3, 0, 0, 0, 0]);
        assert_eq!(
            vmo.get_content_size().unwrap(),
            u64::try_from(TEST_FILE_CONTENTS.len()).unwrap()
        );

        // VMO not writable
        assert_eq!(vmo.write(&[0], 0), Err(zx::Status::ACCESS_DENIED));

        // Accessing the VMO caches it
        assert!(meta_file.vmo.get().is_some());

        // Accessing the VMO through the cached path works
        let vmo = meta_file.vmo().await.unwrap();
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, [0, 1, 2, 3, 0, 0, 0, 0]);
        assert_eq!(vmo.write(&[0], 0), Err(zx::Status::ACCESS_DENIED));
        assert_eq!(
            vmo.get_content_size().unwrap(),
            u64::try_from(TEST_FILE_CONTENTS.len()).unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_non_empty_path() {
        let (_env, meta_file) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();

        DirectoryEntry::open(
            Arc::new(meta_file),
            ExecutionScope::new(),
            fio::OpenFlags::DESCRIBE,
            0,
            VfsPath::validate_and_split("non-empty").unwrap(),
            server_end,
        );

        assert_matches!(
            node_to_file_proxy(proxy).take_event_stream().next().await,
            Some(Ok(fio::FileEvent::OnOpen_{ s, info: None}))
                if s == zx::Status::NOT_DIR.into_raw()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_disallowed_flags() {
        let (_env, meta_file) = TestEnv::new().await;
        let meta_file = Arc::new(meta_file);

        for forbidden_flag in [
            fio::OpenFlags::RIGHT_WRITABLE,
            fio::OpenFlags::RIGHT_EXECUTABLE,
            fio::OpenFlags::CREATE,
            fio::OpenFlags::CREATE_IF_ABSENT,
            fio::OpenFlags::TRUNCATE,
            fio::OpenFlags::APPEND,
        ] {
            let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
            DirectoryEntry::open(
                Arc::clone(&meta_file),
                ExecutionScope::new(),
                fio::OpenFlags::DESCRIBE | forbidden_flag,
                0,
                VfsPath::dot(),
                server_end,
            );

            assert_matches!(
                node_to_file_proxy(proxy).take_event_stream().next().await,
                Some(Ok(fio::FileEvent::OnOpen_{ s, info: None}))
                    if s == zx::Status::NOT_SUPPORTED.into_raw()
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_succeeds() {
        let (_env, meta_file) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();

        DirectoryEntry::open(
            Arc::new(meta_file),
            ExecutionScope::new(),
            fio::OpenFlags::DESCRIBE,
            0,
            VfsPath::dot(),
            server_end,
        );

        assert_matches!(
            node_to_file_proxy(proxy).take_event_stream().next().await,
            Some(Ok(fio::FileEvent::OnOpen_ { s, info: Some(_) }))
                if s == zx::Status::OK.into_raw()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_entry_info() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&meta_file),
            EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::File)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_open() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::open(&meta_file, fio::OpenFlags::empty()).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_adjusts_offset() {
        let (_env, meta_file) = TestEnv::new().await;
        let mut buffer = [0u8];

        for (i, e) in TEST_FILE_CONTENTS.iter().enumerate() {
            assert_eq!(
                FileIo::read_at(&meta_file, i.try_into().unwrap(), &mut buffer).await,
                Ok(1)
            );
            assert_eq!(&buffer, &[*e]);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_past_end_returns_no_bytes() {
        let (_env, meta_file) = TestEnv::new().await;
        let mut buffer = [0u8];

        for i in 0..=1 {
            assert_eq!(
                FileIo::read_at(
                    &meta_file,
                    u64::try_from(TEST_FILE_CONTENTS.len()).unwrap() + i,
                    &mut buffer
                )
                .await,
                Ok(0)
            );
            assert_eq!(&buffer, &[0]);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_caps_count() {
        let (_env, meta_file) = TestEnv::new().await;
        let mut buffer = [0u8; 5];

        assert_eq!(FileIo::read_at(&meta_file, 2, &mut buffer).await, Ok(2));
        assert_eq!(&buffer, &[2, 3, 0, 0, 0]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_write_at() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(FileIo::write_at(&meta_file, 0, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_append() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(FileIo::append(&meta_file, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_truncate() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::truncate(&meta_file, 0).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_backing_memory_rejects_unsupported_flags() {
        let (_env, meta_file) = TestEnv::new().await;

        for flag in [fio::VmoFlags::WRITE, fio::VmoFlags::EXECUTE, fio::VmoFlags::SHARED_BUFFER] {
            assert_eq!(
                File::get_backing_memory(&meta_file, flag).await,
                Err(zx::Status::NOT_SUPPORTED)
            );
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_backing_memory_private() {
        let (_env, meta_file) = TestEnv::new().await;

        let vmo = File::get_backing_memory(&meta_file, fio::VmoFlags::PRIVATE_CLONE)
            .await
            .expect("get_backing_memory should succeed");
        let size = vmo.get_content_size().expect("get_content_size should succeed");

        assert_eq!(size, u64::try_from(TEST_FILE_CONTENTS.len()).unwrap());
        // VMO is readable
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, [0, 1, 2, 3, 0, 0, 0, 0]);
        assert_eq!(
            vmo.get_content_size().unwrap(),
            u64::try_from(TEST_FILE_CONTENTS.len()).unwrap()
        );

        // VMO not writable
        assert_eq!(vmo.write(&[0], 0), Err(zx::Status::ACCESS_DENIED));

        // VMO is not shared
        assert_eq!(vmo.count_info().unwrap().handle_count, 1);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_backing_memory_not_private() {
        let (_env, meta_file) = TestEnv::new().await;

        let vmo = File::get_backing_memory(&meta_file, fio::VmoFlags::READ)
            .await
            .expect("get_backing_memory should succeed");
        let size = vmo.get_content_size().expect("get_content_size should succeed");

        assert_eq!(size, u64::try_from(TEST_FILE_CONTENTS.len()).unwrap());
        // VMO is readable
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, [0, 1, 2, 3, 0, 0, 0, 0]);
        assert_eq!(
            vmo.get_content_size().unwrap(),
            u64::try_from(TEST_FILE_CONTENTS.len()).unwrap()
        );

        // VMO not writable
        assert_eq!(vmo.write(&[0], 0), Err(zx::Status::ACCESS_DENIED));

        // VMO is shared
        assert_eq!(vmo.count_info().unwrap().handle_count, 2);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_size() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(
            File::get_size(&meta_file).await,
            Ok(u64::try_from(TEST_FILE_CONTENTS.len()).unwrap())
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_attrs() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(
            File::get_attrs(&meta_file).await,
            Ok(fio::NodeAttributes {
                mode: fio::MODE_TYPE_FILE | 0o600,
                id: 1,
                content_size: meta_file.location.length,
                storage_size: meta_file.location.length,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            })
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_set_attrs() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(
            File::set_attrs(
                &meta_file,
                fio::NodeAttributeFlags::empty(),
                fio::NodeAttributes {
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
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::close(&meta_file).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_sync() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::sync(&meta_file).await, Err(zx::Status::NOT_SUPPORTED));
    }
}
