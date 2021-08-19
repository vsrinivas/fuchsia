// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::root_dir::RootDir,
    anyhow::{anyhow, Context as _},
    async_trait::async_trait,
    fidl::{endpoints::ServerEnd, HandleBased as _},
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN, MODE_TYPE_FILE,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DIRECTORY,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_WRITABLE,
        VMO_FLAG_EXACT, VMO_FLAG_EXEC, VMO_FLAG_READ, VMO_FLAG_WRITE,
    },
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    once_cell::sync::OnceCell,
    std::sync::Arc,
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::entry::EntryInfo,
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

/// Location of MetaFile contents within a meta.far
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct MetaFileLocation {
    pub(crate) offset: u64,
    pub(crate) length: u64,
}

pub(crate) struct MetaFile {
    root_dir: Arc<RootDir>,
    location: MetaFileLocation,
    vmo: OnceCell<zx::Vmo>,
}

impl MetaFile {
    pub(crate) fn new(root_dir: Arc<RootDir>, location: MetaFileLocation) -> Self {
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

impl vfs::directory::entry::DirectoryEntry for MetaFile {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        _mode: u32,
        path: VfsPath,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if !path.is_empty() {
            let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_DIR);
            return;
        }

        if flags
            & (OPEN_RIGHT_WRITABLE
                | OPEN_RIGHT_ADMIN
                | OPEN_RIGHT_EXECUTABLE
                | OPEN_FLAG_CREATE
                | OPEN_FLAG_CREATE_IF_ABSENT
                | OPEN_FLAG_TRUNCATE
                | OPEN_FLAG_DIRECTORY
                | OPEN_FLAG_APPEND)
            != 0
        {
            let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
            return;
        }

        let () = vfs::file::connection::io1::FileConnection::<Self>::create_connection(
            scope.clone(),
            vfs::file::connection::util::OpenFile::new(self, scope),
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
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl vfs::file::File for MetaFile {
    async fn open(&self, _flags: u32) -> Result<(), zx::Status> {
        Ok(())
    }

    async fn read_at(&self, offset_chunk: u64, buffer: &mut [u8]) -> Result<u64, zx::Status> {
        let offset_far = offset_chunk + self.location.offset;
        let count = std::cmp::min(
            crate::usize_to_u64_safe(buffer.len()),
            self.location.offset + self.location.length - offset_far,
        );
        let (status, bytes) =
            self.root_dir.meta_far.read_at(count, offset_far).await.map_err(|e| {
                fx_log_err!("meta.far read_at fidl error: {:#}", anyhow!(e));
                zx::Status::INTERNAL
            })?;
        let () = zx::Status::ok(status).map_err(|e| {
            fx_log_err!("meta.far read_at protocol error: {:#}", anyhow!(e.clone()));
            e
        })?;
        let () = &mut buffer[..bytes.len()].copy_from_slice(&bytes);
        Ok(crate::usize_to_u64_safe(bytes.len()))
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
        mode: vfs::file::SharingMode,
        flags: u32,
    ) -> Result<Option<fidl_fuchsia_mem::Buffer>, zx::Status> {
        if flags & (VMO_FLAG_WRITE | VMO_FLAG_EXEC | VMO_FLAG_EXACT) != 0 {
            return Err(zx::Status::NOT_SUPPORTED);
        }

        let vmo = self.vmo().await.map_err(|e| {
            fx_log_err!("Failed to get MetaFile VMO during get_buffer: {:#}", anyhow!(e));
            zx::Status::INTERNAL
        })?;
        match mode {
            vfs::file::SharingMode::Private => {
                let vmo = vmo
                    .create_child(
                        zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE
                            | zx::VmoChildOptions::NO_WRITE,
                        0, /*offset*/
                        self.location.length,
                    )
                    .map_err(|e| {
                        fx_log_err!(
                            "Failed to create private child VMO during get_buffer: {:#}",
                            anyhow!(e.clone())
                        );
                        e
                    })?;
                Ok(Some(fidl_fuchsia_mem::Buffer { vmo, size: self.location.length }))
            }
            vfs::file::SharingMode::Shared => {
                let rights = zx::Rights::BASIC
                    | zx::Rights::MAP
                    | zx::Rights::PROPERTY
                    | if flags & VMO_FLAG_READ != 0 { zx::Rights::READ } else { zx::Rights::NONE };
                let vmo = vmo.duplicate_handle(rights).map_err(|e| {
                    fx_log_err!(
                        "Failed to clone VMO handle during get_buffer: {:#}",
                        anyhow!(e.clone())
                    );
                    e
                })?;
                Ok(Some(fidl_fuchsia_mem::Buffer { vmo, size: self.location.length }))
            }
        }
    }

    async fn get_size(&self) -> Result<u64, zx::Status> {
        Ok(self.location.length)
    }

    async fn get_attrs(&self) -> Result<NodeAttributes, zx::Status> {
        Ok(NodeAttributes {
            mode: MODE_TYPE_FILE
                | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ false),
            id: 1,
            content_size: self.location.length,
            storage_size: self.location.length,
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
        fidl::{endpoints::Proxy as _, AsHandleRef as _},
        fidl_fuchsia_io::{FileProxy, NodeProxy, OPEN_FLAG_DESCRIBE},
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        futures::stream::StreamExt as _,
        matches::assert_matches,
        std::convert::{TryFrom as _, TryInto as _},
        vfs::{directory::entry::DirectoryEntry, file::File},
    };

    const TEST_FILE_CONTENTS: [u8; 4] = [0, 1, 2, 3];

    struct TestEnv {
        _blobfs_fake: FakeBlobfs,
    }

    impl TestEnv {
        async fn new() -> (Self, MetaFile) {
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

    fn node_to_file_proxy(proxy: NodeProxy) -> FileProxy {
        FileProxy::from_channel(proxy.into_channel().unwrap())
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
            OPEN_FLAG_DESCRIBE,
            0,
            VfsPath::validate_and_split("non-empty").unwrap(),
            server_end,
        );

        assert_matches!(
            node_to_file_proxy(proxy).take_event_stream().next().await,
            Some(Ok(fidl_fuchsia_io::FileEvent::OnOpen_{ s, info: None}))
                if s == zx::Status::NOT_DIR.into_raw()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_disallowed_flags() {
        let (_env, meta_file) = TestEnv::new().await;
        let meta_file = Arc::new(meta_file);

        for forbidden_flag in [
            OPEN_RIGHT_WRITABLE,
            OPEN_RIGHT_ADMIN,
            OPEN_RIGHT_EXECUTABLE,
            OPEN_FLAG_CREATE,
            OPEN_FLAG_CREATE_IF_ABSENT,
            OPEN_FLAG_TRUNCATE,
            OPEN_FLAG_DIRECTORY,
            OPEN_FLAG_APPEND,
        ] {
            let (proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
            DirectoryEntry::open(
                Arc::clone(&meta_file),
                ExecutionScope::new(),
                OPEN_FLAG_DESCRIBE | forbidden_flag,
                0,
                VfsPath::dot(),
                server_end,
            );

            assert_matches!(
                node_to_file_proxy(proxy).take_event_stream().next().await,
                Some(Ok(fidl_fuchsia_io::FileEvent::OnOpen_{ s, info: None}))
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
            OPEN_FLAG_DESCRIBE,
            0,
            VfsPath::dot(),
            server_end,
        );

        assert_matches!(
            node_to_file_proxy(proxy).take_event_stream().next().await,
            Some(Ok(fidl_fuchsia_io::FileEvent::OnOpen_ { s, info: Some(_) }))
                if s == zx::Status::OK.into_raw()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_entry_info() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&meta_file),
            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_can_hardlink() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(DirectoryEntry::can_hardlink(&meta_file), false);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_open() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::open(&meta_file, 0).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_adjusts_offset() {
        let (_env, meta_file) = TestEnv::new().await;
        let mut buffer = [0u8];

        for (i, e) in TEST_FILE_CONTENTS.iter().enumerate() {
            assert_eq!(File::read_at(&meta_file, i.try_into().unwrap(), &mut buffer).await, Ok(1));
            assert_eq!(&buffer, &[*e]);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_past_end_returns_no_bytes() {
        let (_env, meta_file) = TestEnv::new().await;
        let mut buffer = [0u8];

        assert_eq!(
            File::read_at(&meta_file, TEST_FILE_CONTENTS.len().try_into().unwrap(), &mut buffer)
                .await,
            Ok(0)
        );
        assert_eq!(&buffer, &[0]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_caps_count() {
        let (_env, meta_file) = TestEnv::new().await;
        let mut buffer = [0u8; 5];

        assert_eq!(File::read_at(&meta_file, 2, &mut buffer).await, Ok(2));
        assert_eq!(&buffer, &[2, 3, 0, 0, 0]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_write_at() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::write_at(&meta_file, 0, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_append() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::append(&meta_file, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_truncate() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::truncate(&meta_file, 0).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_buffer_rejects_unsupported_flags() {
        let (_env, meta_file) = TestEnv::new().await;

        use vfs::file::SharingMode::*;
        for sharing_mode in [Private, Shared] {
            for flag in [VMO_FLAG_WRITE, VMO_FLAG_EXEC, VMO_FLAG_EXACT] {
                assert_eq!(
                    File::get_buffer(&meta_file, sharing_mode, flag).await.err().unwrap(),
                    zx::Status::NOT_SUPPORTED
                );
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_buffer_private() {
        let (_env, meta_file) = TestEnv::new().await;

        let fidl_fuchsia_mem::Buffer { vmo, size } =
            File::get_buffer(&meta_file, vfs::file::SharingMode::Private, 0)
                .await
                .expect("get_buffer should succeed")
                .expect("get_buffer should return a buffer");

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
    async fn file_get_buffer_shared() {
        let (_env, meta_file) = TestEnv::new().await;

        let fidl_fuchsia_mem::Buffer { vmo, size } =
            File::get_buffer(&meta_file, vfs::file::SharingMode::Shared, VMO_FLAG_READ)
                .await
                .expect("get_buffer should succeed")
                .expect("get_buffer should return a buffer");

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
            Ok(NodeAttributes {
                mode: MODE_TYPE_FILE
                    | rights_to_posix_mode_bits(/*r*/ true, /*w*/ false, /*x*/ false),
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
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::close(&meta_file).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_sync() {
        let (_env, meta_file) = TestEnv::new().await;

        assert_eq!(File::sync(&meta_file).await, Err(zx::Status::NOT_SUPPORTED));
    }
}
