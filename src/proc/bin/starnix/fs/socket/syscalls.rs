// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_cprng::cprng_draw;
use std::convert::TryInto;
use std::sync::Arc;
use zerocopy::AsBytes;

use super::*;
use crate::errno;
use crate::error;
use crate::fs::buffers::*;
use crate::fs::*;
use crate::mode;
use crate::not_implemented;
use crate::syscalls::*;
use crate::task::*;
use crate::types::locking::*;
use crate::types::*;

pub fn sys_socket(
    ctx: &SyscallContext<'_>,
    domain: u32,
    socket_type: u32,
    _protocol: u32,
) -> Result<SyscallResult, Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let domain = parse_socket_domain(domain)?;
    let socket_type = parse_socket_type(socket_type)?;
    let open_flags = socket_flags_to_open_flags(flags);

    let socket_file = Socket::new_file(ctx.kernel(), Socket::new(domain, socket_type), open_flags);

    let fd_flags = socket_flags_to_fd_flags(flags);
    let fd = ctx.task.files.add_with_flags(socket_file, fd_flags)?;

    Ok(fd.into())
}

fn socket_flags_to_open_flags(flags: u32) -> OpenFlags {
    OpenFlags::RDWR
        | if flags & SOCK_NONBLOCK != 0 { OpenFlags::NONBLOCK } else { OpenFlags::empty() }
}

fn socket_flags_to_fd_flags(flags: u32) -> FdFlags {
    if flags & SOCK_CLOEXEC != 0 {
        FdFlags::CLOEXEC
    } else {
        FdFlags::empty()
    }
}

fn parse_socket_domain(domain: u32) -> Result<SocketDomain, Errno> {
    SocketDomain::from_raw(domain.try_into().map_err(|_| errno!(EAFNOSUPPORT))?).ok_or_else(|| {
        not_implemented!("unsupported socket domain {}", domain);
        errno!(EAFNOSUPPORT)
    })
}

fn parse_socket_type(socket_type: u32) -> Result<SocketType, Errno> {
    SocketType::from_raw(socket_type & 0xf).ok_or_else(|| {
        not_implemented!("unsupported socket type 0x{:x}", socket_type);
        errno!(EPROTONOSUPPORT)
    })
}

fn parse_socket_address(
    task: &Task,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<SocketAddress, Errno> {
    if address_length < SA_FAMILY_SIZE {
        return error!(EINVAL);
    }

    let mut address = vec![0u8; address_length];
    task.mm.read_memory(user_socket_address, &mut address)?;

    let mut family_bytes = [0u8; SA_FAMILY_SIZE];
    family_bytes[..SA_FAMILY_SIZE].copy_from_slice(&address[..SA_FAMILY_SIZE]);
    let family = uapi::__kernel_sa_family_t::from_ne_bytes(family_bytes);

    let address = match family {
        AF_UNIX => {
            let template = sockaddr_un::default();
            let sun_path = &address[SA_FAMILY_SIZE..];
            if sun_path.len() > template.sun_path.len() {
                return error!(EINVAL);
            }
            if sun_path.len() == 0 {
                // Possibly an autobind address, depending on context.
                SocketAddress::Unix(vec![])
            } else {
                let null_index =
                    sun_path.iter().position(|&r| r == b'\0').ok_or_else(|| errno!(EINVAL))?;
                if null_index == 0 {
                    // If there is a null byte at the start of the sun_path, then the
                    // address is abstract.
                    SocketAddress::Unix(sun_path.to_vec())
                } else {
                    // Otherwise, the name is a path.
                    SocketAddress::Unix(sun_path[..null_index].to_vec())
                }
            }
        }
        _ => return error!(EAFNOSUPPORT),
    };
    Ok(address)
}

// See "Autobind feature" section of https://man7.org/linux/man-pages/man7/unix.7.html
fn generate_autobind_address() -> Vec<u8> {
    let mut bytes = [0u8; 4];
    cprng_draw(&mut bytes);
    let value = u32::from_ne_bytes(bytes) & 0xFFFFF;
    format!("\0{:05x}", value).into_bytes()
}

fn translate_fs_error(errno: Errno) -> Errno {
    match errno {
        errno if errno == EACCES || errno == EPERM || errno == EINTR => errno,
        _ => errno!(ECONNREFUSED),
    }
}

pub fn sys_bind(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(&ctx.task, user_socket_address, address_length)?;
    match address {
        SocketAddress::Unspecified => return error!(EAFNOSUPPORT),
        SocketAddress::Unix(mut name) => {
            if name.is_empty() {
                // If the name is empty, then we're supposed to generate an
                // autobind address, which is always abstract.
                name = generate_autobind_address();
            }
            // If there is a null byte at the start of the sun_path, then the
            // address is abstract.
            if name[0] == b'\0' {
                ctx.task.abstract_socket_namespace.bind(name, socket)?;
            } else {
                let mode = ctx.task.fs.apply_umask(mode!(IFSOCK, 0o765));
                let (parent, basename) = ctx
                    .task
                    .lookup_parent_at(FdNumber::AT_FDCWD, &name)
                    .map_err(translate_fs_error)?;

                let _dir_entry = parent
                    .entry
                    .bind_socket(basename, socket.clone(), SocketAddress::Unix(name.clone()), mode)
                    .map_err(|errno| if errno == EEXIST { errno!(EADDRINUSE) } else { errno })?;
            }
        }
    }

    Ok(SUCCESS)
}

pub fn sys_listen(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    backlog: u32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    socket.lock().listen(backlog)?;
    Ok(SUCCESS)
}

pub fn sys_accept(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    sys_accept4(ctx, fd, user_socket_address, user_address_length, 0)
}

pub fn sys_accept4(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
    flags: u32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let accepted_socket = file.blocking_op(
        &ctx.task,
        || socket.lock().accept(),
        FdEvents::POLLIN | FdEvents::POLLHUP,
    )?;

    if !user_socket_address.is_null() {
        let address_bytes = accepted_socket.lock().getpeername()?;
        write_socket_address(&ctx.task, user_socket_address, user_address_length, &address_bytes)?;
    }

    let open_flags = socket_flags_to_open_flags(flags);
    let accepted_socket_file = Socket::new_file(ctx.kernel(), accepted_socket, open_flags);
    let fd_flags = if flags & SOCK_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let accepted_fd = ctx.task.files.add_with_flags(accepted_socket_file, fd_flags)?;
    Ok(accepted_fd.into())
}

pub fn sys_connect(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<SyscallResult, Errno> {
    let client_file = ctx.task.files.get(fd)?;
    let client_socket = client_file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(&ctx.task, user_socket_address, address_length)?;

    let passive_socket = match address {
        SocketAddress::Unspecified => return error!(EAFNOSUPPORT),
        SocketAddress::Unix(name) => {
            if name.is_empty() {
                return error!(ECONNREFUSED);
            }
            if name[0] == b'\0' {
                ctx.task.abstract_socket_namespace.lookup(&name)?
            } else {
                let (parent, basename) = ctx.task.lookup_parent_at(FdNumber::AT_FDCWD, &name)?;
                let name = parent
                    .lookup_child(&mut LookupContext::default(), ctx.task, basename)
                    .map_err(translate_fs_error)?;
                name.entry.node.socket().ok_or_else(|| errno!(ECONNREFUSED))?.clone()
            }
        }
    };

    connect(client_socket, &passive_socket)?;
    Ok(SUCCESS)
}

fn write_socket_address(
    task: &Task,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
    address_bytes: &[u8],
) -> Result<(), Errno> {
    let mut capacity = 0;
    task.mm.read_object(user_address_length, &mut capacity)?;
    if capacity > i32::MAX as socklen_t {
        return error!(EINVAL);
    }
    let length = address_bytes.len() as socklen_t;
    let actual = std::cmp::min(length, capacity) as usize;
    task.mm.write_memory(user_socket_address, &address_bytes[..actual])?;
    task.mm.write_object(user_address_length, &length)?;
    Ok(())
}

pub fn sys_getsockname(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let locked_socket = socket.lock();
    let address_bytes = locked_socket.getsockname();

    write_socket_address(&ctx.task, user_socket_address, user_address_length, &address_bytes)?;

    Ok(SUCCESS)
}

pub fn sys_getpeername(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address_bytes = socket.lock().getpeername()?;

    write_socket_address(&ctx.task, user_socket_address, user_address_length, &address_bytes)?;

    Ok(SUCCESS)
}

pub fn sys_socketpair(
    ctx: &SyscallContext<'_>,
    domain: u32,
    socket_type: u32,
    _protocol: u32,
    user_sockets: UserRef<[FdNumber; 2]>,
) -> Result<SyscallResult, Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let domain = parse_socket_domain(domain)?;
    let socket_type = parse_socket_type(socket_type)?;
    let open_flags = socket_flags_to_open_flags(flags);

    let (left, right) = Socket::new_pair(domain, socket_type);

    let left_file = Socket::new_file(ctx.kernel(), left, open_flags);
    let right_file = Socket::new_file(ctx.kernel(), right, open_flags);

    let fd_flags = socket_flags_to_fd_flags(flags);
    // TODO: Eventually this will need to allocate two fd numbers (each of which could
    // potentially fail), and only populate the fd numbers (which can't fail) if both allocations
    // succeed.
    let left_fd = ctx.task.files.add_with_flags(left_file, fd_flags)?;
    let right_fd = ctx.task.files.add_with_flags(right_file, fd_flags)?;

    let fds = [left_fd, right_fd];
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
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let (bytes_read, _address, ancillary_data) = socket_ops.recvmsg(ctx.task, &file, &iovec)?;

    if let Some(ancillary_data) = ancillary_data {
        let mut num_bytes_to_write = message_header.msg_controllen as usize;
        if !ancillary_data.can_fit_all_data(num_bytes_to_write) {
            // If not all data can fit, set the MSG_CTRUNC flag.
            message_header.msg_flags = MSG_CTRUNC;
            if !ancillary_data.can_fit_any_data(num_bytes_to_write) {
                // If the length is not large enough to fit any real data, set the number of bytes
                // to write to 0.
                num_bytes_to_write = 0;
            }
        }

        let mut control_message_header = ancillary_data.into_cmsghdr(ctx.task)?;

        // Cap the number of bytes to write at the actual length of the control message.
        num_bytes_to_write = std::cmp::min(num_bytes_to_write, control_message_header.cmsg_len);
        // Set the cmsg_len to the actual number of bytes written.
        control_message_header.cmsg_len = num_bytes_to_write;

        ctx.task.mm.write_memory(
            message_header.msg_control,
            &control_message_header.as_bytes()[..num_bytes_to_write],
        )?;

        message_header.msg_controllen = num_bytes_to_write;
    } else {
        // If there is no control message, make sure to clear the length.
        message_header.msg_controllen = 0;
    }
    ctx.task.mm.write_object(user_message_header, &message_header)?;

    Ok(bytes_read.into())
}

pub fn sys_recvfrom(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_buffer: UserAddress,
    buffer_length: usize,
    _flags: u32,
    user_src_address: UserAddress,
    user_src_address_length: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    // TODO: Respect the `flags` argument.

    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let (bytes_read, address, _control) = socket_ops.recvmsg(
        ctx.task,
        &file,
        &[UserBuffer { address: user_buffer, length: buffer_length }],
    )?;

    if !user_src_address.is_null() {
        if let Some(address) = address {
            write_socket_address(
                &ctx.task,
                user_src_address,
                user_src_address_length,
                &address.to_bytes(),
            )?;
        } else {
            ctx.task.mm.write_object(user_src_address_length, &0)?;
        }
    }

    Ok(bytes_read.into())
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

    let ancillary_data = if message_header.msg_controllen > 0 {
        let mut control_message_header = cmsghdr::default();
        ctx.task
            .mm
            .read_object(UserRef::new(message_header.msg_control), &mut control_message_header)?;
        Some(AncillaryData::new(ctx.task, control_message_header)?)
    } else {
        None
    };

    let iovec = ctx.task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let bytes_sent = socket_ops.sendmsg(ctx.task, &file, &iovec, ancillary_data)?;
    Ok(bytes_sent.into())
}

pub fn sys_sendto(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_buffer: UserAddress,
    buffer_length: usize,
    _flags: u32,
    user_dest_address: UserAddress,
    _user_address_length: socklen_t,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    // TODO: Respect the `flags` argument.

    if !user_dest_address.is_null() {
        not_implemented!("sendto: non-null destination address");
        return error!(ENOSYS);
    }
    Ok(file.write(&ctx.task, &[UserBuffer { address: user_buffer, length: buffer_length }])?.into())
}

pub fn sys_getsockopt(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    level: u32,
    optname: u32,
    user_optval: UserAddress,
    user_optlen: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    match level {
        SOL_SOCKET => match optname {
            SO_TYPE => {
                let mut optlen = 0;
                ctx.task.mm.read_object(user_optlen, &mut optlen)?;
                let raw = socket.lock().socket_type().as_raw();
                let actual_optlen = std::mem::size_of_val(&raw) as socklen_t;
                if optlen < actual_optlen {
                    return error!(EINVAL);
                }
                ctx.task.mm.write_memory(user_optval, &raw.to_ne_bytes())?;
                ctx.task.mm.write_object(user_optlen, &actual_optlen)?;
                Ok(SUCCESS)
            }
            _ => error!(ENOPROTOOPT),
        },
        _ => error!(ENOPROTOOPT),
    }
}

pub fn sys_setsockopt(
    _ctx: &SyscallContext<'_>,
    _fd: FdNumber,
    _level: u32,
    _optname: u32,
    _optval: UserAddress,
    _optlen: socklen_t,
) -> Result<SyscallResult, Errno> {
    error!(ENOSYS)
}

pub fn sys_shutdown(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    _how: i32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;

    // TODO: Respect the `how` argument.

    let socket = file.node().socket().ok_or(errno!(ENOTSOCK))?;
    socket.lock().shutdown()?;

    Ok(SUCCESS)
}

fn connect(client_socket: &SocketHandle, passive_socket: &SocketHandle) -> Result<(), Errno> {
    if Arc::ptr_eq(client_socket, passive_socket) {
        return error!(EINVAL);
    }

    // Sort the nodes to determine which order to lock them in.
    let mut ordered_sockets: [&SocketHandle; 2] = [client_socket, passive_socket];
    sort_for_locking(&mut ordered_sockets, |socket| socket);

    let first_socket = ordered_sockets[0];
    let second_socket = ordered_sockets[1];

    let mut first_socket_locked = first_socket.lock();
    let mut second_socket_locked = second_socket.lock();

    if std::ptr::eq(passive_socket, first_socket) {
        // The first socket is the passive node, so connect *to* it.
        second_socket_locked.connect(&mut first_socket_locked)
    } else {
        first_socket_locked.connect(&mut second_socket_locked)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;
    use std::convert::TryInto;

    #[test]
    fn test_socketpair_invalid_arguments() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        assert_eq!(
            sys_socketpair(
                &ctx,
                AF_INET as u32,
                SOCK_STREAM,
                0,
                UserRef::new(UserAddress::default())
            ),
            Err(EAFNOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(&ctx, AF_UNIX as u32, SOCK_RAW, 0, UserRef::new(UserAddress::default())),
            Err(EPROTONOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(
                &ctx,
                AF_UNIX as u32,
                SOCK_STREAM,
                0,
                UserRef::new(UserAddress::default())
            ),
            Err(EFAULT)
        );
    }

    #[test]
    fn test_connect_same_socket() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let fd1 = match sys_socket(&ctx, AF_UNIX as u32, SOCK_STREAM, 0) {
            Ok(SyscallResult::Success(fd)) => fd,
            _ => panic!("Failed to create first socket"),
        };

        let file = ctx
            .task
            .files
            .get(FdNumber::from_raw(fd1.try_into().unwrap()))
            .expect("Failed to fetch socket file.");
        assert_eq!(
            connect(file.node().socket().unwrap(), file.node().socket().unwrap()),
            Err(EINVAL)
        );
    }

    #[test]
    fn test_generate_autobind_address() {
        let address = generate_autobind_address();
        assert_eq!(address.len(), 6);
        assert_eq!(address[0], 0);
        for byte in address[1..].iter() {
            match byte {
                b'0'..=b'9' | b'a'..=b'f' => {
                    // Ok.
                }
                bad => {
                    assert!(false, "bad byte: {}", bad);
                }
            }
        }
    }
}
