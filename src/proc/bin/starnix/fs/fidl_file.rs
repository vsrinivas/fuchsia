// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_kernel as fkernel;
use fuchsia_component::client::connect_channel_to_service;
use fuchsia_zircon as zx;
use lazy_static::lazy_static;
use log::info;
use parking_lot::Mutex;
use std::sync::Arc;

use super::*;
use crate::fd_impl_seekable;
use crate::uapi::*;
use crate::ThreadContext;

lazy_static! {
    static ref VMEX_RESOURCE: zx::Resource = {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        connect_channel_to_service::<fkernel::VmexResourceMarker>(server_end)
            .expect("couldn't connect to fuchsia.kernel.VmexResource");
        let service = fkernel::VmexResourceSynchronousProxy::new(client_end);
        service.get(zx::Time::INFINITE).expect("couldn't talk to fuchsia.kernel.VmexResource")
    };
}

#[derive(FileDesc)]
pub struct FidlFile {
    common: FileCommon,

    // TODO(fxbug.dev/75039): See if we can remove this mutex. It seems strange that one is needed for sync
    // FIDL but not async FIDL.
    node: Mutex<FidlNode>,
}

enum FidlNode {
    File(fio::FileSynchronousProxy),
    Directory(fio::DirectorySynchronousProxy),
    Other(fio::NodeSynchronousProxy),
}

impl FidlNode {
    fn get_attr(&mut self) -> Result<(i32, fio::NodeAttributes), fidl::Error> {
        match self {
            FidlNode::File(n) => n.get_attr(zx::Time::INFINITE),
            FidlNode::Directory(n) => n.get_attr(zx::Time::INFINITE),
            FidlNode::Other(n) => n.get_attr(zx::Time::INFINITE),
        }
    }
}

impl FidlFile {
    pub fn from_node(node: fio::NodeProxy) -> Result<FdHandle, Errno> {
        let node = fio::NodeSynchronousProxy::new(node.into_channel().unwrap().into_zx_channel());
        let node = match node.describe(zx::Time::INFINITE).map_err(fidl_error)? {
            fio::NodeInfo::Directory(_) => {
                FidlNode::Directory(fio::DirectorySynchronousProxy::new(node.into_channel()))
            }
            fio::NodeInfo::File(_) => {
                FidlNode::File(fio::FileSynchronousProxy::new(node.into_channel()))
            }
            _ => FidlNode::Other(node),
        };
        Ok(Arc::new(FidlFile { common: FileCommon::default(), node: Mutex::new(node) }))
    }
}

const BYTES_PER_BLOCK: i64 = 512;

impl FileDesc for FidlFile {
    fd_impl_seekable!();

    fn read_at(&self, ctx: &ThreadContext, offset: usize, buf: &[iovec_t]) -> Result<usize, Errno> {
        let mut total = 0;
        for vec in buf {
            total += vec.iov_len;
        }
        let (status, data) = match *self.node.lock() {
            FidlNode::File(ref n) => {
                // TODO(tbodt): Break this into 8k chunks if needed to fit in the FIDL protocol
                n.read_at(total as u64, offset as u64, zx::Time::INFINITE).map_err(fidl_error)
            }
            FidlNode::Directory(_) => Err(EISDIR),
            FidlNode::Other(_) => Err(EINVAL),
        }?;
        zx::Status::ok(status).map_err(fio_error)?;
        let mut offset = 0;
        for vec in buf {
            let end = std::cmp::min(offset + vec.iov_len, data.len());
            ctx.process.write_memory(vec.iov_base, &data[offset..end])?;
            offset = end;
            if offset == data.len() {
                break;
            }
        }
        Ok(data.len())
    }

    fn write_at(
        &self,
        _ctx: &ThreadContext,
        _offset: usize,
        _data: &[iovec_t],
    ) -> Result<usize, Errno> {
        Err(ENOSYS)
    }

    fn get_vmo(
        &self,
        _ctx: &ThreadContext,
        mut prot: zx::VmarFlags,
        _flags: u32,
    ) -> Result<zx::Vmo, Errno> {
        let has_execute = prot.contains(zx::VmarFlags::PERM_EXECUTE);
        prot -= zx::VmarFlags::PERM_EXECUTE;

        let (status, buffer) = match *self.node.lock() {
            FidlNode::File(ref n) => {
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

    fn fstat(&self, ctx: &ThreadContext) -> Result<stat_t, Errno> {
        // TODO: log FIDL error
        let (status, attrs) = self.node.lock().get_attr().map_err(fidl_error)?;
        zx::Status::ok(status).map_err(fio_error)?;
        Ok(stat_t {
            st_mode: attrs.mode,
            st_ino: attrs.id,
            st_size: attrs.content_size as i64,
            st_blocks: attrs.storage_size as i64 / BYTES_PER_BLOCK,
            st_uid: ctx.process.security.uid,
            st_gid: ctx.process.security.gid,
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
