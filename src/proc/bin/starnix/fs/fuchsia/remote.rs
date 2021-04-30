// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fidl_fuchsia_kernel as fkernel;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_zircon as zx;
use lazy_static::lazy_static;
use log::info;
use std::sync::Arc;

use crate::fd_impl_seekable;
use crate::fs::*;
use crate::task::*;
use crate::uapi::*;

lazy_static! {
    static ref VMEX_RESOURCE: zx::Resource = {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        connect_channel_to_protocol::<fkernel::VmexResourceMarker>(server_end)
            .expect("couldn't connect to fuchsia.kernel.VmexResource");
        let service = fkernel::VmexResourceSynchronousProxy::new(client_end);
        service.get(zx::Time::INFINITE).expect("couldn't talk to fuchsia.kernel.VmexResource")
    };
}

#[derive(FileObject)]
pub struct RemoteFile {
    common: FileCommon,
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
    pub fn from_node(node: fio::NodeSynchronousProxy) -> Result<FileHandle, Errno> {
        let node = match node.describe(zx::Time::INFINITE).map_err(fidl_error)? {
            fio::NodeInfo::Directory(_) => {
                RemoteNode::Directory(fio::DirectorySynchronousProxy::new(node.into_channel()))
            }
            fio::NodeInfo::File(_) => {
                RemoteNode::File(fio::FileSynchronousProxy::new(node.into_channel()))
            }
            _ => RemoteNode::Other(node),
        };
        Ok(Arc::new(RemoteFile { common: FileCommon::default(), node }))
    }
}

const BYTES_PER_BLOCK: i64 = 512;

impl FileObject for RemoteFile {
    fd_impl_seekable!();

    fn read_at(&self, task: &Task, offset: usize, buf: &[iovec_t]) -> Result<usize, Errno> {
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

    fn write_at(&self, _task: &Task, _offset: usize, _data: &[iovec_t]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn get_vmo(
        &self,
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

    fn fstat(&self, task: &Task) -> Result<stat_t, Errno> {
        // TODO: log FIDL error
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

fn fidl_error(err: fidl::Error) -> Errno {
    info!("fidl error: {}", err);
    EIO
}
fn fio_error(status: zx::Status) -> Errno {
    Errno::from_status_like_fdio(status)
}
