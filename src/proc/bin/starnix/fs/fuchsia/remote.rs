// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, HandleBased};
use log::warn;
use parking_lot::{Mutex, RwLockReadGuard, RwLockWriteGuard};
use std::sync::Arc;
use syncio::{
    zxio, zxio::zxio_get_posix_mode, zxio_node_attributes_t, DirentIterator, Zxio, ZxioDirent,
};

use crate::errno;
use crate::error;
use crate::fd_impl_directory;
use crate::fd_impl_nonseekable;
use crate::fd_impl_seekable;
use crate::from_status_like_fdio;
use crate::fs::*;
use crate::logging::impossible_error;
use crate::task::*;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;

pub struct RemoteFs;
impl FileSystemOps for RemoteFs {
    fn generate_node_ids(&self) -> bool {
        true
    }
}

impl RemoteFs {
    pub fn new(root: zx::Channel, rights: u32) -> Result<FileSystemHandle, Errno> {
        let remote_node = RemoteNode::new(root.into_handle(), rights)?;
        let attrs = remote_node.zxio.attr_get().map_err(|_| errno!(EIO))?;
        let mut root_node = FsNode::new_root(remote_node);
        root_node.inode_num = attrs.id;
        Ok(FileSystem::new_with_root(RemoteFs, root_node))
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
    rights: u32,
}

impl RemoteNode {
    fn new(handle: zx::Handle, rights: u32) -> Result<RemoteNode, Errno> {
        let zxio = Arc::new(Zxio::create(handle).map_err(|status| from_status_like_fdio!(status))?);
        Ok(RemoteNode { zxio, rights })
    }
}

pub fn create_fuchsia_pipe(kern: &Kernel, socket: zx::Socket) -> Result<FileHandle, Errno> {
    let ops = Box::new(RemotePipeObject::new(socket.into_handle())?);
    Ok(Anon::new_file(anon_fs(kern), ops, OpenFlags::RDWR))
}

fn update_into_from_attrs(info: &mut FsNodeInfo, attrs: zxio_node_attributes_t) {
    /// st_blksize is measured in units of 512 bytes.
    const BYTES_PER_BLOCK: usize = 512;

    // TODO - store these in FsNodeState and convert on fstat
    info.size = attrs.content_size as usize;
    info.storage_size = attrs.storage_size as usize;
    info.blksize = BYTES_PER_BLOCK;
    info.link_count = attrs.link_count;
}

fn get_zxio_signals_from_events(events: FdEvents) -> zxio::zxio_signals_t {
    let mut signals = zxio::ZXIO_SIGNAL_NONE;
    if events & FdEvents::POLLIN {
        signals |= zxio::ZXIO_SIGNAL_READABLE
            | zxio::ZXIO_SIGNAL_PEER_CLOSED
            | zxio::ZXIO_SIGNAL_READ_DISABLED;
    }
    if events & FdEvents::POLLOUT {
        signals |= zxio::ZXIO_SIGNAL_WRITABLE | zxio::ZXIO_SIGNAL_WRITE_DISABLED;
    }
    if events & FdEvents::POLLRDHUP {
        signals |= zxio::ZXIO_SIGNAL_READ_DISABLED | zxio::ZXIO_SIGNAL_PEER_CLOSED;
    }
    return signals;
}

fn get_events_from_zxio_signals(signals: zxio::zxio_signals_t) -> FdEvents {
    let mut events = FdEvents::empty();

    if signals & (zxio::ZXIO_SIGNAL_READABLE | zxio::ZXIO_SIGNAL_PEER_CLOSED | zxio::ZXIO_SIGNAL_READ_DISABLED) != 0 {
        events |= FdEvents::POLLIN;
    }
    if signals & (zxio::ZXIO_SIGNAL_WRITABLE | zxio::ZXIO_SIGNAL_WRITE_DISABLED) != 0 {
        events |= FdEvents::POLLOUT;
    }
    if signals & (zxio::ZXIO_SIGNAL_READ_DISABLED | zxio::ZXIO_SIGNAL_PEER_CLOSED) != 0 {
        events |= FdEvents::POLLRDHUP;
    }

    events
}

impl FsNodeOps for RemoteNode {
    fn open(&self, node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        let zxio = (&*self.zxio).clone().map_err(|status| from_status_like_fdio!(status))?;
        if node.is_dir() {
            return Ok(Box::new(RemoteDirectoryObject::new(zxio)));
        }

        Ok(Box::new(RemoteFileObject::new(zxio)))
    }

    fn lookup(&self, node: &FsNode, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        let name = std::str::from_utf8(name).map_err(|_| {
            warn!("bad utf8 in pathname! remote filesystems can't handle this");
            EINVAL
        })?;
        let zxio = Arc::new(
            self.zxio
                .open(self.rights, 0, name)
                .map_err(|status| from_status_like_fdio!(status))?,
        );

        // TODO: It's unfortunate to have another round-trip. We should be able
        // to set the mode based on the information we get during open.
        let attrs = zxio.attr_get().map_err(|status| from_status_like_fdio!(status))?;

        let ops = Box::new(RemoteNode { zxio, rights: self.rights });
        let mode =
            FileMode::from_bits(unsafe { zxio_get_posix_mode(attrs.protocols, attrs.abilities) });

        let child = node.fs().create_node_with_id(ops, mode, attrs.id);

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

fn zxio_read(zxio: &Zxio, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data);
    let mut bytes = vec![0u8; total];
    let actual = zxio.read(&mut bytes).map_err(|status| from_status_like_fdio!(status))?;
    task.mm.write_all(data, &bytes[0..actual])?;
    Ok(actual)
}

fn zxio_read_at(
    zxio: &Zxio,
    task: &Task,
    offset: usize,
    data: &[UserBuffer],
) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data);
    let mut bytes = vec![0u8; total];
    let actual =
        zxio.read_at(offset as u64, &mut bytes).map_err(|status| from_status_like_fdio!(status))?;
    task.mm.write_all(data, &bytes[0..actual])?;
    Ok(actual)
}

fn zxio_write(zxio: &Zxio, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data);
    let mut bytes = vec![0u8; total];
    task.mm.read_all(data, &mut bytes)?;
    let actual = zxio.write(&bytes).map_err(|status| from_status_like_fdio!(status))?;
    Ok(actual)
}

fn zxio_write_at(
    zxio: &Zxio,
    task: &Task,
    offset: usize,
    data: &[UserBuffer],
) -> Result<usize, Errno> {
    let total = UserBuffer::get_total_length(data);
    let mut bytes = vec![0u8; total];
    task.mm.read_all(data, &mut bytes)?;
    let actual =
        zxio.write_at(offset as u64, &bytes).map_err(|status| from_status_like_fdio!(status))?;
    Ok(actual)
}

fn zxio_wait_async(
    zxio: &Arc<Zxio>,
    waiter: &Arc<Waiter>,
    events: FdEvents,
    handler: EventHandler,
) {
    let zxio_clone = zxio.clone();
    let signal_handler = move |signals: zx::Signals| {
        let observed_zxio_signals = zxio_clone.wait_end(signals);
        let observed_events = get_events_from_zxio_signals(observed_zxio_signals);
        handler(observed_events);
    };

    let (handle, signals) = zxio.wait_begin(get_zxio_signals_from_events(events));
    waiter.wake_on_signals(&handle, signals, Box::new(signal_handler)).unwrap(); // TODO return error
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
    pub fn next(&mut self, zxio: &Zxio) -> Result<Option<ZxioDirent>, Errno> {
        match self.pending_entry.take() {
            Some(entry) => return Ok(Some(entry)),
            None => {
                let iterator = self.get_or_init_iterator(zxio)?;
                let result = iterator.next();
                match result {
                    Ok(v) => return Ok(Some(v)),
                    Err(zx::Status::NOT_FOUND) => return Ok(None),
                    Err(status) => return Err(from_status_like_fdio!(status)),
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
        RemoteDirectoryObject {
            zxio: zxio,
            iterator: Mutex::new(RemoteDirectoryIterator::default()),
        }
    }
}

impl FileOps for RemoteDirectoryObject {
    fd_impl_directory!();

    fn seek(
        &self,
        file: &FileObject,
        _task: &Task,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        let mut current_offset = file.offset.lock();
        let mut iterator = self.iterator.lock();
        let new_offset = match whence {
            SeekOrigin::SET => Some(offset),
            SeekOrigin::CUR => (*current_offset).checked_add(offset),
            SeekOrigin::END => None,
        }
        .ok_or(errno!(EINVAL))?;

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
                Ok(Some(_)) => continue,
                Ok(None) => break, // No more entries.
                Err(_) => {
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
        file: &FileObject,
        _task: &Task,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        // It is important to acquire the lock to the offset before the context,
        //  to avoid a deadlock where seek() tries to modify the context.
        let mut offset = file.offset.lock();
        let mut iterator = self.iterator.lock();

        let mut add_entry = |entry: &ZxioDirent| {
            let inode_num: ino_t = entry.id.ok_or(errno!(EIO))?;
            let entry_type = DirectoryEntryType::UNKNOWN;
            let new_offset = *offset + 1;
            sink.add(inode_num, new_offset, entry_type, &entry.name)?;
            *offset = new_offset;
            Ok(())
        };

        while let Some(entry) = iterator.next(&self.zxio)? {
            if let Err(e) = add_entry(&entry) {
                iterator.pending_entry = Some(entry);
                return Err(e);
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
    fd_impl_seekable!();

    fn read_at(
        &self,
        _file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        zxio_read_at(&self.zxio, task, offset, data)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        zxio_write_at(&self.zxio, task, offset, data)
    }

    fn get_vmo(
        &self,
        _file: &FileObject,
        _task: &Task,
        mut prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        let has_execute = prot.contains(zx::VmarFlags::PERM_EXECUTE);
        prot -= zx::VmarFlags::PERM_EXECUTE;
        let (mut vmo, _size) =
            self.zxio.vmo_get(prot).map_err(|status| from_status_like_fdio!(status))?;
        if has_execute {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        }
        Ok(vmo)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        waiter: &Arc<Waiter>,
        events: FdEvents,
        handler: EventHandler,
    ) {
        zxio_wait_async(&self.zxio, waiter, events, handler)
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
    fd_impl_nonseekable!();

    fn read(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        zxio_read(&self.zxio, task, data)
    }

    fn write(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        zxio_write(&self.zxio, task, data)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        waiter: &Arc<Waiter>,
        events: FdEvents,
        handler: EventHandler,
    ) {
        zxio_wait_async(&self.zxio, waiter, events, handler)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::Proxy;
    use fidl_fuchsia_io as fio;
    use fuchsia_async as fasync;

    use crate::errno;
    use crate::mm::PAGE_SIZE;
    use crate::syscalls::*;
    use crate::testing::*;

    #[::fuchsia::test]
    async fn test_tree() -> Result<(), anyhow::Error> {
        let (_kernel, task_owner) = create_kernel_and_task();
        let rights = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;
        let root = io_util::directory::open_in_namespace("/pkg", rights)?;
        let fs = RemoteFs::new(root.into_channel().unwrap().into_zx_channel(), rights)?;
        let ns = Namespace::new(fs.clone());
        let root = ns.root();
        let mut context = LookupContext::default();
        assert_eq!(
            root.lookup_child(&mut context, &task_owner.task, b"nib").err(),
            Some(errno!(ENOENT))
        );
        let mut context = LookupContext::default();
        root.lookup_child(&mut context, &task_owner.task, b"lib").unwrap();

        let mut context = LookupContext::default();
        let _test_file = root
            .lookup_child(&mut context, &task_owner.task, b"bin/hello_starnix")?
            .open(OpenFlags::RDONLY)?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_blocking_io() -> Result<(), anyhow::Error> {
        let (kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let task = Arc::clone(ctx.task);

        let address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        let (client, server) = zx::Socket::create(zx::SocketOpts::empty())?;
        let pipe = create_fuchsia_pipe(&kernel, client)?;

        let thread = std::thread::spawn(move || {
            assert_eq!(64, pipe.read(&task, &[UserBuffer { address, length: 64 }]).unwrap());
        });

        // Wait for the thread to become blocked on the read.
        zx::Duration::from_seconds(2).sleep();

        let bytes = [0u8; 64];
        assert_eq!(64, server.write(&bytes)?);

        // The thread should unblock and join us here.
        let _ = thread.join();

        Ok(())
    }
}
