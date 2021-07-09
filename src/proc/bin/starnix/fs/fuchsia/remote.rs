// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_kernel as fkernel;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_zircon::{self as zx, HandleBased};
use lazy_static::lazy_static;
use log::warn;
use std::sync::Arc;
use syncio::{zxio::zxio_get_posix_mode, zxio_node_attributes_t, Zxio};

use crate::devices::*;
use crate::fs::*;
use crate::task::*;
use crate::types::*;
use crate::{fd_impl_seekable, not_implemented};

lazy_static! {
    static ref VMEX_RESOURCE: zx::Resource = {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        connect_channel_to_protocol::<fkernel::VmexResourceMarker>(server_end)
            .expect("couldn't connect to fuchsia.kernel.VmexResource");
        let service = fkernel::VmexResourceSynchronousProxy::new(client_end);
        service.get(zx::Time::INFINITE).expect("couldn't talk to fuchsia.kernel.VmexResource")
    };
}

fn update_stat_from_result(node: &FsNode, attrs: zxio_node_attributes_t) -> Result<(), Errno> {
    /// st_blksize is measured in units of 512 bytes.
    const BYTES_PER_BLOCK: usize = 512;

    let mut state = node.state_mut();
    state.node_id = attrs.id;
    // TODO - store these in FsNodeState and convert on fstat
    state.mode = unsafe { zxio_get_posix_mode(attrs.protocols, attrs.abilities) };
    state.content_size = attrs.content_size as usize;
    state.storage_size = attrs.storage_size as usize;
    state.block_size = BYTES_PER_BLOCK;
    state.link_count = attrs.link_count;

    Ok(())
}

pub struct RemoteNode {
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

impl FsNodeOps for RemoteNode {
    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(RemoteFileObject { zxio: Arc::clone(&self.zxio) }))
    }

    fn lookup(&self, _node: &FsNode, name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        let name = std::str::from_utf8(name).map_err(|_| {
            warn!("bad utf8 in pathname! remote filesystems can't handle this");
            EINVAL
        })?;
        let zxio =
            Arc::new(self.zxio.open(self.rights, 0, name).map_err(Errno::from_status_like_fdio)?);
        Ok(Box::new(RemoteNode { zxio, rights: self.rights }))
    }

    fn mkdir(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        not_implemented!("remote mkdir");
        Err(ENOSYS)
    }

    fn truncate(&self, _node: &FsNode, length: u64) -> Result<(), Errno> {
        self.zxio.truncate(length).map_err(Errno::from_status_like_fdio)
    }

    fn update_stat(&self, node: &FsNode) -> Result<(), Errno> {
        let attributes = self.zxio.attr_get().map_err(Errno::from_status_like_fdio)?;
        update_stat_from_result(node, attributes)
    }
}

pub fn new_remote_filesystem(root: zx::Channel, rights: u32) -> FsNodeHandle {
    let remotefs = AnonNodeDevice::new(0); // TODO: Get from device registry.
    let zxio = Arc::new(Zxio::create(root.into_handle()).unwrap());
    FsNode::new_root(RemoteNode { zxio, rights }, remotefs)
}

pub struct RemoteFileObject {
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
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).expect("replace_as_executable failed");
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
        let ns = Namespace::new(new_remote_filesystem(
            root.into_channel().unwrap().into_zx_channel(),
            rights,
        ));
        let root = ns.root();
        assert_eq!(root.lookup(b"nib").err(), Some(ENOENT));
        root.lookup(b"lib").unwrap();

        let _test_file = root.lookup(b"bin/hello_starnix")?.node.open()?;
        Ok(())
    }
}
