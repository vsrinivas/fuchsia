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
    protocol: u32,
) -> Result<SyscallResult, Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let socket_file = create_socket(ctx, domain, socket_type, protocol, flags)?;

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

pub fn sys_connect(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    _address_length: u64,
) -> Result<SyscallResult, Errno> {
    let first_socket_file = ctx.task.files.get(fd)?;
    if !first_socket_file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    // TODO: Verify address_length.
    let user_ref: UserRef<sockaddr_un> = UserRef::new(user_socket_address);
    let mut address = sockaddr_un::default();
    ctx.task.mm.read_object(user_ref, &mut address)?;

    let path_len =
        address.sun_path.iter().position(|&r| r == b'\0').ok_or_else(|| errno!(EINVAL))?;

    let (parent, basename) =
        ctx.task.lookup_parent_at(FdNumber::AT_FDCWD, &address.sun_path[..path_len])?;
    let node = parent.lookup(&mut LookupContext::default(), ctx.task, basename)?;
    let second_socket_node = node.entry.node.clone();

    Socket::connect(first_socket_file.node(), &second_socket_node)
        .expect("socket connect failed, even after sockets were checked.");

    Ok(SUCCESS)
}

pub fn sys_socketpair(
    ctx: &SyscallContext<'_>,
    domain: u32,
    socket_type: u32,
    protocol: u32,
    user_sockets: UserRef<[FdNumber; 2]>,
) -> Result<SyscallResult, Errno> {
    // Create both the sockets. It's fine to call this twice, since both will either succeed or
    // fail. Put another way, there's no way the second socket file creation can error, if the first
    // succeeds, since they are created with the same options.
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let first_socket_file = create_socket(ctx, domain, socket_type, protocol, flags)?;
    let second_socket_file = create_socket(ctx, domain, socket_type, protocol, flags)?;

    Socket::connect(first_socket_file.node(), second_socket_file.node())
        .expect("socket connect failed, even after sockets were checked.");

    let fd_flags = if flags & SOCK_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    // TODO: Eventually this will need to allocate two inode numbers (each of which could
    // potentially fail), and only populate the inode numbers (which can't fail) if both allocations
    // succeed.
    let fd1 = ctx.task.files.add_with_flags(first_socket_file, fd_flags)?;
    let fd2 = ctx.task.files.add_with_flags(second_socket_file, fd_flags)?;

    let fds = [fd1, fd2];
    ctx.task.mm.write_object(user_sockets, &fds)?;

    Ok(SUCCESS)
}

pub fn sys_recvmsg(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_message_header: UserRef<msghdr>,
    _flags: u32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    // TODO: Respect the `flags` argument.

    let mut message_header = msghdr::default();
    ctx.task.mm.read_object(user_message_header.clone(), &mut message_header)?;

    let iovec = ctx.task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;
    let result = file.read(&ctx.task, &iovec)?;

    // TODO: These control messages should be written and read in-band.
    let socket = file.node().socket().map(|socket| socket.lock()).ok_or(errno!(ENOTSOCK))?;
    if let Some(control_message) =
        socket.connected_socket().and_then(|s| s.lock().take_control_message())
    {
        let max_bytes =
            std::cmp::min(control_message.len(), message_header.msg_controllen as usize);
        ctx.task.mm.write_memory(message_header.msg_control, &control_message[..max_bytes])?;
        message_header.msg_controllen = max_bytes as u64;
    } else {
        message_header.msg_controllen = 0;
    }
    ctx.task.mm.write_object(user_message_header, &message_header)?;
    Ok(result.into())
}

pub fn sys_sendmsg(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_message_header: UserRef<msghdr>,
    _flags: u32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    // TODO: Respect the `flags` argument.

    let mut message_header = msghdr::default();
    ctx.task.mm.read_object(user_message_header, &mut message_header)?;

    let mut bytes = vec![0u8; message_header.msg_controllen as usize];
    ctx.task.mm.read_memory(message_header.msg_control, &mut bytes)?;

    if let Some(socket) = file.node().socket() {
        socket.lock().set_control_message(bytes);
    }

    let iovec = ctx.task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;
    Ok(file.write(ctx.task, &iovec)?.into())
}

pub fn sys_shutdown(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    _how: i32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    // TODO: Respect the `how` argument.

    let mut socket = file.node().socket().ok_or(errno!(ENOTSOCK))?.lock();
    let _disconnected_node = socket.disconnect();

    Ok(SUCCESS)
}

/// Creates a socket with the provided configuration options, and returns the file that owns it.
///
/// The caller is responsible for adding the returned file to an `FdTable`. For example, by calling
/// `task.files.add_with_flags(file, ...);`.
///
/// # Parameters
/// - `domain`: The domain of the socket. Currently only `AF_UNIX` is supported.
/// - `socket_type`: The type of socket, for example `SOCK_STREAM` or `SOCK_DGRAM`.
/// - `protocol`: The protocol to be used with the socket, most commonly 0.
/// - `flags`: The socket flags that are used to determine the `OpenFlags` for the file.
fn create_socket(
    ctx: &SyscallContext<'_>,
    domain: u32,
    socket_type: u32,
    _protocol: u32,
    flags: u32,
) -> Result<FileHandle, Errno> {
    if domain != AF_UNIX {
        not_implemented!("unsupported socket domain {}", domain);
        return error!(EAFNOSUPPORT);
    }

    let socket_type = socket_type & 0xf;
    if !(socket_type == SOCK_STREAM || socket_type == SOCK_DGRAM || socket_type == SOCK_SEQPACKET) {
        not_implemented!("unsupported socket type 0x{:x}", socket_type);
        return error!(EPROTONOSUPPORT);
    }

    let open_flags = OpenFlags::RDWR
        | if flags & SOCK_NONBLOCK != 0 { OpenFlags::NONBLOCK } else { OpenFlags::empty() };
    Ok(new_socket(ctx.kernel(), open_flags))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mm::PAGE_SIZE;
    use crate::testing::*;
    use std::convert::TryInto;
    use std::sync::Arc;

    /// Creates a pair of connected sockets and returns the files associated with those sockets.
    ///
    /// The sockets are created as AF_UNIX/SOCK_STREAM sockets.
    fn create_socket_pair(ctx: &SyscallContext<'_>) -> Result<(FileHandle, FileHandle), Errno> {
        let socket_addr = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        let socket_ref: UserRef<[FdNumber; 2]> = UserRef::new(socket_addr);
        let mut socket_pair = [FdNumber::from_raw(0); 2];
        ctx.task.mm.write_object(socket_ref, &socket_pair).expect("");

        assert_eq!(sys_socketpair(&ctx, AF_UNIX, SOCK_STREAM, 0, socket_ref), Ok(SUCCESS));
        ctx.task.mm.read_object(socket_ref, &mut socket_pair).expect("");

        let first_file = ctx.task.files.get(socket_pair[0])?;
        let second_file = ctx.task.files.get(socket_pair[1])?;

        Ok((first_file, second_file))
    }

    /// Tests that two sockets created using `sys_socketpair` are indeed connected to each other.
    #[test]
    fn test_socketpair_connects_sockets() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let (first_file, second_file) =
            create_socket_pair(&ctx).expect("failed to create sockets.");

        let first_socket = first_file.node().socket().unwrap();
        let second_socket = second_file.node().socket().unwrap();
        let first_connected_socket = first_socket.lock().connected_socket().unwrap();
        let second_connected_socket = second_socket.lock().connected_socket().unwrap();

        assert!(Arc::ptr_eq(first_socket, &second_connected_socket));
        assert!(Arc::ptr_eq(second_socket, &first_connected_socket));
    }

    #[test]
    fn test_socketpair_invalid_arguments() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        assert_eq!(
            sys_socketpair(&ctx, AF_INET, SOCK_STREAM, 0, UserRef::new(UserAddress::default())),
            Err(EAFNOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(&ctx, AF_UNIX, SOCK_RAW, 0, UserRef::new(UserAddress::default())),
            Err(EPROTONOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(&ctx, AF_UNIX, SOCK_STREAM, 0, UserRef::new(UserAddress::default())),
            Err(EFAULT)
        );
    }

    #[test]
    fn test_connect_same_socket() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let fd1 = match sys_socket(&ctx, AF_UNIX, SOCK_STREAM, 0) {
            Ok(SyscallResult::Success(fd)) => fd,
            _ => panic!("Failed to create first socket"),
        };

        let file = ctx
            .task
            .files
            .get(FdNumber::from_raw(fd1.try_into().unwrap()))
            .expect("Failed to fetch socket file.");
        assert!(Socket::connect(file.node(), file.node()).is_ok());
    }
}
