// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fuchsia_zircon::{self as zx, HandleBased};
use std::sync::Arc;
use syncio::{
    zxio, zxio::zxio_get_posix_mode, zxio_node_attributes_t, DirentIterator, Zxio, ZxioDirent,
    ZxioSignals,
};
use tracing::warn;

use crate::auth::FsCred;
use crate::fs::*;
use crate::lock::{Mutex, RwLockReadGuard, RwLockWriteGuard};
use crate::logging::impossible_error;
use crate::mm::MemoryAccessorExt;
use crate::task::*;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;

pub struct RemoteFs;
impl FileSystemOps for RemoteFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        const REMOTE_FS_MAGIC: u32 = u32::from_be_bytes(*b"f.io");
        Ok(statfs::default(REMOTE_FS_MAGIC))
    }

    fn generate_node_ids(&self) -> bool {
        true
    }
}

impl RemoteFs {
    pub fn new_fs(
        kernel: &Kernel,
        root: zx::Channel,
        rights: fio::OpenFlags,
    ) -> Result<FileSystemHandle, Errno> {
        let remote_node = RemoteNode::new(root.into_handle(), rights)?;
        let attrs = remote_node.zxio.attr_get().map_err(|_| errno!(EIO))?;
        let mut root_node = FsNode::new_root(remote_node);
        root_node.inode_num = attrs.id;
        Ok(FileSystem::new_with_root(kernel, RemoteFs, root_node))
    }
}

struct RemoteNode {
    /// The underlying Zircon I/O object for this remote node.
    ///
    /// We delegate to the zxio library for actually doing I/O with remote
    /// objects, including fuchsia.io.Directory and fuchsia.io.File objects.
    /// This structure lets us share code with FDIO and other Fuchsia clients.
    zxio: Arc<syncio::Zxio>,

    /// The fuchsia.io rights for the dir handle. Subdirs will be opened with
    /// the same rights.
    rights: fio::OpenFlags,
}

impl RemoteNode {
    fn new(handle: zx::Handle, rights: fio::OpenFlags) -> Result<RemoteNode, Errno> {
        let zxio = Arc::new(Zxio::create(handle).map_err(|status| from_status_like_fdio!(status))?);
        Ok(RemoteNode { zxio, rights })
    }
}

pub fn create_fuchsia_pipe(
    current_task: &CurrentTask,
    socket: zx::Socket,
    flags: OpenFlags,
) -> Result<FileHandle, Errno> {
    let ops = Box::new(RemotePipeObject::new(socket.into_handle())?);
    Ok(Anon::new_file(current_task, ops, flags))
}

fn update_into_from_attrs(info: &mut FsNodeInfo, attrs: zxio_node_attributes_t) {
    /// st_blksize is measured in units of 512 bytes.
    const BYTES_PER_BLOCK: i64 = 512;

    // TODO - store these in FsNodeState and convert on fstat
    info.size = attrs.content_size as usize;
    info.storage_size = attrs.storage_size as usize;
    info.blksize = BYTES_PER_BLOCK;
    info.link_count = attrs.link_count;
}

fn get_zxio_signals_from_events(events: FdEvents) -> zxio::zxio_signals_t {
    let mut signals = ZxioSignals::NONE;

    if events & FdEvents::POLLIN {
        signals |= ZxioSignals::READABLE;
    }
    if events & FdEvents::POLLPRI {
        signals |= ZxioSignals::OUT_OF_BAND;
    }
    if events & FdEvents::POLLOUT {
        signals |= ZxioSignals::WRITABLE;
    }
    if events & FdEvents::POLLERR {
        signals |= ZxioSignals::ERROR;
    }
    if events & FdEvents::POLLHUP {
        signals |= ZxioSignals::PEER_CLOSED;
    }
    if events & FdEvents::POLLRDHUP {
        signals |= ZxioSignals::READ_DISABLED;
    }

    signals.bits()
}

fn get_events_from_zxio_signals(signals: zxio::zxio_signals_t) -> FdEvents {
    let zxio_signals = ZxioSignals::from_bits_truncate(signals);

    let mut events = FdEvents::empty();

    if zxio_signals.intersects(ZxioSignals::READABLE) {
        events |= FdEvents::POLLIN;
    }

    if zxio_signals.intersects(ZxioSignals::OUT_OF_BAND) {
        events |= FdEvents::POLLPRI;
    }

    if zxio_signals.intersects(ZxioSignals::WRITABLE) {
        events |= FdEvents::POLLOUT;
    }

    if zxio_signals.intersects(ZxioSignals::ERROR) {
        events |= FdEvents::POLLERR;
    }

    if zxio_signals.intersects(ZxioSignals::PEER_CLOSED) {
        events |= FdEvents::POLLHUP;
    }

    if zxio_signals.intersects(ZxioSignals::READ_DISABLED) {
        events |= FdEvents::POLLRDHUP;
    }

    events
}

impl FsNodeOps for RemoteNode {
    fn create_file_ops(&self, node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        let zxio = (*self.zxio).clone().map_err(|status| from_status_like_fdio!(status))?;
        if node.is_dir() {
            return Ok(Box::new(RemoteDirectoryObject::new(zxio)));
        }

        Ok(Box::new(RemoteFileObject::new(zxio)))
    }

    fn mknod(
        &self,
        node: &FsNode,
        name: &FsStr,
        mode: FileMode,
        _dev: DeviceType,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        let name = std::str::from_utf8(name).map_err(|_| {
            warn!("bad utf8 in pathname! remote filesystems can't handle this");
            errno!(EINVAL)
        })?;
        if !mode.is_reg() {
            warn!("Can only create regular files in remotefs.");
            return error!(EINVAL, name);
        }

        let open_flags = fio::OpenFlags::CREATE
            | fio::OpenFlags::RIGHT_WRITABLE
            | fio::OpenFlags::RIGHT_READABLE;
        let zxio = Arc::new(
            self.zxio
                .open(open_flags, 0, name)
                .map_err(|status| from_status_like_fdio!(status, name))?,
        );

        // TODO: It's unfortunate to have another round-trip. We should be able
        // to set the mode based on the information we get during open.
        let attrs = zxio.attr_get().map_err(|status| from_status_like_fdio!(status, name))?;
        let ops = Box::new(RemoteNode { zxio, rights: self.rights });
        let user_perms = mode.bits() & 0o700;
        let mode = mode | FileMode::from_bits((user_perms >> 3) | (user_perms >> 6));
        let child = node.fs().create_node_with_id(ops, attrs.id, mode, FsCred::root());

        Ok(child)
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        let name = std::str::from_utf8(name).map_err(|_| {
            warn!("bad utf8 in pathname! remote filesystems can't handle this");
            errno!(EINVAL)
        })?;
        let zxio =
            Arc::new(self.zxio.open(self.rights, 0, name).map_err(|status| match status {
                // TODO: When the file is not found `PEER_CLOSED` is returned. In this case the peer
                // closed should be translated into ENOENT, so that the file may be created. This
                // logic creates a race when creating files in remote filesystems, between us and
                // any other client creating a file between here and `mknod`.
                zx::Status::PEER_CLOSED => errno!(ENOENT, name),
                status => from_status_like_fdio!(status, name),
            })?);

        // TODO: It's unfortunate to have another round-trip. We should be able
        // to set the mode based on the information we get during open.
        let attrs = zxio.attr_get().map_err(|status| from_status_like_fdio!(status))?;

        let ops = Box::new(RemoteNode { zxio, rights: self.rights });
        let mode =
            FileMode::from_bits(unsafe { zxio_get_posix_mode(attrs.protocols, attrs.abilities) });
        // Make sure the same permissions are granted to user, group, and other.
        let user_perms = mode.bits() & 0o700;
        let mode = mode | FileMode::from_bits((user_perms >> 3) | (user_perms >> 6));

        let child = node.fs().create_node_with_id(ops, attrs.id, mode, FsCred::root());

        update_into_from_attrs(&mut child.info_write(), attrs);
        Ok(child)
    }

    fn truncate(&self, _node: &FsNode, length: u64) -> Result<(), Errno> {
        self.zxio.truncate(length).map_err(|status| from_status_like_fdio!(status))
    }

    fn update_info<'a>(&self, node: &'a FsNode) -> Result<RwLockReadGuard<'a, FsNodeInfo>, Errno> {
        let attrs = self.zxio.attr_get().map_err(|status| from_status_like_fdio!(status))?;
        let mut info = node.info_write();
        update_into_from_attrs(&mut info, attrs);
        Ok(RwLockWriteGuard::downgrade(info))
    }
}

fn zxio_read(zxio: &Zxio, current_task: &CurrentTask, data: &[UserBuffer]) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data)?;
    let mut bytes = vec![0u8; total];
    let actual = zxio.read(&mut bytes).map_err(|status| from_status_like_fdio!(status))?;
    current_task.mm.write_all(data, &bytes[0..actual])?;
    Ok(actual)
}

fn zxio_read_at(
    zxio: &Zxio,
    current_task: &CurrentTask,
    offset: usize,
    data: &[UserBuffer],
) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data)?;
    let mut bytes = vec![0u8; total];
    let actual =
        zxio.read_at(offset as u64, &mut bytes).map_err(|status| from_status_like_fdio!(status))?;
    current_task.mm.write_all(data, &bytes[0..actual])?;
    Ok(actual)
}

fn zxio_write(
    zxio: &Zxio,
    current_task: &CurrentTask,
    data: &[UserBuffer],
) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data)?;
    let mut bytes = vec![0u8; total];
    current_task.mm.read_all(data, &mut bytes)?;
    let actual = zxio.write(&bytes).map_err(|status| from_status_like_fdio!(status))?;
    Ok(actual)
}

fn zxio_write_at(
    zxio: &Zxio,
    current_task: &CurrentTask,
    offset: usize,
    data: &[UserBuffer],
) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data)?;
    let mut bytes = vec![0u8; total];
    current_task.mm.read_all(data, &mut bytes)?;
    let actual =
        zxio.write_at(offset as u64, &bytes).map_err(|status| from_status_like_fdio!(status))?;
    Ok(actual)
}

pub fn zxio_wait_async(
    zxio: &Arc<Zxio>,
    waiter: &Waiter,
    events: FdEvents,
    handler: EventHandler,
    options: WaitAsyncOptions,
) -> WaitKey {
    let zxio_clone = zxio.clone();
    let signal_handler = move |signals: zx::Signals| {
        let observed_zxio_signals = zxio_clone.wait_end(signals);
        let observed_events = get_events_from_zxio_signals(observed_zxio_signals);
        handler(observed_events);
    };

    let (handle, signals) = zxio.wait_begin(get_zxio_signals_from_events(events));
    // unwrap OK here as errors are only generated from misuse
    waiter.wake_on_signals(&handle, signals, Box::new(signal_handler), options).unwrap()
}

pub fn zxio_cancel_wait(zxio: &Arc<Zxio>, waiter: &Waiter, key: WaitKey) -> bool {
    let (handle, signals) = zxio.wait_begin(ZxioSignals::NONE.bits());
    let did_cancel = waiter.cancel_signal_wait(&handle, key);
    zxio.wait_end(signals);
    did_cancel
}

fn zxio_query_events(zxio: &Arc<Zxio>) -> FdEvents {
    let signals = get_zxio_signals_from_events(FdEvents::POLLIN | FdEvents::POLLOUT);
    let (handle, signals) = zxio.wait_begin(signals);
    // Wait can error out if the remote gets closed.
    let observed_signals = match handle.wait(signals, zx::Time::INFINITE_PAST) {
        Ok(signals) => signals,
        Err(_) => return FdEvents::POLLHUP,
    };
    let observed_zxio_signals = zxio.wait_end(observed_signals);
    get_events_from_zxio_signals(observed_zxio_signals)
}

/// Helper struct to track the context necessary to iterate over dir entries.
#[derive(Default)]
struct RemoteDirectoryIterator {
    iterator: Option<DirentIterator>,

    /// If the last attempt to write to the sink failed, this contains the entry
    /// that is pending to be added.
    pending_entry: Option<ZxioDirent>,
}

impl RemoteDirectoryIterator {
    fn get_or_init_iterator(&mut self, zxio: &Zxio) -> Result<&mut DirentIterator, Errno> {
        if self.iterator.is_none() {
            let iterator =
                zxio.create_dirent_iterator().map_err(|status| from_status_like_fdio!(status))?;
            self.iterator = Some(iterator);
        }
        if let Some(iterator) = &mut self.iterator {
            return Ok(iterator);
        }

        // Should be an impossible error, because we just created the iterator above.
        error!(EIO)
    }

    /// Returns the next dir entry. If no more entries are found, returns None.
    /// Returns an error if the iterator fails for other reasons described by
    /// the zxio library.
    pub fn next(&mut self, zxio: &Zxio) -> Option<Result<ZxioDirent, Errno>> {
        match self.pending_entry.take() {
            Some(entry) => Some(Ok(entry)),
            None => {
                let iterator = match self.get_or_init_iterator(zxio) {
                    Ok(iter) => iter,
                    Err(e) => return Some(Err(e)),
                };
                let result = iterator.next();
                match result {
                    Some(Ok(v)) => Some(Ok(v)),
                    Some(Err(status)) => Some(Err(from_status_like_fdio!(status))),
                    None => None,
                }
            }
        }
    }
}

struct RemoteDirectoryObject {
    /// The underlying Zircon I/O object.
    zxio: Zxio,

    iterator: Mutex<RemoteDirectoryIterator>,
}

impl RemoteDirectoryObject {
    pub fn new(zxio: Zxio) -> RemoteDirectoryObject {
        RemoteDirectoryObject { zxio, iterator: Mutex::new(RemoteDirectoryIterator::default()) }
    }
}

impl FileOps for RemoteDirectoryObject {
    fileops_impl_directory!();

    fn seek(
        &self,
        file: &FileObject,
        _current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        let mut current_offset = file.offset.lock();
        let mut iterator = self.iterator.lock();
        let new_offset = match whence {
            SeekOrigin::Set => Some(offset),
            SeekOrigin::Cur => (*current_offset).checked_add(offset),
            SeekOrigin::End => None,
        }
        .ok_or_else(|| errno!(EINVAL))?;

        if new_offset < 0 {
            return error!(EINVAL);
        }

        let mut iterator_position = *current_offset;

        if new_offset < iterator_position {
            // Our iterator only goes forward, so reset it here.
            *iterator = RemoteDirectoryIterator::default();
            iterator_position = 0;
        }

        if iterator_position != new_offset {
            iterator.pending_entry = None;
        }

        // Advance the iterator to catch up with the offset.
        for i in iterator_position..new_offset {
            match iterator.next(&self.zxio) {
                Some(Ok(_)) => continue,
                None => break, // No more entries.
                Some(Err(_)) => {
                    // In order to keep the offset and the iterator in sync, set the new offset
                    // to be as far as we could get.
                    // Note that failing the seek here would also cause the iterator and the
                    // offset to not be in sync, because the iterator has already moved from
                    // where it was.
                    *current_offset = i;
                    return Ok(*current_offset);
                }
            }
        }

        *current_offset = new_offset;

        Ok(*current_offset)
    }

    fn readdir(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        // It is important to acquire the lock to the offset before the context,
        //  to avoid a deadlock where seek() tries to modify the context.
        let mut iterator = self.iterator.lock();

        let mut add_entry = |entry: &ZxioDirent| {
            let inode_num: ino_t = entry.id.ok_or_else(|| errno!(EIO))?;
            let entry_type = DirectoryEntryType::UNKNOWN;
            sink.add(inode_num, sink.offset() + 1, entry_type, &entry.name)?;
            Ok(())
        };

        while let Some(entry) = iterator.next(&self.zxio) {
            if let Ok(entry) = entry {
                if let Err(e) = add_entry(&entry) {
                    iterator.pending_entry = Some(entry);
                    return Err(e);
                }
            }
        }
        Ok(())
    }
}

struct RemoteFileObject {
    /// The underlying Zircon I/O object.
    zxio: Arc<Zxio>,
}

impl RemoteFileObject {
    pub fn new(zxio: Zxio) -> RemoteFileObject {
        RemoteFileObject { zxio: Arc::new(zxio) }
    }
}

impl FileOps for RemoteFileObject {
    fileops_impl_seekable!();

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        zxio_read_at(&self.zxio, current_task, offset, data)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        zxio_write_at(&self.zxio, current_task, offset, data)
    }

    fn get_vmo(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _length: Option<usize>,
        mut prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        let has_execute = prot.contains(zx::VmarFlags::PERM_EXECUTE);
        prot -= zx::VmarFlags::PERM_EXECUTE;
        let mut vmo = self.zxio.vmo_get(prot).map_err(|status| from_status_like_fdio!(status))?;
        if has_execute {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        }
        Ok(vmo)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        zxio_wait_async(&self.zxio, waiter, events, handler, options)
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, waiter: &Waiter, key: WaitKey) {
        zxio_cancel_wait(&self.zxio, waiter, key);
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        zxio_query_events(&self.zxio)
    }
}

struct RemotePipeObject {
    /// The underlying Zircon I/O object.
    ///
    /// Shared with RemoteNode.
    zxio: Arc<syncio::Zxio>,
}

impl RemotePipeObject {
    fn new(handle: zx::Handle) -> Result<RemotePipeObject, Errno> {
        let zxio = Arc::new(Zxio::create(handle).map_err(|status| from_status_like_fdio!(status))?);
        Ok(RemotePipeObject { zxio })
    }
}

impl FileOps for RemotePipeObject {
    fileops_impl_nonseekable!();

    fn read(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        zxio_read(&self.zxio, current_task, data)
    }

    fn write(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        zxio_write(&self.zxio, current_task, data)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        zxio_wait_async(&self.zxio, waiter, events, handler, options)
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, waiter: &Waiter, key: WaitKey) {
        zxio_cancel_wait(&self.zxio, waiter, key);
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        zxio_query_events(&self.zxio)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::mm::PAGE_SIZE;
    use crate::testing::*;
    use fidl_fuchsia_io as fio;

    #[::fuchsia::test]
    fn test_tree() -> Result<(), anyhow::Error> {
        let (kernel, current_task) = create_kernel_and_task();
        let rights = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE;
        let (server, client) = zx::Channel::create().expect("failed to create channel pair");
        fdio::open("/pkg", rights, server).expect("failed to open /pkg");
        let fs = RemoteFs::new_fs(&kernel, client, rights)?;
        let ns = Namespace::new(fs);
        let root = ns.root();
        let mut context = LookupContext::default();
        assert_eq!(
            root.lookup_child(&current_task, &mut context, b"nib").err(),
            Some(errno!(ENOENT))
        );
        let mut context = LookupContext::default();
        root.lookup_child(&current_task, &mut context, b"lib").unwrap();

        let mut context = LookupContext::default();
        let _test_file = root
            .lookup_child(&current_task, &mut context, b"bin/hello_starnix")?
            .open(&current_task, OpenFlags::RDONLY, true)?;
        Ok(())
    }

    #[::fuchsia::test]
    fn test_blocking_io() -> Result<(), anyhow::Error> {
        let (_kernel, current_task) = create_kernel_and_task();

        let address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        let (client, server) = zx::Socket::create(zx::SocketOpts::empty())?;
        let pipe = create_fuchsia_pipe(&current_task, client, OpenFlags::RDWR)?;

        let thread = std::thread::spawn(move || {
            assert_eq!(
                64,
                pipe.read(&current_task, &[UserBuffer { address, length: 64 }]).unwrap()
            );
        });

        // Wait for the thread to become blocked on the read.
        zx::Duration::from_seconds(2).sleep();

        let bytes = [0u8; 64];
        assert_eq!(64, server.write(&bytes)?);

        // The thread should unblock and join us here.
        let _ = thread.join();

        Ok(())
    }

    #[::fuchsia::test]
    fn test_poll() {
        let (_kernel, current_task) = create_kernel_and_task();

        let address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        let (client, server) = zx::Socket::create(zx::SocketOpts::empty()).expect("Socket::create");
        let pipe = create_fuchsia_pipe(&current_task, client, OpenFlags::RDWR)
            .expect("create_fuchsia_pipe");
        let server_zxio = Zxio::create(server.into_handle()).expect("Zxio::create");

        assert_eq!(pipe.query_events(&current_task), FdEvents::POLLOUT);

        let epoll_object = EpollFileObject::new_file(&current_task);
        let epoll_file = epoll_object.downcast_file::<EpollFileObject>().unwrap();
        let event = EpollEvent { events: FdEvents::POLLIN.mask(), data: 0 };
        epoll_file.add(&current_task, &pipe, &epoll_object, event).expect("poll_file.add");

        let fds = epoll_file.wait(&current_task, 1, zx::Duration::from_millis(0)).expect("wait");
        assert!(fds.is_empty());

        assert_eq!(server_zxio.write(&[0]).expect("write"), 1);

        assert_eq!(pipe.query_events(&current_task), FdEvents::POLLOUT | FdEvents::POLLIN);
        let fds = epoll_file.wait(&current_task, 1, zx::Duration::from_millis(0)).expect("wait");
        assert_eq!(fds.len(), 1);

        assert_eq!(
            pipe.read(&current_task, &[UserBuffer { address, length: 64 }]).expect("read"),
            1
        );

        assert_eq!(pipe.query_events(&current_task), FdEvents::POLLOUT);
        let fds = epoll_file.wait(&current_task, 1, zx::Duration::from_millis(0)).expect("wait");
        assert!(fds.is_empty());
    }
}
