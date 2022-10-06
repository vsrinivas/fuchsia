// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use std::convert::TryInto;
use zerocopy::{AsBytes, FromBytes};

use super::*;
use crate::fs::buffers::*;
use crate::fs::*;
use crate::logging::{not_implemented, strace};
use crate::mm::vmo::round_up_to_increment;
use crate::task::*;
use crate::types::*;

pub fn sys_socket(
    current_task: &CurrentTask,
    domain: u32,
    socket_type: u32,
    protocol: u32,
) -> Result<FdNumber, Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let domain = parse_socket_domain(current_task, domain)?;
    let socket_type = parse_socket_type(current_task, domain, socket_type)?;
    let protocol = SocketProtocol::from_raw(protocol);
    let open_flags = socket_flags_to_open_flags(flags);
    let socket_file =
        Socket::new_file(current_task, Socket::new(domain, socket_type, protocol)?, open_flags);

    let fd_flags = socket_flags_to_fd_flags(flags);
    let fd = current_task.files.add_with_flags(socket_file, fd_flags)?;

    Ok(fd)
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

fn parse_socket_domain(current_task: &CurrentTask, domain: u32) -> Result<SocketDomain, Errno> {
    SocketDomain::from_raw(domain.try_into().map_err(|_| errno!(EAFNOSUPPORT))?).ok_or_else(|| {
        not_implemented!(current_task, "unsupported socket domain {}", domain);
        errno!(EAFNOSUPPORT)
    })
}

fn parse_socket_type(
    current_task: &CurrentTask,
    domain: SocketDomain,
    socket_type: u32,
) -> Result<SocketType, Errno> {
    let socket_type = SocketType::from_raw(socket_type & 0xf).ok_or_else(|| {
        not_implemented!(current_task, "unsupported socket type 0x{:x}", socket_type);
        errno!(EPROTONOSUPPORT)
    })?;
    // For AF_UNIX, SOCK_RAW sockets are treated as if they were SOCK_DGRAM.
    Ok(if domain == SocketDomain::Unix && socket_type == SocketType::Raw {
        SocketType::Datagram
    } else {
        socket_type
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
            if sun_path.is_empty() {
                // Possibly an autobind address, depending on context.
                SocketAddress::Unix(vec![])
            } else {
                let null_index =
                    sun_path.iter().position(|&r| r == b'\0').unwrap_or(sun_path.len());
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
        AF_VSOCK => {
            let vsock_address = sockaddr_vm::read_from(&*address);
            if let Some(address) = vsock_address {
                SocketAddress::Vsock(address.svm_port)
            } else {
                SocketAddress::Unspecified
            }
        }
        AF_INET => {
            let sockaddr_len = std::mem::size_of::<sockaddr_in>();
            let addrlen = std::cmp::min(address_length, sockaddr_len);
            SocketAddress::Inet(address[..addrlen].to_vec())
        }
        AF_INET6 => {
            let sockaddr_len = std::mem::size_of::<sockaddr_in6>();
            let addrlen = std::cmp::min(address_length, sockaddr_len);
            SocketAddress::Inet6(address[..addrlen].to_vec())
        }
        AF_NETLINK => SocketAddress::default_for_domain(SocketDomain::Netlink),
        _ => SocketAddress::Unspecified,
    };
    Ok(address)
}

fn maybe_parse_socket_address(
    task: &Task,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<Option<SocketAddress>, Errno> {
    Ok(if user_socket_address.is_null() {
        None
    } else {
        Some(parse_socket_address(task, user_socket_address, address_length)?)
    })
}

// See "Autobind feature" section of https://man7.org/linux/man-pages/man7/unix.7.html
fn generate_autobind_address() -> Vec<u8> {
    let mut bytes = [0u8; 4];
    zx::cprng_draw(&mut bytes);
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
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(current_task, user_socket_address, address_length)?;
    if !address.valid_for_domain(socket.domain) {
        return error!(EINVAL);
    }
    match address {
        SocketAddress::Unspecified => return error!(EINVAL),
        SocketAddress::Unix(mut name) => {
            if name.is_empty() {
                // If the name is empty, then we're supposed to generate an
                // autobind address, which is always abstract.
                name = generate_autobind_address();
            }
            // If there is a null byte at the start of the sun_path, then the
            // address is abstract.
            if name[0] == b'\0' {
                current_task.abstract_socket_namespace.bind(name, socket)?;
            } else {
                let mode = file.node().info().mode;
                let mode = current_task.fs().apply_umask(mode).with_type(FileMode::IFSOCK);
                let (parent, basename) =
                    current_task.lookup_parent_at(FdNumber::AT_FDCWD, &name)?;

                let _dir_entry = parent
                    .entry
                    .bind_socket(
                        current_task,
                        basename,
                        socket.clone(),
                        SocketAddress::Unix(name.clone()),
                        mode,
                        current_task.as_fscred(),
                    )
                    .map_err(|errno| if errno == EEXIST { errno!(EADDRINUSE) } else { errno })?;
            }
        }
        SocketAddress::Vsock(port) => {
            current_task.abstract_vsock_namespace.bind(port, socket)?;
        }
        SocketAddress::Inet(_) | SocketAddress::Inet6(_) => socket.bind(address)?,
        SocketAddress::Netlink(_) => {
            socket.bind(SocketAddress::default_for_domain(SocketDomain::Netlink))?
        }
    }

    Ok(())
}

pub fn sys_listen(current_task: &CurrentTask, fd: FdNumber, backlog: i32) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    socket.listen(backlog, current_task.as_ucred())?;
    Ok(())
}

pub fn sys_accept(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<FdNumber, Errno> {
    sys_accept4(current_task, fd, user_socket_address, user_address_length, 0)
}

pub fn sys_accept4(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
    flags: u32,
) -> Result<FdNumber, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let accepted_socket = file.blocking_op(
        current_task,
        || socket.accept().map(BlockableOpsResult::Done),
        FdEvents::POLLIN | FdEvents::POLLHUP,
        None,
    )?;

    if !user_socket_address.is_null() {
        let address_bytes = accepted_socket.getpeername()?;
        write_socket_address(
            current_task,
            user_socket_address,
            user_address_length,
            &address_bytes,
        )?;
    }

    let open_flags = socket_flags_to_open_flags(flags);
    let accepted_socket_file = Socket::new_file(current_task, accepted_socket, open_flags);
    let fd_flags = if flags & SOCK_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let accepted_fd = current_task.files.add_with_flags(accepted_socket_file, fd_flags)?;
    Ok(accepted_fd)
}

pub fn sys_connect(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<(), Errno> {
    let client_file = current_task.files.get(fd)?;
    let client_socket = client_file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(current_task, user_socket_address, address_length)?;
    let peer = match address {
        SocketAddress::Unspecified => return error!(ECONNREFUSED),
        SocketAddress::Unix(ref name) => {
            strace!(
                &current_task,
                "connect to unix socket named {:?}",
                String::from_utf8_lossy(name)
            );
            if name.is_empty() {
                return error!(ECONNREFUSED);
            }
            if name[0] == b'\0' {
                SocketPeer::Handle(current_task.abstract_socket_namespace.lookup(name)?)
            } else {
                let (parent, basename) = current_task.lookup_parent_at(FdNumber::AT_FDCWD, name)?;
                let name = parent
                    .lookup_child(current_task, &mut LookupContext::default(), basename)
                    .map_err(translate_fs_error)?;
                SocketPeer::Handle(
                    name.entry.node.socket().ok_or_else(|| errno!(ECONNREFUSED))?.clone(),
                )
            }
        }
        // Connect not available for AF_VSOCK
        SocketAddress::Vsock(_) => return error!(ENOSYS),
        SocketAddress::Inet(ref addr) | SocketAddress::Inet6(ref addr) => {
            strace!(
                &current_task,
                "connect to inet socket named {:?}",
                String::from_utf8_lossy(addr)
            );
            SocketPeer::Address(address)
        }
        SocketAddress::Netlink(_) => return error!(ENOSYS),
    };

    // TODO(tbodt): Support blocking when the UNIX domain socket queue fills up. This one's weird
    // because as far as I can tell, removing a socket from the queue does not actually trigger
    // FdEvents on anything.
    client_socket.connect(peer, current_task.as_ucred())?;
    Ok(())
}

fn write_socket_address(
    task: &Task,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
    address_bytes: &[u8],
) -> Result<(), Errno> {
    let capacity = task.mm.read_object(user_address_length)?;
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
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address_bytes = socket.getsockname();

    write_socket_address(current_task, user_socket_address, user_address_length, &address_bytes)?;

    Ok(())
}

pub fn sys_getpeername(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address_bytes = socket.getpeername()?;

    write_socket_address(current_task, user_socket_address, user_address_length, &address_bytes)?;

    Ok(())
}

pub fn sys_socketpair(
    current_task: &CurrentTask,
    domain: u32,
    socket_type: u32,
    _protocol: u32,
    user_sockets: UserRef<[FdNumber; 2]>,
) -> Result<(), Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let domain = parse_socket_domain(current_task, domain)?;
    if domain != SocketDomain::Unix {
        return error!(EAFNOSUPPORT);
    }
    let socket_type = parse_socket_type(current_task, domain, socket_type)?;
    let open_flags = socket_flags_to_open_flags(flags);

    let (left, right) = UnixSocket::new_pair(current_task, domain, socket_type, open_flags)?;

    let fd_flags = socket_flags_to_fd_flags(flags);
    // TODO: Eventually this will need to allocate two fd numbers (each of which could
    // potentially fail), and only populate the fd numbers (which can't fail) if both allocations
    // succeed.
    let left_fd = current_task.files.add_with_flags(left, fd_flags)?;
    let right_fd = current_task.files.add_with_flags(right, fd_flags)?;

    let fds = [left_fd, right_fd];
    strace!(current_task, "socketpair -> [{:#x}, {:#x}]", fds[0].raw(), fds[1].raw());
    current_task.mm.write_object(user_sockets, &fds)?;

    Ok(())
}

fn recvmsg_internal(
    current_task: &CurrentTask,
    file: &FileHandle,
    user_message_header: UserRef<msghdr>,
    flags: u32,
    deadline: Option<zx::Time>,
) -> Result<usize, Errno> {
    let mut message_header = current_task.mm.read_object(user_message_header.clone())?;
    let iovec =
        current_task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;

    let flags = SocketMessageFlags::from_bits_truncate(flags);
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let info = socket_ops.recvmsg(current_task, file, &iovec, flags, deadline)?;

    message_header.msg_flags = 0;

    let cmsg_buffer_size = message_header.msg_controllen as usize;
    let mut cmsg_bytes_written = 0;
    let header_size = std::mem::size_of::<cmsghdr>();

    for ancillary_data in info.ancillary_data {
        if ancillary_data.total_size() == 0 {
            // Skip zero-byte ancillary data on the receiving end. Not doing this trips this
            // assert:
            // https://cs.android.com/android/platform/superproject/+/master:system/libbase/cmsg.cpp;l=144;drc=15ec2c7a23cda814351a064a345a8270ed8c83ab
            continue;
        }

        let expected_size = header_size + ancillary_data.total_size();
        let message_bytes = ancillary_data.into_bytes(
            current_task,
            flags,
            cmsg_buffer_size - cmsg_bytes_written,
        )?;

        // If the message is smaller than expected, set the MSG_CTRUNC flag, so the caller can tell
        // some of the message is missing.
        let truncated = message_bytes.len() < expected_size;
        if truncated {
            message_header.msg_flags |= MSG_CTRUNC as u64;
        }

        if message_bytes.len() < header_size {
            // Can't fit the header, so stop trying to write.
            break;
        }

        if !message_bytes.is_empty() {
            current_task
                .mm
                .write_memory(message_header.msg_control + cmsg_bytes_written, &message_bytes)?;
            cmsg_bytes_written += message_bytes.len();
            if !truncated {
                cmsg_bytes_written =
                    round_up_to_increment(cmsg_bytes_written, std::mem::size_of::<usize>())?;
            }
        }
    }

    message_header.msg_controllen = cmsg_bytes_written;

    // TODO: Handle info.address.

    if info.bytes_read != info.message_length {
        message_header.msg_flags |= MSG_TRUNC as u64;
    }

    current_task.mm.write_object(user_message_header, &message_header)?;

    if flags.contains(SocketMessageFlags::TRUNC) {
        Ok(info.message_length)
    } else {
        Ok(info.bytes_read)
    }
}

pub fn sys_recvmsg(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_message_header: UserRef<msghdr>,
    flags: u32,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }
    recvmsg_internal(current_task, &file, user_message_header, flags, None)
}

pub fn sys_recvmmsg(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_mmsgvec: UserRef<mmsghdr>,
    vlen: u32,
    mut flags: u32,
    user_timeout: UserRef<timespec>,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    if vlen > UIO_MAXIOV {
        return error!(EINVAL);
    }

    let deadline = if user_timeout.is_null() {
        None
    } else {
        let ts = current_task.mm.read_object(user_timeout)?;
        Some(zx::Time::after(duration_from_timespec(ts)?))
    };

    let mut index = 0usize;
    while index < vlen as usize {
        let user_mmsghdr = user_mmsgvec.at(index);
        let user_msghdr = user_mmsghdr.cast::<msghdr>();
        match recvmsg_internal(current_task, &file, user_msghdr, flags, deadline) {
            Err(error) => {
                if index == 0 {
                    return Err(error);
                }
                break;
            }
            Ok(bytes_read) => {
                let msg_len = bytes_read as u32;
                let user_msg_len =
                    UserRef::<u32>::new(user_mmsghdr.addr() + std::mem::size_of::<msghdr>());
                current_task.mm.write_object(user_msg_len, &msg_len)?;
            }
        }
        index += 1;
        if flags & MSG_WAITFORONE != 0 {
            flags |= MSG_DONTWAIT;
        }
    }
    Ok(index)
}

pub fn sys_recvfrom(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buffer: UserAddress,
    buffer_length: usize,
    flags: u32,
    user_src_address: UserAddress,
    user_src_address_length: UserRef<socklen_t>,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    let flags = SocketMessageFlags::from_bits_truncate(flags);
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let info = socket_ops.recvmsg(
        current_task,
        &file,
        &[UserBuffer { address: user_buffer, length: buffer_length }],
        flags,
        None,
    )?;

    if !user_src_address.is_null() {
        if let Some(address) = info.address {
            write_socket_address(
                current_task,
                user_src_address,
                user_src_address_length,
                &address.to_bytes(),
            )?;
        } else {
            current_task.mm.write_object(user_src_address_length, &0)?;
        }
    }

    if flags.contains(SocketMessageFlags::TRUNC) {
        Ok(info.message_length)
    } else {
        Ok(info.bytes_read)
    }
}

fn sendmsg_internal(
    current_task: &CurrentTask,
    file: &FileHandle,
    user_message_header: UserRef<msghdr>,
    flags: u32,
) -> Result<usize, Errno> {
    let message_header = current_task.mm.read_object(user_message_header)?;

    let dest_address = maybe_parse_socket_address(
        current_task,
        message_header.msg_name,
        message_header.msg_namelen as usize,
    )?;
    let iovec =
        current_task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;

    let mut control_bytes_read = 0;
    let mut ancillary_data = Vec::new();
    let header_size = std::mem::size_of::<cmsghdr>();
    loop {
        let space = message_header.msg_controllen - control_bytes_read;
        if space < header_size {
            break;
        }
        let mut cmsg = cmsghdr::default();
        current_task
            .mm
            .read_memory(message_header.msg_control + control_bytes_read, cmsg.as_bytes_mut())?;
        // If the message header is not long enough to fit the required fields of the
        // control data, return EINVAL.
        if cmsg.cmsg_len < header_size {
            return error!(EINVAL);
        }

        let data_size = std::cmp::min(cmsg.cmsg_len - header_size, space);
        let mut data = vec![0u8; data_size];
        current_task.mm.read_memory(
            message_header.msg_control + control_bytes_read + header_size,
            &mut data,
        )?;
        control_bytes_read +=
            round_up_to_increment(header_size + data.len(), std::mem::size_of::<usize>())?;
        ancillary_data.push(AncillaryData::from_cmsg(
            current_task,
            ControlMsg::new(cmsg.cmsg_level, cmsg.cmsg_type, data),
        )?);
    }

    let flags = SocketMessageFlags::from_bits_truncate(flags);
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    socket_ops.sendmsg(current_task, file, &iovec, dest_address, ancillary_data, flags)
}

pub fn sys_sendmsg(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_message_header: UserRef<msghdr>,
    flags: u32,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }
    sendmsg_internal(current_task, &file, user_message_header, flags)
}

pub fn sys_sendmmsg(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_mmsgvec: UserRef<mmsghdr>,
    vlen: u32,
    flags: u32,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    if vlen > UIO_MAXIOV {
        return error!(EINVAL);
    }

    let mut index = 0usize;
    while index < vlen as usize {
        let user_mmsghdr = user_mmsgvec.at(index);
        let user_msghdr = user_mmsghdr.cast::<msghdr>();
        match sendmsg_internal(current_task, &file, user_msghdr, flags) {
            Err(error) => {
                if index == 0 {
                    return Err(error);
                }
                break;
            }
            Ok(bytes_read) => {
                let msg_len = bytes_read as u32;
                let user_msg_len =
                    UserRef::<u32>::new(user_mmsghdr.addr() + std::mem::size_of::<msghdr>());
                current_task.mm.write_object(user_msg_len, &msg_len)?;
            }
        }
        index += 1;
    }
    Ok(index)
}

pub fn sys_sendto(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_buffer: UserAddress,
    buffer_length: usize,
    flags: u32,
    user_dest_address: UserAddress,
    dest_address_length: socklen_t,
) -> Result<usize, Errno> {
    let file = current_task.files.get(fd)?;
    if !file.node().is_sock() {
        return error!(ENOTSOCK);
    }

    let dest_address =
        maybe_parse_socket_address(current_task, user_dest_address, dest_address_length as usize)?;
    let data = &[UserBuffer { address: user_buffer, length: buffer_length }];

    let flags = SocketMessageFlags::from_bits_truncate(flags);
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    socket_ops.sendmsg(current_task, &file, data, dest_address, vec![], flags)
}

pub fn sys_getsockopt(
    current_task: &CurrentTask,
    fd: FdNumber,
    level: u32,
    optname: u32,
    user_optval: UserAddress,
    user_optlen: UserRef<socklen_t>,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;

    let optlen = current_task.mm.read_object(user_optlen)?;
    let opt_value = socket.getsockopt(level, optname, optlen)?;

    let actual_optlen = opt_value.len() as socklen_t;
    if optlen < actual_optlen {
        return error!(EINVAL);
    }
    current_task.mm.write_memory(user_optval, &opt_value)?;
    current_task.mm.write_object(user_optlen, &actual_optlen)?;

    Ok(())
}

pub fn sys_setsockopt(
    current_task: &CurrentTask,
    fd: FdNumber,
    level: u32,
    optname: u32,
    user_optval: UserAddress,
    optlen: socklen_t,
) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;

    socket.setsockopt(
        current_task,
        level,
        optname,
        UserBuffer { address: user_optval, length: optlen as usize },
    )?;
    Ok(())
}

pub fn sys_shutdown(current_task: &CurrentTask, fd: FdNumber, how: u32) -> Result<(), Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let how = match how {
        SHUT_RD => SocketShutdownFlags::READ,
        SHUT_WR => SocketShutdownFlags::WRITE,
        SHUT_RDWR => SocketShutdownFlags::READ | SocketShutdownFlags::WRITE,
        _ => return error!(EINVAL),
    };
    socket.shutdown(how)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;

    #[::fuchsia::test]
    fn test_socketpair_invalid_arguments() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(
            sys_socketpair(
                &current_task,
                AF_INET as u32,
                SOCK_STREAM,
                0,
                UserRef::new(UserAddress::default())
            ),
            error!(EAFNOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(
                &current_task,
                AF_UNIX as u32,
                4,
                0,
                UserRef::new(UserAddress::default())
            ),
            error!(EPROTONOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(
                &current_task,
                AF_UNIX as u32,
                SOCK_STREAM,
                0,
                UserRef::new(UserAddress::default())
            ),
            error!(EFAULT)
        );
    }

    #[::fuchsia::test]
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
                    panic!("bad byte: {}", bad);
                }
            }
        }
    }
}
