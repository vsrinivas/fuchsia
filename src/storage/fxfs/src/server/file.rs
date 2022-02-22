// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        async_enter,
        object_handle::{GetProperties, ObjectHandle, ReadObjectHandle, WriteObjectHandle},
        object_store::{
            filesystem::SyncOptions, CachingObjectHandle, StoreObjectHandle, Timestamp,
        },
        round::{round_down, round_up},
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
    fidl_fuchsia_io::{self as fio, FilesystemInfo, NodeAttributes, NodeMarker},
    fidl_fuchsia_mem::Buffer,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, Status},
    futures::{channel::oneshot, join},
    once_cell::sync::Lazy,
    std::sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc, Mutex,
    },
    vfs::{
        common::send_on_open_with_error,
        directory::entry::{DirectoryEntry, EntryInfo},
        execution_scope::ExecutionScope,
        file::{connection::io1::FileConnection, File},
        path::Path,
    },
};

// Transfer buffers are to be used with supply_pages. supply_pages only works with pages that are
// unmapped, but we need the pages to be mapped so that we can decrypt and potentially verify
// checksums.  To keep things simple, the buffers are fixed size at 1 MiB which should cover most
// requests.
const TRANSFER_BUFFER_MAX_SIZE: u64 = 1_048_576;

// The number of transfer buffers we support.
const TRANSFER_BUFFER_COUNT: u64 = 8;

struct TransferBuffers {
    vmo: zx::Vmo,
    free_list: Mutex<Vec<u64>>,
    event: event_listener::Event,
}

impl TransferBuffers {
    fn new() -> Self {
        const VMO_SIZE: u64 = TRANSFER_BUFFER_COUNT * TRANSFER_BUFFER_MAX_SIZE;
        Self {
            vmo: zx::Vmo::create(VMO_SIZE).unwrap(),
            free_list: Mutex::new(
                (0..VMO_SIZE).step_by(TRANSFER_BUFFER_MAX_SIZE as usize).collect(),
            ),
            event: event_listener::Event::new(),
        }
    }

    async fn get(&self) -> TransferBuffer<'_> {
        loop {
            let listener = self.event.listen();
            if let Some(offset) = self.free_list.lock().unwrap().pop() {
                return TransferBuffer { buffers: self, offset };
            }
            listener.await;
        }
    }
}

struct TransferBuffer<'a> {
    buffers: &'a TransferBuffers,

    // The offset this buffer starts at in the VMO.
    offset: u64,
}

impl TransferBuffer<'_> {
    fn vmo(&self) -> &zx::Vmo {
        &self.buffers.vmo
    }

    fn offset(&self) -> u64 {
        self.offset
    }

    // Allocating pages in the kernel is time-consuming, so it can help to commit pages first,
    // whilst other work is occurring in the background, and then copy later which is relatively
    // fast.
    fn commit(&self, size: u64) {
        let _ignore_error = self.buffers.vmo.op_range(
            zx::VmoOp::COMMIT,
            self.offset,
            std::cmp::min(size, TRANSFER_BUFFER_MAX_SIZE),
        );
    }
}

impl Drop for TransferBuffer<'_> {
    fn drop(&mut self) {
        self.buffers.free_list.lock().unwrap().push(self.offset);
        self.buffers.event.notify(1);
    }
}

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

    pub async fn create_connection(
        this: OpenedNode<FxFile>,
        scope: ExecutionScope,
        flags: u32,
        server_end: ServerEnd<NodeMarker>,
        shutdown: oneshot::Receiver<()>,
    ) {
        FileConnection::<FxFile>::create_connection_async(
            // Note readable/writable/executable do not override what's set in flags, they merely
            // tell the FileConnection which set of rights the file can be opened as.
            scope.clone(),
            this.take(),
            flags,
            server_end,
            /*readable=*/ true,
            /*writable=*/ true,
            /*executable=*/ false,
            shutdown,
        )
        .await;
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

    pub fn page_in(self: &Arc<Self>, mut range: std::ops::Range<u64>) {
        const ZERO_VMO_SIZE: u64 = 1024 * 1024;
        static ZERO_VMO: Lazy<zx::Vmo> = Lazy::new(|| zx::Vmo::create(ZERO_VMO_SIZE).unwrap());

        let vmo = self.vmo();
        // We have to check the VMO's size as well as size from the handle because the object gets
        // updated *after* the VMO's size changes and whilst the kernel locking will mean that this
        // request is guaranteed to be within bounds, with read-ahead, we need to be careful not to
        // extend the range so that it's out-of-bounds.
        let aligned_size = std::cmp::min(
            round_up(self.handle.uncached_size(), zx::system_get_page_size()).unwrap(),
            vmo.get_size().unwrap(),
        );
        let mut offset = std::cmp::max(range.start, aligned_size);
        while offset < range.end {
            let end = std::cmp::min(range.end, offset + ZERO_VMO_SIZE);
            self.handle.owner().pager().supply_pages(vmo, offset..end, &ZERO_VMO, 0);
            offset = end;
        }
        if aligned_size < range.end {
            range.end = aligned_size;
        } else {
            const READ_AHEAD: u64 = 131_072;
            if aligned_size - range.end < READ_AHEAD {
                range.end = aligned_size;
            } else {
                range.end += READ_AHEAD;
            }
        }
        if range.end <= range.start {
            return;
        }
        range.start = round_down(range.start, self.handle.block_size());
        let this = self.clone();
        // TODO(fxbug.dev/89444): Handle IO errors.
        fasync::Task::spawn_on(self.handle.owner().executor(), async move {
            async_enter!("page_in");
            static TRANSFER_BUFFERS: Lazy<TransferBuffers> = Lazy::new(|| TransferBuffers::new());
            let (buffer, transfer_buffer) = join!(
                async {
                    this.handle.read_uncached(range.clone()).await.expect("TODO handle errors")
                },
                async {
                    let buffer = TRANSFER_BUFFERS.get().await;
                    // Committing pages in the kernel is time consuming, so we do this in parallel
                    // to the read.  This assumes that the implementation of join! polls the other
                    // future first (which happens to be the case for now).
                    buffer.commit(range.end - range.start);
                    buffer
                }
            );
            let mut buf = buffer.as_slice();
            while !buf.is_empty() {
                let (source, remainder) =
                    buf.split_at(std::cmp::min(buf.len(), TRANSFER_BUFFER_MAX_SIZE as usize));
                buf = remainder;
                transfer_buffer
                    .vmo()
                    .write(source, transfer_buffer.offset())
                    .expect("TODO handle errors");
                this.handle.owner().pager().supply_pages(
                    this.vmo(),
                    range.start..range.start + source.len() as u64,
                    transfer_buffer.vmo(),
                    transfer_buffer.offset(),
                );
                range.start += source.len() as u64;
            }
        })
        .detach();
    }

    // Called by the pager to indicate there are no more VMO references.
    pub fn on_zero_children(&self) {
        // Drop the open count that we took in `get_buffer`.
        self.open_count_sub_one();
    }

    pub async fn flush(&self) -> Result<(), Error> {
        self.handle.flush().await
    }

    pub fn get_block_size(&self) -> u64 {
        self.handle.block_size()
    }

    pub async fn is_allocated(&self, start_offset: u64) -> Result<(bool, u64), Status> {
        self.handle.uncached_handle().is_allocated(start_offset).await.map_err(map_to_status)
    }

    // TODO(fxbug.dev/89873): might be better to have a cached/uncached mode for file and call
    // this when in uncached mode
    pub async fn write_at_uncached(&self, offset: u64, content: &[u8]) -> Result<u64, Status> {
        let mut buf = self.handle.uncached_handle().allocate_buffer(content.len());
        buf.as_mut_slice().copy_from_slice(content);
        let _ = self
            .handle
            .uncached_handle()
            .write_or_append(Some(offset), buf.as_ref())
            .await
            .map_err(map_to_status)?;
        self.has_written.store(true, Ordering::Relaxed);
        Ok(content.len() as u64)
    }

    // TODO(fxbug.dev/89873): might be better to have a cached/uncached mode for file and call
    // this when in uncached mode
    pub async fn read_at_uncached(&self, offset: u64, buffer: &mut [u8]) -> Result<u64, Status> {
        let mut buf = self.handle.uncached_handle().allocate_buffer(buffer.len());
        buf.as_mut_slice().fill(0);
        let bytes_read = self
            .handle
            .uncached_handle()
            .read(offset, buf.as_mut())
            .await
            .map_err(map_to_status)?;
        buffer.copy_from_slice(buf.as_slice());
        Ok(bytes_read as u64)
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
                .graveyard()
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
        scope.clone().spawn_with_shutdown(move |shutdown| {
            Self::create_connection(OpenedNode::new(self), scope, flags, server_end, shutdown)
        });
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(self.object_id(), fio::DIRENT_TYPE_FILE)
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

    // Returns a VMO handle that supports paging.
    async fn get_buffer(&self, flags: u32) -> Result<Buffer, Status> {
        // We do not support exact/duplicate sharing mode.
        if flags & fio::VMO_FLAG_EXACT != 0 {
            log::error!("get_buffer does not support exact sharing mode!");
            return Err(Status::NOT_SUPPORTED);
        }
        // We only support the combination of VMO_FLAG_WRITE when a private COW clone is
        // explicitly specified.  This implicitly restricts any mmap call that attempts
        // to use MAP_SHARED + PROT_WRITE.
        if flags & fio::VMO_FLAG_WRITE != 0 && flags & fio::VMO_FLAG_PRIVATE == 0 {
            log::error!("get_buffer only supports VMO_FLAG_WRITE with VMO_FLAG_PRIVATE!");
            return Err(Status::NOT_SUPPORTED);
        }
        // We do not support executable VMO handles.
        if flags & fio::VMO_FLAG_EXEC != 0 {
            log::error!("get_buffer does not support execute rights!");
            return Err(Status::NOT_SUPPORTED);
        }

        let vmo = self.handle.data_buffer().vmo();
        let size = vmo.get_size()?;

        let mut child_options = zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE;
        // By default, SNAPSHOT includes WRITE, so we explicitly remove it if not required.
        if flags & fio::VMO_FLAG_WRITE == 0 {
            child_options |= zx::VmoChildOptions::NO_WRITE
        }

        let child_vmo = vmo.create_child(child_options, 0, size)?;
        if self.handle.owner().pager().start_servicing(self.object_id()).map_err(map_to_status)? {
            // Take an open count so that we keep this object alive if it is unlinked.
            self.open_count_add_one();
        }
        Ok(Buffer { vmo: child_vmo, size })
    }

    async fn get_size(&self) -> Result<u64, Status> {
        Ok(self.handle.get_size())
    }

    async fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        let props = self.handle.get_properties().await.map_err(map_to_status)?;
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
            self.handle.flush().await.map_err(map_to_status)?;
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

    fn query_filesystem(&self) -> Result<FilesystemInfo, Status> {
        let store = self.handle.store();
        Ok(store
            .filesystem()
            .get_info()
            .to_filesystem_info(store.object_count(), self.handle.owner().id()))
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            object_handle::INVALID_OBJECT_ID,
            object_store::filesystem::Filesystem,
            server::testing::{close_file_checked, open_file_checked, TestFixture},
        },
        fdio::fdio_sys::{V_IRGRP, V_IROTH, V_IRUSR, V_IWUSR, V_TYPE_FILE},
        fidl_fuchsia_io::{
            self as fio, SeekOrigin, UnlinkOptions, MODE_TYPE_FILE, OPEN_FLAG_APPEND,
            OPEN_FLAG_CREATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        futures::join,
        io_util::{read_file_bytes, write_file_bytes},
        std::sync::{
            atomic::{self, AtomicBool},
            Arc,
        },
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run(10, test)]
    async fn test_empty_file() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file =
            open_file_checked(&root, OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
                .await;

        let buf = file
            .read(fio::MAX_BUF)
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("read failed");
        assert!(buf.is_empty());

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_ne!(attrs.id, INVALID_OBJECT_ID);
        assert_eq!(attrs.mode, V_TYPE_FILE | V_IRUSR | V_IWUSR | V_IRGRP | V_IROTH);
        assert_eq!(attrs.content_size, 0u64);
        assert_eq!(attrs.storage_size, 0u64);
        assert_eq!(attrs.link_count, 1u64);
        assert_ne!(attrs.creation_time, 0u64);
        assert_ne!(attrs.modification_time, 0u64);
        assert_eq!(attrs.creation_time, attrs.modification_time);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
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

    #[fasync::run(10, test)]
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
            let bytes_written = file
                .write(input.as_bytes())
                .await
                .expect("write failed")
                .map_err(Status::from_raw)
                .expect("File write was successful");
            assert_eq!(bytes_written as usize, input.as_bytes().len());
        }

        let buf = file
            .read_at(fio::MAX_BUF, 0)
            .await
            .expect("read_at failed")
            .map_err(Status::from_raw)
            .expect("File read was successful");
        assert_eq!(buf.len(), expected_output.as_bytes().len());
        assert!(buf.iter().eq(expected_output.as_bytes().iter()));

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(attrs.content_size, expected_output.as_bytes().len() as u64);
        // We haven't synced yet, but the pending writes should have blocks reserved still.
        assert_eq!(attrs.storage_size, fixture.fs().block_size() as u64);

        let () = file
            .sync()
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("sync failed");

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(attrs.content_size, expected_output.as_bytes().len() as u64);
        assert_eq!(attrs.storage_size, fixture.fs().block_size() as u64);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
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
                let _: u64 = file
                    .write(&vec![0xaa as u8; 8192])
                    .await
                    .expect("FIDL call failed")
                    .map_err(Status::from_raw)
                    .expect("File write was successful");
            } else {
                let buf = file
                    .read(8192)
                    .await
                    .expect("FIDL call failed")
                    .map_err(Status::from_raw)
                    .expect("File read was successful");
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

    #[fasync::run(10, test)]
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

            let bytes_written = file
                .write(input.as_bytes())
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("File write was successful");
            assert_eq!(bytes_written as usize, input.as_bytes().len());
            close_file_checked(file).await;
        }

        let file = open_file_checked(&root, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo").await;
        let buf = file
            .read_at(fio::MAX_BUF, 0)
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("File read was successful");
        assert_eq!(buf.len(), expected_output.as_bytes().len());
        assert_eq!(&buf[..], expected_output.as_bytes());

        let (status, attrs) = file.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(attrs.content_size, expected_output.as_bytes().len() as u64);
        assert_eq!(attrs.storage_size, fixture.fs().block_size() as u64);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
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
        let _: u64 = file
            .write(input.as_bytes())
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("File write was successful");

        {
            let offset = file
                .seek(SeekOrigin::Start, 0)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("seek was successful");
            assert_eq!(offset, 0);
            let buf = file
                .read(5)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("File read was successful");
            assert!(buf.iter().eq("hello".as_bytes().into_iter()));
        }
        {
            let offset = file
                .seek(SeekOrigin::Current, 2)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("seek was successful");
            assert_eq!(offset, 7);
            let buf = file
                .read(5)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("File read was successful");
            assert!(buf.iter().eq("world".as_bytes().into_iter()));
        }
        {
            let offset = file
                .seek(SeekOrigin::Current, -5)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("seek was successful");
            assert_eq!(offset, 7);
            let buf = file
                .read(5)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("File read was successful");
            assert!(buf.iter().eq("world".as_bytes().into_iter()));
        }
        {
            let offset = file
                .seek(SeekOrigin::End, -1)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("seek was successful");
            assert_eq!(offset, 12);
            let buf = file
                .read(1)
                .await
                .expect("FIDL call failed")
                .map_err(Status::from_raw)
                .expect("File read was successful");
            assert!(buf.iter().eq("!".as_bytes().into_iter()));
        }

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
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

        let _: u64 = file
            .write(input.as_bytes())
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("File write was successful");

        let offset = file
            .seek(SeekOrigin::Start, 0)
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("Seek was successful");
        assert_eq!(offset, 0);

        let status = file.truncate(len as u64).await.expect("FIDL call failed");
        Status::ok(status).expect("File truncate was successful");

        let mut expected_buf = vec![0 as u8; len];
        expected_buf[..input.as_bytes().len()].copy_from_slice(input.as_bytes());

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), len);
        assert_eq!(buf, expected_buf);

        // Write something at the end of the gap.
        expected_buf[len - 1..].copy_from_slice("a".as_bytes());

        let _: u64 = file
            .write_at("a".as_bytes(), (len - 1) as u64)
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("File write was successful");

        let offset = file
            .seek(SeekOrigin::Start, 0)
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("Seek was successful");
        assert_eq!(offset, 0);

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), len);
        assert_eq!(buf, expected_buf);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
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

        let offset = file
            .seek(SeekOrigin::Start, 0)
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("Seek was successful");
        assert_eq!(offset, 0);

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

        let offset = file
            .seek(SeekOrigin::Start, 0)
            .await
            .expect("seek failed")
            .map_err(Status::from_raw)
            .expect("Seek was successful");
        assert_eq!(offset, 0);

        let buf = read_file_bytes(&file).await.expect("File read was successful");
        assert_eq!(buf.len(), len);
        assert_eq!(buf, expected_buf);

        close_file_checked(file).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
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

        let offset = file
            .seek(SeekOrigin::Start, 0)
            .await
            .expect("truncate failed")
            .map_err(Status::from_raw)
            .expect("Seek was successful");
        assert_eq!(offset, 0);

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

        let offset = file
            .seek(SeekOrigin::Start, 0)
            .await
            .expect("seek failed")
            .map_err(Status::from_raw)
            .expect("Seek was successful");
        assert_eq!(offset, 0);

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
                    let _: u64 = file
                        .write(b"hello")
                        .await
                        .expect("write failed")
                        .map_err(Status::from_raw)
                        .expect("write error");
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
                    let _: u64 = file
                        .write(b"hello")
                        .await
                        .expect("write failed")
                        .map_err(Status::from_raw)
                        .expect("write error");
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
                    assert_eq!(
                        file.close().await.expect("FIDL call failed").map_err(Status::from_raw),
                        Ok(())
                    );
                    root.unlink("foo", UnlinkOptions::EMPTY)
                        .await
                        .expect("FIDL call failed")
                        .expect("unlink failed");
                }
                done.store(true, atomic::Ordering::Relaxed);
            })
        );

        Arc::try_unwrap(fixture).unwrap_or_else(|_| panic!()).close().await;
    }
}
