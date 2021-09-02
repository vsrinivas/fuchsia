// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::errno;
use crate::error;
use crate::fs::*;
use crate::mode;
use crate::not_implemented;
use crate::syscalls::*;
use crate::types::*;

pub fn sys_socket(
    ctx: &SyscallContext<'_>,
    domain: u32,
    socket_type: u32,
    _protocol: u32,
) -> Result<SyscallResult, Errno> {
    if domain != AF_UNIX {
        not_implemented!("unsupported socket domain {}", domain);
        return error!(EINVAL);
    }

    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let socket_type = socket_type & !(SOCK_NONBLOCK | SOCK_CLOEXEC);
    if !(socket_type == SOCK_STREAM || socket_type == SOCK_DGRAM || socket_type == SOCK_SEQPACKET) {
        not_implemented!("unsupported socket type 0x{:x}", socket_type);
        return error!(EINVAL);
    }

    let open_flags = OpenFlags::RDWR
        | if flags & SOCK_NONBLOCK != 0 { OpenFlags::NONBLOCK } else { OpenFlags::empty() };
    let socket_file = new_socket(ctx.kernel(), open_flags)?;

    let fd_flags = if flags & SOCK_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let fd = ctx.task.files.add_with_flags(socket_file, fd_flags)?;

    Ok(fd.into())
}

pub fn sys_bind(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    _address_length: u64,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }
    let socket_handle = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;

    // TODO: _address_length needs to be checked once we don't assume `sockaddr_un`.
    let user_ref: UserRef<sockaddr_un> = UserRef::new(user_socket_address);
    let mut address = sockaddr_un::default();
    ctx.task.mm.read_object(user_ref, &mut address)?;

    let path_len =
        address.sun_path.iter().position(|&r| r == b'\0').ok_or_else(|| errno!(EINVAL))?;
    let mode = ctx.task.fs.apply_umask(mode!(IFSOCK, 0o765));

    let (parent, basename) =
        ctx.task.lookup_parent_at(FdNumber::AT_FDCWD, &address.sun_path[..path_len])?;

    parent.entry.create_socket(basename, mode, socket_handle.clone()).map_err(|errno| {
        if errno == EEXIST {
            errno!(EADDRINUSE)
        } else {
            errno
        }
    })?;

    Ok(SUCCESS)
}
