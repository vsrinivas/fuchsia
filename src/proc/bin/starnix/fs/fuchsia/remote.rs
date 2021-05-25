// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fidl_fuchsia_kernel as fkernel;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_zircon as zx;
use lazy_static::lazy_static;
use log::{info, warn};

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

struct RemoteDirectoryNode {
    dir: fio::DirectorySynchronousProxy,
    // The fuchsia.io rights for the dir handle. Subdirs will be opened with the same rights.
    rights: u32,
}

impl FsNodeOps for RemoteDirectoryNode {
    fn open(&self) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(RemoteFile {
            node: RemoteNode::Directory(
                syncio::directory_clone(&self.dir, fio::CLONE_FLAG_SAME_RIGHTS).map_err(|_| EIO)?,
            ),
        }))
    }

    fn lookup(&self, name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        let desc = syncio::directory_open(
            &self.dir,
            std::str::from_utf8(name).map_err(|_| {
                warn!("bad utf8 in pathname! fidl can't handle this");
                EINVAL
            })?,
            self.rights,
            0,
            zx::Time::INFINITE,
        )
        .map_err(|e| match e {
            zx::Status::NOT_FOUND => ENOENT,
            _ => {
                warn!("open failed on {:?}: {:?}", name, e);
                EIO
            }
        })?;
        Ok(match desc.info {
            fio::NodeInfo::Directory(_) => Box::new(RemoteDirectoryNode {
                dir: fio::DirectorySynchronousProxy::new(desc.node.into_channel()),
                rights: self.rights,
            }),
            fio::NodeInfo::File(_) => Box::new(RemoteFileNode {
                file: fio::FileSynchronousProxy::new(desc.node.into_channel()),
            }),
            _ => {
                warn!("non-directories are unimplemented {:?}", desc.info);
                return Err(ENOSYS);
            }
        })
    }

    fn mkdir(&self, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        not_implemented!("remote mkdir");
        Err(ENOSYS)
    }
}

struct RemoteFileNode {
    file: fio::FileSynchronousProxy,
}

impl FsNodeOps for RemoteFileNode {
    fn open(&self) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(RemoteFile {
            node: RemoteNode::File(
                syncio::file_clone(&self.file, fio::CLONE_FLAG_SAME_RIGHTS).map_err(|_| EIO)?,
            ),
        }))
    }
}

#[cfg(test)]
use crate::devices::*;

#[cfg(test)] // Will be used outside of tests later
fn new_remote_filesystem(dir: fio::DirectorySynchronousProxy, rights: u32) -> FsNodeHandle {
    let remotefs = AnonymousNodeDevice::new(0); // TODO: Get from device registry.
    FsNode::new_root(RemoteDirectoryNode { dir, rights }, remotefs)
}

pub struct RemoteFile {
    node: RemoteNode,
}

enum RemoteNode {
    File(fio::FileSynchronousProxy),
    Directory(fio::DirectorySynchronousProxy),
    Other(fio::NodeSynchronousProxy),
}

impl RemoteNode {
    fn get_attr(&self) -> Result<(i32, fio::NodeAttributes), fidl::Error> {
        match self {
            RemoteNode::File(n) => n.get_attr(zx::Time::INFINITE),
            RemoteNode::Directory(n) => n.get_attr(zx::Time::INFINITE),
            RemoteNode::Other(n) => n.get_attr(zx::Time::INFINITE),
        }
    }
}

impl RemoteFile {
    pub fn from_description(description: syncio::DescribedNode) -> FileHandle {
        let node = match description.info {
            fio::NodeInfo::Directory(_) => RemoteNode::Directory(
                fio::DirectorySynchronousProxy::new(description.node.into_channel()),
            ),
            fio::NodeInfo::File(_) => {
                RemoteNode::File(fio::FileSynchronousProxy::new(description.node.into_channel()))
            }
            _ => RemoteNode::Other(description.node),
        };
        FileObject::new(RemoteFile { node })
    }
}

const BYTES_PER_BLOCK: i64 = 512;

impl FileOps for RemoteFile {
    fd_impl_seekable!();

    fn read_at(
        &self,
        _fd: &FileObject,
        task: &Task,
        offset: usize,
        buf: &[iovec_t],
    ) -> Result<usize, Errno> {
        let mut total = 0;
        for vec in buf {
            total += vec.iov_len;
        }
        let (status, data) = match self.node {
            RemoteNode::File(ref n) => {
                // TODO(tbodt): Break this into 8k chunks if needed to fit in the FIDL protocol
                n.read_at(total as u64, offset as u64, zx::Time::INFINITE).map_err(fidl_error)
            }
            RemoteNode::Directory(_) => Err(EISDIR),
            RemoteNode::Other(_) => Err(EINVAL),
        }?;
        zx::Status::ok(status).map_err(fio_error)?;
        let mut offset = 0;
        for vec in buf {
            let end = std::cmp::min(offset + vec.iov_len, data.len());
            task.mm.write_memory(vec.iov_base, &data[offset..end])?;
            offset = end;
            if offset == data.len() {
                break;
            }
        }
        Ok(data.len())
    }

    fn write_at(
        &self,
        _fd: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[iovec_t],
    ) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn get_vmo(
        &self,
        _fd: &FileObject,
        _task: &Task,
        mut prot: zx::VmarFlags,
        _flags: u32,
    ) -> Result<zx::Vmo, Errno> {
        let has_execute = prot.contains(zx::VmarFlags::PERM_EXECUTE);
        prot -= zx::VmarFlags::PERM_EXECUTE;

        let (status, buffer) = match self.node {
            RemoteNode::File(ref n) => {
                n.get_buffer(prot.bits(), zx::Time::INFINITE).map_err(fidl_error)
            }
            _ => Err(ENODEV),
        }?;
        zx::Status::ok(status).map_err(fio_error)?;
        let mut vmo = buffer.unwrap().vmo;
        if has_execute {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).expect("replace_as_executable failed");
        }
        Ok(vmo)
    }

    fn fstat(&self, _fd: &FileObject, task: &Task) -> Result<stat_t, Errno> {
        let (status, attrs) = self.node.get_attr().map_err(fidl_error)?;
        zx::Status::ok(status).map_err(fio_error)?;
        Ok(stat_t {
            st_mode: attrs.mode,
            st_ino: attrs.id,
            st_size: attrs.content_size as i64,
            st_blocks: attrs.storage_size as i64 / BYTES_PER_BLOCK,
            st_uid: task.creds.uid,
            st_gid: task.creds.gid,
            st_nlink: attrs.link_count,
            ..stat_t::default()
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::Proxy;
    use fidl_fuchsia_io::DirectorySynchronousProxy;

    #[::fuchsia::test]
    async fn test_tree() -> Result<(), anyhow::Error> {
        let rights = fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE;
        let root = io_util::directory::open_in_namespace("/pkg", rights)?;
        let root = new_remote_filesystem(
            DirectorySynchronousProxy::new(root.into_channel().unwrap().into_zx_channel()),
            rights,
        );
        assert_eq!(path_lookup(&root, b"nib").err(), Some(ENOENT));
        path_lookup(&root, b"lib").unwrap();

        let _test_file = path_lookup(&root, b"bin/hello_starnix")?.open()?;
        Ok(())
    }
}

fn fidl_error(err: fidl::Error) -> Errno {
    info!("fidl error: {}", err);
    EIO
}
fn fio_error(status: zx::Status) -> Errno {
    Errno::from_status_like_fdio(status)
}
