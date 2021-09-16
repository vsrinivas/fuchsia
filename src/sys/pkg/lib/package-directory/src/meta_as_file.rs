// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{root_dir::RootDir, u64_to_usize_safe, usize_to_u64_safe},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        NodeAttributes, NodeMarker, DIRENT_TYPE_FILE, INO_UNKNOWN, MODE_TYPE_FILE,
        OPEN_FLAG_APPEND, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_TRUNCATE,
        OPEN_RIGHT_ADMIN, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon as zx,
    std::sync::Arc,
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::entry::EntryInfo,
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
    },
};

pub(crate) struct MetaAsFile {
    root_dir: Arc<RootDir>,
}

impl MetaAsFile {
    pub(crate) fn new(root_dir: Arc<RootDir>) -> Self {
        MetaAsFile { root_dir }
    }

    fn file_size(&self) -> u64 {
        crate::usize_to_u64_safe(self.root_dir.hash.to_string().as_bytes().len())
    }
}

impl vfs::directory::entry::DirectoryEntry for MetaAsFile {
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
            // readable/writable/executable do not override the flags, they tell the
            // FileConnection if it's ever valid to open the file with that right.
            true,  /*=readable*/
            false, /*=writable*/
            false, /*=executable*/
        );
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
    }
}

#[async_trait]
impl vfs::file::File for MetaAsFile {
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

    async fn get_buffer(&self, _flags: u32) -> Result<fidl_fuchsia_mem::Buffer, zx::Status> {
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
        fidl_fuchsia_io::{
            DirectoryMarker, FileMarker, DIRENT_TYPE_FILE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE,
            VMO_FLAG_EXACT, VMO_FLAG_PRIVATE, VMO_FLAG_READ,
        },
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        futures::stream::StreamExt as _,
        matches::assert_matches,
        std::convert::TryInto as _,
        vfs::{directory::entry::DirectoryEntry, file::File},
    };

    struct TestEnv {
        _blobfs_fake: FakeBlobfs,
    }

    impl TestEnv {
        async fn new() -> (Self, MetaAsFile) {
            let pkg = PackageBuilder::new("pkg").build().await.unwrap();
            let (metafar_blob, _) = pkg.contents();
            let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
            blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
            let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle).await.unwrap();
            let meta_as_file = MetaAsFile::new(Arc::new(root_dir));
            (Self { _blobfs_fake: blobfs_fake }, meta_as_file)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_size() {
        let (_env, meta_as_file) = TestEnv::new().await;
        assert_eq!(meta_as_file.file_size(), 64);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_non_empty_path() {
        let (_env, meta_as_file) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();

        DirectoryEntry::open(
            Arc::new(meta_as_file),
            ExecutionScope::new(),
            OPEN_FLAG_DESCRIBE,
            0,
            VfsPath::validate_and_split("non-empty").unwrap(),
            server_end.into_channel().into(),
        );

        assert_matches!(
            proxy.take_event_stream().next().await,
            Some(Ok(fidl_fuchsia_io::FileEvent::OnOpen_{ s, info: None}))
                if s == zx::Status::NOT_DIR.into_raw()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_open_rejects_disallowed_flags() {
        let (_env, meta_as_file) = TestEnv::new().await;
        let meta_as_file = Arc::new(meta_as_file);

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
                Arc::clone(&meta_as_file),
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
    async fn directory_entry_open_succeeds() {
        let (_env, meta_as_file) = TestEnv::new().await;
        let (proxy, server_end) = fidl::endpoints::create_proxy::<FileMarker>().unwrap();
        let hash = meta_as_file.root_dir.hash.to_string();

        Arc::new(meta_as_file).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            MODE_TYPE_FILE,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(io_util::file::read(&proxy).await.unwrap(), hash.as_bytes());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn directory_entry_entry_info() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(
            DirectoryEntry::entry_info(&meta_as_file),
            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_open() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(File::open(&meta_as_file, 0).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_caps_offset() {
        let (_env, meta_as_file) = TestEnv::new().await;
        let mut buffer = vec![0u8];
        assert_eq!(
            File::read_at(
                &meta_as_file,
                (meta_as_file.root_dir.hash.to_string().as_bytes().len() + 1).try_into().unwrap(),
                buffer.as_mut()
            )
            .await,
            Ok(0)
        );
        assert_eq!(buffer.as_slice(), &[0]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at_caps_count() {
        let (_env, meta_as_file) = TestEnv::new().await;
        let mut buffer = vec![0u8; 2];
        assert_eq!(
            File::read_at(
                &meta_as_file,
                (meta_as_file.root_dir.hash.to_string().as_bytes().len() - 1).try_into().unwrap(),
                buffer.as_mut()
            )
            .await,
            Ok(1)
        );
        assert_eq!(
            buffer.as_slice(),
            &[*meta_as_file.root_dir.hash.to_string().as_bytes().last().unwrap(), 0]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_read_at() {
        let (_env, meta_as_file) = TestEnv::new().await;
        let content_len = meta_as_file.root_dir.hash.to_string().as_bytes().len();
        let mut buffer = vec![0u8; content_len];

        assert_eq!(File::read_at(&meta_as_file, 0, buffer.as_mut()).await, Ok(64));
        assert_eq!(buffer.as_slice(), meta_as_file.root_dir.hash.to_string().as_bytes());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_write_at() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(File::write_at(&meta_as_file, 0, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_append() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(File::append(&meta_as_file, &[]).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_truncate() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(File::truncate(&meta_as_file, 0).await, Err(zx::Status::NOT_SUPPORTED));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_buffer() {
        let (_env, meta_as_file) = TestEnv::new().await;

        for sharing_mode in [0, VMO_FLAG_EXACT, VMO_FLAG_PRIVATE] {
            for flag in [0, VMO_FLAG_READ] {
                assert_eq!(
                    File::get_buffer(&meta_as_file, sharing_mode | flag).await.err().unwrap(),
                    zx::Status::NOT_SUPPORTED
                );
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_size() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(File::get_size(&meta_as_file).await, Ok(64));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_get_attrs() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(
            File::get_attrs(&meta_as_file).await,
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
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(
            File::set_attrs(
                &meta_as_file,
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
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(File::close(&meta_as_file).await, Ok(()));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn file_sync() {
        let (_env, meta_as_file) = TestEnv::new().await;

        assert_eq!(File::sync(&meta_as_file).await, Err(zx::Status::NOT_SUPPORTED));
    }
}
