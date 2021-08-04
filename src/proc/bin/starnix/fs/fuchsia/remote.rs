// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, HandleBased};
use log::warn;
use parking_lot::{RwLockReadGuard, RwLockWriteGuard};
use std::sync::Arc;
use syncio::{zxio::zxio_get_posix_mode, zxio_node_attributes_t, Zxio};

use crate::fd_impl_seekable;
use crate::fs::*;
use crate::logging::impossible_error;
use crate::task::*;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;

pub struct RemoteFs;
impl FileSystemOps for RemoteFs {}

impl RemoteFs {
    pub fn new(root: zx::Channel, rights: u32) -> FileSystemHandle {
        let zxio = Arc::new(Zxio::create(root.into_handle()).unwrap());
        FileSystem::new(RemoteFs, RemoteNode { zxio, rights })
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

fn update_into_from_attrs(info: &mut FsNodeInfo, attrs: zxio_node_attributes_t) {
    /// st_blksize is measured in units of 512 bytes.
    const BYTES_PER_BLOCK: usize = 512;

    info.inode_num = attrs.id;
    // TODO - store these in FsNodeState and convert on fstat
    info.mode =
        FileMode::from_bits(unsafe { zxio_get_posix_mode(attrs.protocols, attrs.abilities) });
    info.size = attrs.content_size as usize;
    info.storage_size = attrs.storage_size as usize;
    info.blksize = BYTES_PER_BLOCK;
    info.link_count = attrs.link_count;
}

impl FsNodeOps for RemoteNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(RemoteFileObject { zxio: Arc::clone(&self.zxio) }))
    }

    fn lookup(&self, _parent: &FsNode, mut child: FsNode) -> Result<FsNodeHandle, Errno> {
        let name = std::str::from_utf8(child.local_name()).map_err(|_| {
            warn!("bad utf8 in pathname! remote filesystems can't handle this");
            EINVAL
        })?;
        let zxio =
            Arc::new(self.zxio.open(self.rights, 0, name).map_err(Errno::from_status_like_fdio)?);

        // TODO: It's unfortunate to have another round-trip. We should be able
        // to set the mode based on the information we get during open.
        let attrs = zxio.attr_get().map_err(Errno::from_status_like_fdio)?;
        update_into_from_attrs(child.info_mut(), attrs);

        child.set_ops(RemoteNode { zxio, rights: self.rights });
        Ok(child.into_handle())
    }

    fn truncate(&self, _node: &FsNode, length: u64) -> Result<(), Errno> {
        self.zxio.truncate(length).map_err(Errno::from_status_like_fdio)
    }

    fn update_info<'a>(&self, node: &'a FsNode) -> Result<RwLockReadGuard<'a, FsNodeInfo>, Errno> {
        let attrs = self.zxio.attr_get().map_err(Errno::from_status_like_fdio)?;
        let mut info = node.info_write();
        update_into_from_attrs(&mut info, attrs);
        Ok(RwLockWriteGuard::downgrade(info))
    }
}

struct RemoteFileObject {
    /// The underlying Zircon I/O object.
    ///
    /// Shared with RemoteNode.
    zxio: Arc<syncio::Zxio>,
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
        let total = UserBuffer::get_total_length(data);
        let mut bytes = vec![0u8; total];
        let actual =
            self.zxio.read_at(offset as u64, &mut bytes).map_err(Errno::from_status_like_fdio)?;
        task.mm.write_all(data, &bytes[0..actual])?;
        Ok(actual)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let total = UserBuffer::get_total_length(data);
        let mut bytes = vec![0u8; total];
        task.mm.read_all(data, &mut bytes)?;
        let actual =
            self.zxio.write_at(offset as u64, &bytes).map_err(Errno::from_status_like_fdio)?;
        Ok(actual)
    }

    fn get_vmo(
        &self,
        _file: &FileObject,
        _task: &Task,
        mut prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        let has_execute = prot.contains(zx::VmarFlags::PERM_EXECUTE);
        prot -= zx::VmarFlags::PERM_EXECUTE;
        let (mut vmo, _size) = self.zxio.vmo_get(prot).map_err(Errno::from_status_like_fdio)?;
        if has_execute {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        }
        Ok(vmo)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::Proxy;
    use fidl_fuchsia_io as fio;

    #[::fuchsia::test]
    async fn test_tree() -> Result<(), anyhow::Error> {
        let rights = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;
        let root = io_util::directory::open_in_namespace("/pkg", rights)?;
        let fs = RemoteFs::new(root.into_channel().unwrap().into_zx_channel(), rights);
        let ns = Namespace::new(fs.clone());
        let context = FsContext::new(fs);
        let root = ns.root();
        assert_eq!(root.lookup(&context, b"nib", SymlinkFollowing::Enabled).err(), Some(ENOENT));
        root.lookup(&context, b"lib", SymlinkFollowing::Enabled).unwrap();

        let _test_file = root
            .lookup(&context, b"bin/hello_starnix", SymlinkFollowing::Enabled)?
            .open(OpenFlags::RDONLY)?;
        Ok(())
    }
}
