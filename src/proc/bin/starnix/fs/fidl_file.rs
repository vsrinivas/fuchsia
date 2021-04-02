// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_kernel as fkernel;
use fuchsia_zircon as zx;
use fuchsia_component::client::connect_to_service;
use parking_lot::Mutex;
use lazy_static::lazy_static;

use crate::types::*;
use crate::ThreadContext;
use super::*;

lazy_static! {
    static ref VMEX_RESOURCE: zx::Resource = {
        let service = connect_to_service::<fkernel::VmexResourceMarker>().expect("couldn't connect to fuchsia.kernel.VmexResource");
        let mut service = fkernel::VmexResourceSynchronousProxy::new(service.into_channel().unwrap().into_zx_channel());
        service.get(zx::Time::INFINITE).expect("couldn't talk to fuchsia.kernel.VmexResource")
    };
}

#[derive(FileDesc)]
pub struct FidlFile {
    common: FileCommon,

    // TODO(tbodt): whyyyyyy is this a mutex... whyyyy does fidl::client::sync::Proxy require
    // mutability
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
        let mut node = fio::NodeSynchronousProxy::new(node.into_channel().unwrap().into_zx_channel());
        let node = match node.describe(zx::Time::INFINITE).map_err(|_| EIO)? {
            fio::NodeInfo::Directory(_) => FidlNode::Directory(fio::DirectorySynchronousProxy::new(node.into_channel())),
            fio::NodeInfo::File(_) => FidlNode::File(fio::FileSynchronousProxy::new(node.into_channel())),
            _ => FidlNode::Other(node),
        };
        Ok(Arc::new(FidlFile { common: FileCommon::default(), node: Mutex::new(node) }))
    }
}

impl FileDesc for FidlFile {
    fn write(&self, _ctx: &ThreadContext, _data: &[iovec]) -> Result<usize, Errno> {
        Err(ENOSYS)
    }
    fn read(&self, ctx: &ThreadContext, offset: &mut usize, buf: &[iovec]) -> Result<usize, Errno> {
        let mut total = 0;
        for vec in buf {
            total += vec.iov_len;
        }
        let (status, data) = match *self.node.lock() {
            FidlNode::File(ref mut n) => n.read_at(total as u64, *offset as u64, zx::Time::INFINITE).map_err(|_| EIO),
            FidlNode::Directory(_) => Err(EISDIR),
            FidlNode::Other(_) => Err(EINVAL),
        }?;
        zx::Status::ok(status).map_err(|s| match s {
            // TODO
            _ => EIO,
        })?;
        let mut offset = 0;
        for vec in buf {
            ctx.process.write_memory(vec.iov_base, &data[offset..offset+vec.iov_len])?;
            offset += vec.iov_len;
        }
        Ok(total)
    }

    fn mmap(&self, _ctx: &ThreadContext, mut prot: zx::VmarFlags, _flags: i32, offset: usize) -> Result<(zx::Vmo, usize), Errno> {
        let has_execute = prot.contains(zx::VmarFlags::PERM_EXECUTE);
        prot -= zx::VmarFlags::PERM_EXECUTE;

        let (status, buffer) = match *self.node.lock() {
            FidlNode::File(ref mut n) => n.get_buffer(prot.bits(), zx::Time::INFINITE).map_err(|e| {
                info!("get_attr fidl error: {:?}", e);
                EIO
            }),
            _ => Err(ENODEV),
        }?;
        zx::Status::ok(status).map_err(|s| match s {
            // TODO
            _ => {
                info!("get_buffer error: {:?}", s);
                EIO
            }
        })?;
        let mut vmo = buffer.unwrap().vmo;
        if has_execute {
            vmo = vmo.replace_as_executable().unwrap();
        }
        Ok((vmo, offset))
    }

    fn fstat(&self, ctx: &ThreadContext) -> Result<stat_t, Errno> {
        // TODO: log FIDL error
        let (status, attrs) = self.node.lock().get_attr().map_err(|e| {
            info!("get_attr fidl error: {:?}", e);
            EIO
        })?;
        zx::Status::ok(status).map_err(|s| match s {
            // TODO
            _ => {
                info!("get_attr error: {:?}", s);
                EIO
            }
        })?;
        Ok(stat_t {
            st_mode: attrs.mode,
            st_ino: attrs.id,
            st_size: attrs.content_size as i64,
            st_blocks: attrs.storage_size as i64 / 512,
            st_uid: ctx.process.security.uid,
            st_gid: ctx.process.security.gid,
            st_nlink: attrs.link_count,
            ..stat_t::default()
        })
    }
}
