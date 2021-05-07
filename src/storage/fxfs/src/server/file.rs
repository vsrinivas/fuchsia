// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_handle::{ObjectHandle, ObjectHandleExt},
        object_store::StoreObjectHandle,
        server::{directory::FxDirectory, errors::map_to_status, node::FxNode, volume::FxVolume},
    },
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, NodeAttributes, NodeMarker},
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::Status,
    std::{
        any::Any,
        sync::{
            atomic::{AtomicUsize, Ordering},
            Arc,
        },
    },
    vfs::{
        common::send_on_open_with_error,
        directory::entry::{DirectoryEntry, EntryInfo},
        execution_scope::ExecutionScope,
        file::{
            connection::{self, io1::FileConnection},
            File, SharingMode,
        },
        path::Path,
    },
};

/// FxFile represents an open connection to a file.
pub struct FxFile {
    handle: StoreObjectHandle<FxVolume>,
    open_count: AtomicUsize,
}

impl FxFile {
    pub fn new(handle: StoreObjectHandle<FxVolume>) -> Self {
        Self { handle, open_count: AtomicUsize::new(0) }
    }
    pub fn open_count(&self) -> usize {
        self.open_count.load(Ordering::Relaxed)
    }
}

impl Drop for FxFile {
    fn drop(&mut self) {
        self.handle.owner().cache().remove(self.object_id());
    }
}

impl FxNode for FxFile {
    fn object_id(&self) -> u64 {
        self.handle.object_id()
    }
    fn parent(&self) -> Option<Arc<FxDirectory>> {
        unreachable!(); // Add a parent back-reference if needed.
    }
    fn set_parent(&self, _parent: Arc<FxDirectory>) {
        // NOP
    }
    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync + 'static> {
        self
    }
}

impl DirectoryEntry for FxFile {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if !path.is_empty() {
            send_on_open_with_error(flags, server_end, Status::NOT_FILE);
            return;
        }
        // Since close decrements open_count, we need to increment it here as we create OpenFile
        // since it will call close when dropped.
        self.open_count.fetch_add(1, Ordering::Relaxed);
        FileConnection::<FxFile>::create_connection(
            // Note readable/writable do not override what's set in flags, they merely tell the
            // FileConnection that it's valid to open the file readable/writable.
            scope.clone(),
            connection::util::OpenFile::new(self, scope),
            flags,
            mode,
            server_end,
            /*readable=*/ true,
            /*writable=*/ true,
        );
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl File for FxFile {
    async fn open(&self, _flags: u32) -> Result<(), Status> {
        Ok(())
    }

    async fn read_at(&self, offset: u64, buffer: &mut [u8]) -> Result<u64, Status> {
        let mut buf = self.handle.allocate_buffer(buffer.len() as usize);
        let bytes_read = self.handle.read(offset, buf.as_mut()).await.map_err(map_to_status)?;
        &mut buffer[..bytes_read].copy_from_slice(&buf.as_slice()[..bytes_read]);
        Ok(bytes_read as u64)
    }

    async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, Status> {
        // TODO(jfsulliv): Update mtime.
        let mut buf = self.handle.allocate_buffer(content.len());
        buf.as_mut_slice()[..content.len()].copy_from_slice(content);
        self.handle.write(offset, buf.as_ref()).await.map_err(map_to_status)?;
        Ok(content.len() as u64)
    }

    async fn append(&self, content: &[u8]) -> Result<(u64, u64), Status> {
        // TODO(jfsulliv): this needs to be made atomic. We already lock at the Device::write level
        // but we need to lift a lock higher.
        let offset = self.handle.get_size();
        let bytes_written = self.write_at(offset, content).await?;
        Ok((bytes_written, offset + bytes_written))
    }

    async fn truncate(&self, length: u64) -> Result<(), Status> {
        // TODO(jfsulliv): Update mtime.
        let mut transaction = self.handle.new_transaction().await.map_err(map_to_status)?;
        self.handle.truncate(&mut transaction, length).await.map_err(map_to_status)?;
        transaction.commit().await;
        Ok(())
    }

    async fn get_buffer(&self, _mode: SharingMode, _flags: u32) -> Result<Option<Buffer>, Status> {
        log::error!("get_buffer not implemented");
        Err(Status::NOT_SUPPORTED)
    }

    async fn get_size(&self) -> Result<u64, Status> {
        Ok(self.handle.get_size())
    }

    async fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        log::error!("get_attrs not implemented");
        Err(Status::NOT_SUPPORTED)
    }

    async fn set_attrs(
        &self,
        _flags: u32,
        _attrs: NodeAttributes,
        may_defer: bool,
    ) -> Result<(), Status> {
        if may_defer {
            // TODO(jfsulliv): Cache the new attributes.
            return Ok(());
        }
        log::error!("set_attrs not implemented");
        Err(Status::NOT_SUPPORTED)
    }

    async fn close(&self) -> Result<(), Status> {
        assert!(self.open_count.fetch_sub(1, Ordering::Relaxed) > 0);
        Ok(())
    }

    async fn sync(&self) -> Result<(), Status> {
        log::error!("sync not implemented");
        Err(Status::NOT_SUPPORTED)
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            device::DeviceHolder,
            server::testing::{close_file_checked, open_file_checked, TestFixture},
            testing::fake_device::FakeDevice,
        },
        fidl_fuchsia_io::{
            self as fio, SeekOrigin, MODE_TYPE_FILE, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
            OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        io_util::{read_file_bytes, write_file_bytes},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_file() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file =
            open_file_checked(&root, OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
                .await;

        let (status, buf) = file.read(fio::MAX_BUF).await.expect("FIDL call failed");
        Status::ok(status).expect("File read was successful");
        assert!(buf.is_empty());

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await;

        let inputs = vec!["hello, ", "world!"];
        let expected_output = "hello, world!";
        for input in inputs {
            let (status, bytes_written) = file.write(input.as_bytes()).await.expect("write failed");
            Status::ok(status).expect("File write was successful");
            assert_eq!(bytes_written as usize, input.as_bytes().len());
        }

        let (status, buf) = file.read_at(fio::MAX_BUF, 0).await.expect("read_at failed");
        Status::ok(status).expect("File read was successful");
        assert_eq!(buf.len(), expected_output.as_bytes().len());
        assert!(buf.iter().eq(expected_output.as_bytes().iter()));

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_writes_persist() {
        let mut device = DeviceHolder::new(FakeDevice::new(2048, 512));
        for i in 0..2 {
            let fixture = TestFixture::open(device, /*format=*/ i == 0).await;
            let root = fixture.root();

            let flags = if i == 0 {
                OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE
            } else {
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE
            };
            let file = open_file_checked(&root, flags, MODE_TYPE_FILE, "foo").await;

            if i == 0 {
                let (status, _) =
                    file.write(&vec![0xaa as u8; 8192]).await.expect("FIDL call failed");
                Status::ok(status).expect("File write was successful");
            } else {
                let (status, buf) = file.read(8192).await.expect("FIDL call failed");
                Status::ok(status).expect("File read was successful");
                assert_eq!(buf, vec![0xaa as u8; 8192]);
            }

            close_file_checked(file).await;
            device = fixture.close().await;
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_append() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let inputs = vec!["hello, ", "world!"];
        let expected_output = "hello, world!";
        for input in inputs {
            let file = open_file_checked(
                &root,
                OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_APPEND,
                MODE_TYPE_FILE,
                "foo",
            )
            .await;

            let (status, bytes_written) =
                file.write(input.as_bytes()).await.expect("FIDL call failed");
            Status::ok(status).expect("File write was successful");
            assert_eq!(bytes_written as usize, input.as_bytes().len());
        }

        let file = open_file_checked(&root, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo").await;
        let (status, buf) = file.read_at(fio::MAX_BUF, 0).await.expect("FIDL call failed");
        Status::ok(status).expect("File read was successful");
        assert_eq!(buf.len(), expected_output.as_bytes().len());
        assert!(buf.iter().eq(expected_output.as_bytes().iter()));

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_seek() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await;

        let input = "hello, world!";
        let (status, _bytes_written) =
            file.write(input.as_bytes()).await.expect("FIDL call failed");
        Status::ok(status).expect("File write was successful");

        {
            let (status, offset) = file.seek(0, SeekOrigin::Start).await.expect("FIDL call failed");
            assert_eq!(offset, 0);
            Status::ok(status).expect("seek was successful");
            let (status, buf) = file.read(5).await.expect("FIDL call failed");
            Status::ok(status).expect("File read was successful");
            assert!(buf.iter().eq("hello".as_bytes().into_iter()));
        }
        {
            let (status, offset) =
                file.seek(2, SeekOrigin::Current).await.expect("FIDL call failed");
            assert_eq!(offset, 7);
            Status::ok(status).expect("seek was successful");
            let (status, buf) = file.read(5).await.expect("FIDL call failed");
            Status::ok(status).expect("File read was successful");
            assert!(buf.iter().eq("world".as_bytes().into_iter()));
        }
        {
            let (status, offset) =
                file.seek(-5, SeekOrigin::Current).await.expect("FIDL call failed");
            assert_eq!(offset, 7);
            Status::ok(status).expect("seek was successful");
            let (status, buf) = file.read(5).await.expect("FIDL call failed");
            Status::ok(status).expect("File read was successful");
            assert!(buf.iter().eq("world".as_bytes().into_iter()));
        }
        {
            let (status, offset) = file.seek(-1, SeekOrigin::End).await.expect("FIDL call failed");
            assert_eq!(offset, 12);
            Status::ok(status).expect("seek was successful");
            let (status, buf) = file.read(1).await.expect("FIDL call failed");
            Status::ok(status).expect("File read was successful");
            assert!(buf.iter().eq("!".as_bytes().into_iter()));
        }

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_extend() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await;

        let input = "hello, world!";
        let len: usize = 16 * 1024;

        let (status, _bytes_written) =
            file.write(input.as_bytes()).await.expect("FIDL call failed");
        Status::ok(status).expect("File write was successful");

        let (status, offset) = file.seek(0, SeekOrigin::Start).await.expect("FIDL call failed");
        assert_eq!(offset, 0);
        Status::ok(status).expect("Seek was successful");

        let status = file.truncate(len as u64).await.expect("FIDL call failed");
        Status::ok(status).expect("File truncate was successful");

        let mut expected_buf = vec![0 as u8; len];
        expected_buf[..input.as_bytes().len()].copy_from_slice(input.as_bytes());

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), len);
        assert_eq!(buf, expected_buf);

        // Write something at the end of the gap.
        expected_buf[len - 1..].copy_from_slice("a".as_bytes());

        let (status, _bytes_written) =
            file.write_at("a".as_bytes(), (len - 1) as u64).await.expect("FIDL call failed");
        Status::ok(status).expect("File write was successful");

        let (status, offset) = file.seek(0, SeekOrigin::Start).await.expect("FIDL call failed");
        assert_eq!(offset, 0);
        Status::ok(status).expect("Seek was successful");

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), len);
        assert_eq!(buf, expected_buf);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_shrink() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await;

        let len: usize = 2 * 1024;
        let input = {
            let mut v = vec![0 as u8; len];
            for i in 0..v.len() {
                v[i] = ('a' as u8) + (i % 13) as u8;
            }
            v
        };
        let short_len: usize = 513;

        write_file_bytes(&file, &input).await.expect("File write was successful");

        let status = file.truncate(short_len as u64).await.expect("truncate failed");
        Status::ok(status).expect("File truncate was successful");

        let (status, offset) = file.seek(0, SeekOrigin::Start).await.expect("FIDL call failed");
        assert_eq!(offset, 0);
        Status::ok(status).expect("Seek was successful");

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), short_len);
        assert_eq!(buf, input[..short_len]);

        // Re-truncate to the original length and verify the data's zeroed.
        let status = file.truncate(len as u64).await.expect("FIDL call failed");
        Status::ok(status).expect("File truncate was successful");

        let expected_buf = {
            let mut v = vec![0 as u8; len];
            v[..short_len].copy_from_slice(&input[..short_len]);
            v
        };

        let (status, offset) = file.seek(0, SeekOrigin::Start).await.expect("seek failed");
        assert_eq!(offset, 0);
        Status::ok(status).expect("Seek was successful");

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), len);
        assert_eq!(buf, expected_buf);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_truncate_shrink_repeated() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await;

        let orig_len: usize = 4 * 1024;
        let mut len = orig_len;
        let input = {
            let mut v = vec![0 as u8; len];
            for i in 0..v.len() {
                v[i] = ('a' as u8) + (i % 13) as u8;
            }
            v
        };
        let short_len: usize = 513;

        write_file_bytes(&file, &input).await.expect("File write was successful");

        while len > short_len {
            let to_truncate = std::cmp::min(len - short_len, 512);
            len -= to_truncate;
            let status = file.truncate(len as u64).await.expect("FIDL call failed");
            Status::ok(status).expect("File truncate was successful");
            len -= to_truncate;
        }

        let (status, offset) = file.seek(0, SeekOrigin::Start).await.expect("truncate failed");
        assert_eq!(offset, 0);
        Status::ok(status).expect("Seek was successful");

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), short_len);
        assert_eq!(buf, input[..short_len]);

        // Re-truncate to the original length and verify the data's zeroed.
        let status = file.truncate(orig_len as u64).await.expect("FIDL call failed");
        Status::ok(status).expect("File truncate was successful");

        let expected_buf = {
            let mut v = vec![0 as u8; orig_len];
            v[..short_len].copy_from_slice(&input[..short_len]);
            v
        };

        let (status, offset) = file.seek(0, SeekOrigin::Start).await.expect("seek failed");
        assert_eq!(offset, 0);
        Status::ok(status).expect("Seek was successful");

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), orig_len);
        assert_eq!(buf, expected_buf);

        close_file_checked(file).await;
        fixture.close().await;
    }
}
