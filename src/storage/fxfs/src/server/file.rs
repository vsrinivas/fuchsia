// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_handle::{GetProperties, ObjectHandle, WriteObjectHandle},
        object_store::{
            filesystem::SyncOptions, CachingObjectHandle, StoreObjectHandle, Timestamp,
        },
        server::{
            directory::FxDirectory,
            errors::map_to_status,
            node::{FxNode, OpenedNode},
            volume::FxVolume,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    fdio::fdio_sys::{V_IRGRP, V_IROTH, V_IRUSR, V_IWUSR, V_TYPE_FILE},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, NodeAttributes, NodeMarker},
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{self as zx, Status},
    std::sync::{
        atomic::{AtomicBool, AtomicU8, AtomicUsize, Ordering},
        Arc,
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

// When the top bit of the open count is set, it means the file has been deleted and when the count
// drops to zero, it will be tombstoned.  Once it has dropped to zero, it cannot be opened again
// (assertions will fire).
const PURGED: usize = 1 << (usize::BITS - 1);

/// FxFile represents an open connection to a file.
pub struct FxFile {
    handle: CachingObjectHandle<FxVolume>,
    open_count: AtomicUsize,
    has_written: AtomicBool,
}

impl FxFile {
    pub fn new(handle: StoreObjectHandle<FxVolume>) -> Arc<Self> {
        let file = Arc::new(Self {
            handle: CachingObjectHandle::new(handle),
            open_count: AtomicUsize::new(0),
            has_written: AtomicBool::new(false),
        });
        file.handle.owner().pager().register_file(&file);
        file
    }

    pub fn open_count(&self) -> usize {
        self.open_count.load(Ordering::Relaxed)
    }

    pub fn create_connection(
        this: OpenedNode<FxFile>,
        scope: ExecutionScope,
        flags: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        FileConnection::<FxFile>::create_connection(
            // Note readable/writable/executable do not override what's set in flags, they merely
            // tell the FileConnection which set of rights the file can be opened as.
            scope.clone(),
            connection::util::OpenFile::new(this.take(), scope),
            flags,
            server_end,
            /*readable=*/ true,
            /*writable=*/ true,
            /*executable=*/ false,
        );
    }

    /// Marks the file as being purged.  Returns true if there are no open references.
    pub fn mark_purged(&self) -> bool {
        let mut old = self.open_count.load(Ordering::Relaxed);
        loop {
            assert_eq!(old & PURGED, 0);
            match self.open_count.compare_exchange_weak(
                old,
                old | PURGED,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                Ok(_) => return old == 0,
                Err(x) => old = x,
            }
        }
    }

    pub fn vmo(&self) -> &zx::Vmo {
        self.handle.data_buffer().vmo()
    }

    pub fn page_in(&self, range: std::ops::Range<u64>) {
        // TODO(csuter): For now just always respond with a filled buffer.  This obviously needs
        // to be replaced with something that does a read.
        let len = range.end - range.start;
        let vmo = zx::Vmo::create(len).unwrap();
        static COUNTER: AtomicU8 = AtomicU8::new(1);
        let fill = vec![COUNTER.fetch_add(1, Ordering::Relaxed); len as usize];
        vmo.write(&fill, 0).unwrap();
        self.handle.owner().pager().supply_pages(self.vmo(), range, &vmo, 0);
    }

    // Called by the pager to indicate there are no more VMO references.
    pub fn on_zero_children(&self) {
        // Drop the open count that we took in get_pageable_vmo.
        self.open_count_sub_one();
    }

    // Returns a VMO handle that supports paging.
    pub fn get_pageable_vmo(self: &Arc<Self>) -> Result<zx::Vmo, Error> {
        let vmo = self.handle.data_buffer().vmo();
        let vmo =
            vmo.create_child(zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE, 0, vmo.get_size()?)?;
        if self.handle.owner().pager().start_servicing(self.object_id())? {
            // Take an open count so that we keep this object alive if it is unlinked.
            self.open_count_add_one();
        }
        Ok(vmo)
    }
}

impl Drop for FxFile {
    fn drop(&mut self) {
        let volume = self.handle.owner();
        volume.cache().remove(self);
        volume.pager().unregister_file(self);
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

    fn try_into_directory_entry(self: Arc<Self>) -> Option<Arc<dyn DirectoryEntry>> {
        Some(self)
    }

    fn open_count_add_one(&self) {
        let old = self.open_count.fetch_add(1, Ordering::Relaxed);
        assert!(old != PURGED && old != PURGED - 1);
    }

    fn open_count_sub_one(&self) {
        let old = self.open_count.fetch_sub(1, Ordering::Relaxed);
        assert!(old & !PURGED > 0);
        if old == PURGED + 1 {
            let store = self.handle.store();
            store
                .filesystem()
                .object_manager()
                .graveyard()
                .unwrap()
                .queue_tombstone(store.store_object_id(), self.object_id());
        }
    }
}

impl DirectoryEntry for FxFile {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        _mode: u32,
        path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if !path.is_empty() {
            send_on_open_with_error(flags, server_end, Status::NOT_FILE);
            return;
        }
        Self::create_connection(OpenedNode::new(self), scope, flags, server_end);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(self.object_id(), fio::DIRENT_TYPE_FILE)
    }

    fn can_hardlink(&self) -> bool {
        true
    }
}

#[async_trait]
impl File for FxFile {
    async fn open(&self, _flags: u32) -> Result<(), Status> {
        Ok(())
    }

    async fn read_at(&self, offset: u64, buffer: &mut [u8]) -> Result<u64, Status> {
        let bytes_read = self.handle.read_cached(offset, buffer).await.map_err(map_to_status)?;
        Ok(bytes_read as u64)
    }

    async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, Status> {
        let _ = self
            .handle
            .write_or_append_cached(Some(offset), content)
            .await
            .map_err(map_to_status)?;
        self.has_written.store(true, Ordering::Relaxed);
        Ok(content.len() as u64)
    }

    async fn append(&self, content: &[u8]) -> Result<(u64, u64), Status> {
        let size =
            self.handle.write_or_append_cached(None, content).await.map_err(map_to_status)?;
        self.has_written.store(true, Ordering::Relaxed);
        Ok((content.len() as u64, size))
    }

    async fn truncate(&self, length: u64) -> Result<(), Status> {
        self.handle.truncate(length).await.map_err(map_to_status)?;
        self.has_written.store(true, Ordering::Relaxed);
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
        let props = self.handle.get_properties().await.map_err(map_to_status)?;
        // TODO(jfsulliv): This assumes that we always get the data attribute at index 0 of
        // |attribute_sizes|.
        Ok(NodeAttributes {
            mode: V_TYPE_FILE | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH,
            id: self.handle.object_id(),
            content_size: props.data_attribute_size,
            storage_size: props.allocated_size,
            link_count: props.refs,
            creation_time: props.creation_time.as_nanos(),
            modification_time: props.modification_time.as_nanos(),
        })
    }

    async fn set_attrs(&self, flags: u32, attrs: NodeAttributes) -> Result<(), Status> {
        let crtime = if flags & fidl_fuchsia_io::NODE_ATTRIBUTE_FLAG_CREATION_TIME > 0 {
            Some(Timestamp::from_nanos(attrs.creation_time))
        } else {
            None
        };
        let mtime = if flags & fidl_fuchsia_io::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME > 0 {
            Some(Timestamp::from_nanos(attrs.modification_time))
        } else {
            None
        };
        if let (None, None) = (crtime.as_ref(), mtime.as_ref()) {
            return Ok(());
        }
        self.handle.write_timestamps(crtime, mtime).await.map_err(map_to_status)?;
        self.has_written.store(true, Ordering::Relaxed);
        Ok(())
    }

    async fn close(&self) -> Result<(), Status> {
        if self.has_written.load(Ordering::Relaxed) {
            if let Err(e) = self.handle.flush().await {
                log::warn!("{} Flush failed: {:?}", self.object_id(), e);
            }
        }
        self.open_count_sub_one();
        Ok(())
    }

    async fn sync(&self) -> Result<(), Status> {
        self.handle.flush().await.map_err(map_to_status)?;
        // TODO(csuter): at the moment, this doesn't send a flush to the device, which doesn't
        // match minfs.
        self.handle.store().filesystem().sync(SyncOptions::default()).await.map_err(map_to_status)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::FxFile,
        crate::{
            object_handle::INVALID_OBJECT_ID,
            object_store::{filesystem::Filesystem, ObjectDescriptor},
            server::testing::{close_file_checked, open_file_checked, TestFixture},
        },
        fidl_fuchsia_io::{
            self as fio, SeekOrigin, MODE_TYPE_FILE, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
            OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fidl_fuchsia_io2::UnlinkOptions,
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        futures::join,
        io_util::{read_file_bytes, write_file_bytes},
        std::{
            sync::{
                atomic::{self, AtomicBool},
                Arc,
            },
            time::Duration,
        },
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_file() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file =
            open_file_checked(&root, OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
                .await;

        let (status, buf) = file.read(fio::MAX_BUF).await.expect("FIDL call failed");
        Status::ok(status).expect("read failed");
        assert!(buf.is_empty());

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        // TODO(jfsulliv): Check mode
        assert_ne!(attrs.id, INVALID_OBJECT_ID);
        assert_eq!(attrs.content_size, 0u64);
        assert_eq!(attrs.storage_size, 0u64);
        assert_eq!(attrs.link_count, 1u64);
        assert_ne!(attrs.creation_time, 0u64);
        assert_ne!(attrs.modification_time, 0u64);
        assert_eq!(attrs.creation_time, attrs.modification_time);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_attrs() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await;

        let (status, initial_attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");

        let crtime = initial_attrs.creation_time ^ 1u64;
        let mtime = initial_attrs.modification_time ^ 1u64;

        let mut attrs = initial_attrs.clone();
        attrs.creation_time = crtime;
        attrs.modification_time = mtime;
        let status = file
            .set_attr(fidl_fuchsia_io::NODE_ATTRIBUTE_FLAG_CREATION_TIME, &mut attrs)
            .await
            .expect("FIDL call failed");
        Status::ok(status).expect("set_attr failed");

        let mut expected_attrs = initial_attrs.clone();
        expected_attrs.creation_time = crtime; // Only crtime is updated so far.
        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(expected_attrs, attrs);

        let mut attrs = initial_attrs.clone();
        attrs.creation_time = 0u64; // This should be ignored since we don't set the flag.
        attrs.modification_time = mtime;
        let status = file
            .set_attr(fidl_fuchsia_io::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME, &mut attrs)
            .await
            .expect("FIDL call failed");
        Status::ok(status).expect("set_attr failed");

        let mut expected_attrs = initial_attrs.clone();
        expected_attrs.creation_time = crtime;
        expected_attrs.modification_time = mtime;
        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(expected_attrs, attrs);

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

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(attrs.content_size, expected_output.as_bytes().len() as u64);
        // We haven't synced yet, but the pending writes should have blocks reserved still.
        assert_eq!(attrs.storage_size, fixture.fs().block_size() as u64);

        let status = file.sync().await.expect("FIDL call failed");
        Status::ok(status).expect("sync failed");

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(attrs.content_size, expected_output.as_bytes().len() as u64);
        assert_eq!(attrs.storage_size, fixture.fs().block_size() as u64);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_writes_persist() {
        let mut device = DeviceHolder::new(FakeDevice::new(8192, 512));
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

            let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
            Status::ok(status).expect("get_attr failed");
            assert_eq!(attrs.content_size, 8192u64);
            assert_eq!(attrs.storage_size, 8192u64);

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
            close_file_checked(file).await;
        }

        let file = open_file_checked(&root, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo").await;
        let (status, buf) = file.read_at(fio::MAX_BUF, 0).await.expect("FIDL call failed");
        Status::ok(status).expect("File read was successful");
        assert_eq!(buf.len(), expected_output.as_bytes().len());
        assert_eq!(&buf[..], expected_output.as_bytes());

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(attrs.content_size, expected_output.as_bytes().len() as u64);
        assert_eq!(attrs.storage_size, fixture.fs().block_size() as u64);

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

    #[fasync::run(10, test)]
    async fn test_unlink_with_open_race() {
        let fixture = Arc::new(TestFixture::new().await);
        let fixture1 = fixture.clone();
        let fixture2 = fixture.clone();
        let fixture3 = fixture.clone();
        let done = Arc::new(AtomicBool::new(false));
        let done1 = done.clone();
        let done2 = done.clone();
        join!(
            fasync::Task::spawn(async move {
                let root = fixture1.root();
                while !done1.load(atomic::Ordering::Relaxed) {
                    let file = open_file_checked(
                        &root,
                        OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                        MODE_TYPE_FILE,
                        "foo",
                    )
                    .await;
                    file.write(b"hello").await.expect("write failed");
                }
            }),
            fasync::Task::spawn(async move {
                let root = fixture2.root();
                while !done2.load(atomic::Ordering::Relaxed) {
                    let file = open_file_checked(
                        &root,
                        OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                        MODE_TYPE_FILE,
                        "foo",
                    )
                    .await;
                    file.write(b"hello").await.expect("write failed");
                }
            }),
            fasync::Task::spawn(async move {
                let root = fixture3.root();
                for _ in 0..300 {
                    let file = open_file_checked(
                        &root,
                        OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                        MODE_TYPE_FILE,
                        "foo",
                    )
                    .await;
                    assert_eq!(file.close().await.expect("FIDL call failed"), 0);
                    root.unlink2("foo", UnlinkOptions::EMPTY)
                        .await
                        .expect("FIDL call failed")
                        .expect("unlink failed");
                }
                done.store(true, atomic::Ordering::Relaxed);
            })
        );

        Arc::try_unwrap(fixture).unwrap_or_else(|_| panic!()).close().await;
    }

    #[fasync::run(10, test)]
    async fn test_pager() {
        let fixture = TestFixture::new().await;

        {
            let root = fixture.root();
            let file_proxy = open_file_checked(
                &root,
                OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_FILE,
                "foo",
            )
            .await;
            assert_eq!(file_proxy.truncate(100000).await.expect("truncate fidl failed"), 0);

            let (status, attr) = file_proxy.get_attr().await.expect("get_attr fidl failed");
            assert_eq!(status, 0);

            let file = fixture
                .volume()
                .get_or_load_node(attr.id, ObjectDescriptor::File, None)
                .await
                .expect("get_or_load_node failed")
                .into_any()
                .downcast::<FxFile>()
                .unwrap();

            let vmo = file.get_pageable_vmo().expect("get_pageable_vmo failed");
            let mut buf = [0; 100];
            vmo.read(&mut buf, 0).expect("read failed");

            assert_eq!(buf, [1; 100]);

            // Now if we drop all other references, we should still be able to read from the VMO.
            std::mem::drop(file_proxy);
            std::mem::drop(file);

            buf.fill(0);
            vmo.read(&mut buf, 90000).expect("read failed");

            assert_eq!(buf, [2; 100]);

            // Reading from the first page shouldn't trigger another page in.
            vmo.read(&mut buf, 0).expect("read failed");
            assert_eq!(buf, [1; 100]);

            // If we close the VMO now, it should cause the file to be dropped so that if we read it
            // again, we should see another page-in.
            std::mem::drop(vmo);

            let mut pause = 100;
            loop {
                {
                    let file = fixture
                        .volume()
                        .get_or_load_node(attr.id, ObjectDescriptor::File, None)
                        .await
                        .expect("get_or_load_node failed")
                        .into_any()
                        .downcast::<FxFile>()
                        .unwrap();

                    let vmo = file.get_pageable_vmo().expect("get_pageable_vmo failed");
                    let mut buf = [0; 100];
                    vmo.read(&mut buf, 0).expect("read failed");

                    if buf == [3; 100] {
                        break;
                    }
                }

                // The NO_CHILDREN message is asynchronous, so all we can do is sleep and try again.
                fasync::Timer::new(Duration::from_millis(pause)).await;
                pause *= 2;
            }
        }

        fixture.close().await;
    }
}
