// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_cprng::cprng_draw;
use std::convert::TryInto;

use super::*;
use crate::errno;
use crate::error;
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
    protocol: u32,
) -> Result<SyscallResult, Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let socket_file = create_socket(ctx, domain, socket_type, protocol, flags)?;

    let fd_flags = if flags & SOCK_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let fd = ctx.task.files.add_with_flags(socket_file, fd_flags)?;

    Ok(fd.into())
}

fn parse_socket_address(
    task: &Task,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<SocketAddress, Errno> {
    const ADDRESS_FAMILY_SIZE: usize = std::mem::size_of::<uapi::__kernel_sa_family_t>();
    if address_length < ADDRESS_FAMILY_SIZE {
        return error!(EINVAL);
    }

    let mut address = vec![0u8; address_length];
    task.mm.read_memory(user_socket_address, &mut address)?;

    let mut family_bytes = [0u8; ADDRESS_FAMILY_SIZE];
    family_bytes[..ADDRESS_FAMILY_SIZE].copy_from_slice(&address[..ADDRESS_FAMILY_SIZE]);
    let family = uapi::__kernel_sa_family_t::from_ne_bytes(family_bytes);

    let address = match family {
        AF_UNIX => {
            let template = sockaddr_un::default();
            let sun_path = &address[ADDRESS_FAMILY_SIZE..];
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
    format!("\0{:0x}", value).into_bytes()
}

pub fn sys_bind(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(&ctx.task, user_socket_address, address_length)?;
    match address {
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
                // TODO: Is lookup_parent_at returning the right errors?
                let (parent, basename) = ctx.task.lookup_parent_at(FdNumber::AT_FDCWD, &name)?;

                let _dir_entry = parent
                    .entry
                    .bind_socket(basename, socket.clone(), SocketAddress::Unix(name.clone()), mode)
                    .map_err(|errno| if errno == EEXIST { errno!(EADDRINUSE) } else { errno })?;
            }
        }
        _ => return error!(EAFNOSUPPORT),
    }

    Ok(SUCCESS)
}

pub fn sys_connect(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<SyscallResult, Errno> {
    // The "active" socket is the one performing the connection.
    let active_socket_file = ctx.task.files.get(fd)?;
    let active_socket = active_socket_file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(&ctx.task, user_socket_address, address_length)?;

    let passive_socket = match address {
        SocketAddress::Unix(name) => {
            if name.is_empty() {
                return error!(ECONNREFUSED);
            }
            if name[0] == b'\0' {
                ctx.task.abstract_socket_namespace.lookup(&name)?
            } else {
                let (parent, basename) = ctx.task.lookup_parent_at(FdNumber::AT_FDCWD, &name)?;
                let name =
                    parent.lookup_child(&mut LookupContext::default(), ctx.task, basename)?;
                name.entry.node.socket().ok_or_else(|| errno!(ECONNREFUSED))?.clone()
            }
        }
        _ => return error!(EAFNOSUPPORT),
    };

    connect_sockets(&passive_socket, active_socket)?;
    Ok(SUCCESS)
}

fn write_socket_address(
    task: &Task,
    user_socket_address: UserAddress,
    user_address_length: UserRef<usize>,
    address_bytes: &[u8],
) -> Result<(), Errno> {
    let mut address_length = 0;
    task.mm.read_object(user_address_length, &mut address_length)?;
    let byte_count = std::cmp::min(address_bytes.len(), address_length);
    task.mm.write_memory(user_socket_address, &address_bytes[..byte_count])?;
    task.mm.write_object(user_address_length, &address_bytes.len())?;
    Ok(())
}

pub fn sys_getsockname(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<usize>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }
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
    user_address_length: UserRef<usize>,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address_bytes = socket.lock().getpeername()?;

    write_socket_address(&ctx.task, user_socket_address, user_address_length, &address_bytes)?;

    Ok(SUCCESS)
}

pub fn sys_socketpair(
    ctx: &SyscallContext<'_>,
    raw_domain: u32,
    socket_type: u32,
    protocol: u32,
    user_sockets: UserRef<[FdNumber; 2]>,
) -> Result<SyscallResult, Errno> {
    // Create both the sockets. It's fine to call this twice, since both will either succeed or
    // fail. Put another way, there's no way the second socket file creation can error, if the first
    // succeeds, since they are created with the same options.
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let first_socket_file = create_socket(ctx, raw_domain, socket_type, protocol, flags)?;
    let second_socket_file = create_socket(ctx, raw_domain, socket_type, protocol, flags)?;

    {
        let mut first_socket = first_socket_file.node().socket().unwrap().lock();
        let mut second_socket = second_socket_file.node().socket().unwrap().lock();
        first_socket.bind(SocketAddress::Unspecified)?;
        second_socket.connect(&mut first_socket)?;
    }

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
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let (bytes_read, control) = socket_ops.recvmsg(ctx.task, &file, &iovec)?;

    if let Some(control) = control {
        let max_bytes = std::cmp::min(control.len(), message_header.msg_controllen as usize);
        ctx.task.mm.write_memory(message_header.msg_control, &control.bytes()[..max_bytes])?;
        message_header.msg_controllen = max_bytes as u64;
    } else {
        // If there is no control message, make sure to clear the length.
        message_header.msg_controllen = 0;
    }
    ctx.task.mm.write_object(user_message_header, &message_header)?;

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
    let control = if message_header.msg_controllen > 0 {
        let mut bytes = vec![0u8; message_header.msg_controllen as usize];
        ctx.task.mm.read_memory(message_header.msg_control, &mut bytes)?;
        Some(bytes)
    } else {
        None
    };

    let iovec = ctx.task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let bytes_sent = socket_ops.sendmsg(ctx.task, &file, &iovec, control)?;
    Ok(bytes_sent.into())
}

pub fn sys_shutdown(
    ctx: &SyscallContext<'_>,
    fd: FdNumber,
    _how: i32,
) -> Result<SyscallResult, Errno> {
    let file = ctx.task.files.get(fd)?;

    // TODO: Respect the `how` argument.

    let socket = file.node().socket().ok_or(errno!(ENOTSOCK))?;
    let connected_node = socket.lock().shutdown()?;
    let _ = connected_node.map(|node| node.socket().unwrap().lock().shutdown());

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
    raw_domain: u32,
    socket_type: u32,
    _protocol: u32,
    flags: u32,
) -> Result<FileHandle, Errno> {
    let domain = match raw_domain.try_into().map_err(|_| errno!(EAFNOSUPPORT))? {
        AF_UNIX => SocketDomain::Unix,
        _ => {
            not_implemented!("unsupported socket domain {}", raw_domain);
            return error!(EAFNOSUPPORT);
        }
    };

    let socket_type = socket_type & 0xf;
    if !(socket_type == SOCK_STREAM || socket_type == SOCK_DGRAM || socket_type == SOCK_SEQPACKET) {
        not_implemented!("unsupported socket type 0x{:x}", socket_type);
        return error!(EPROTONOSUPPORT);
    }

    let open_flags = OpenFlags::RDWR
        | if flags & SOCK_NONBLOCK != 0 { OpenFlags::NONBLOCK } else { OpenFlags::empty() };

    Ok(Socket::new_file(ctx.kernel(), domain, open_flags))
}

/// Connects the sockets in the provided `FsNodeHandle`s together.
///
/// After the connection has been established, the sockets in each node will read/write from
/// each other.
///
/// WARNING: It's an error to call `connect` with nodes that do not contain sockets.
///
/// # Parameters
/// - `passive_socket`: The socket that is being connected *to*.
/// - `active_socket`: The socket that is performing the connection.
///
/// Returns an error if any of the sockets could not be connected.
fn connect_sockets(
    passive_socket: &SocketHandle,
    active_socket: &SocketHandle,
) -> Result<(), Errno> {
    if std::ptr::eq(passive_socket, active_socket) {
        return error!(ECONNREFUSED);
    }

    // Sort the nodes to determine which order to lock them in.
    let mut ordered_sockets: [&SocketHandle; 2] = [active_socket, passive_socket];
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
            connect_sockets(file.node().socket().unwrap(), file.node().socket().unwrap()),
            Err(ECONNREFUSED)
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
