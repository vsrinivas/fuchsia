// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use std::convert::TryInto;
use zerocopy::{AsBytes, FromBytes};

use super::*;
use crate::errno;
use crate::error;
use crate::fs::buffers::*;
use crate::fs::*;
use crate::mode;
use crate::not_implemented;
use crate::strace;
use crate::syscalls::*;
use crate::task::*;
use crate::types::*;

pub fn sys_socket(
    current_task: &CurrentTask,
    domain: u32,
    socket_type: u32,
    _protocol: u32,
) -> Result<FdNumber, Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let domain = parse_socket_domain(domain)?;
    let socket_type = parse_socket_type(domain, socket_type)?;
    let open_flags = socket_flags_to_open_flags(flags);
    let socket_file =
        Socket::new_file(current_task.kernel(), Socket::new(domain, socket_type), open_flags);

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

fn parse_socket_domain(domain: u32) -> Result<SocketDomain, Errno> {
    SocketDomain::from_raw(domain.try_into().map_err(|_| errno!(EAFNOSUPPORT))?).ok_or_else(|| {
        not_implemented!("unsupported socket domain {}", domain);
        errno!(EAFNOSUPPORT)
    })
}

fn parse_socket_type(domain: SocketDomain, socket_type: u32) -> Result<SocketType, Errno> {
    let socket_type = SocketType::from_raw(socket_type & 0xf).ok_or_else(|| {
        not_implemented!("unsupported socket type 0x{:x}", socket_type);
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
            if sun_path.len() == 0 {
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
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(&current_task, user_socket_address, address_length)?;
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
                let mode = current_task.fs.apply_umask(mode!(IFSOCK, 0o765));
                let (parent, basename) =
                    current_task.lookup_parent_at(FdNumber::AT_FDCWD, &name)?;

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
    current_task: &CurrentTask,
    fd: FdNumber,
    backlog: i32,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    socket.listen(backlog)?;
    Ok(SUCCESS)
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
        &current_task,
        || socket.accept(current_task.as_ucred()),
        FdEvents::POLLIN | FdEvents::POLLHUP,
        None,
    )?;

    if !user_socket_address.is_null() {
        let address_bytes = accepted_socket.getpeername()?;
        write_socket_address(
            &current_task,
            user_socket_address,
            user_address_length,
            &address_bytes,
        )?;
    }

    let open_flags = socket_flags_to_open_flags(flags);
    let accepted_socket_file = Socket::new_file(current_task.kernel(), accepted_socket, open_flags);
    let fd_flags = if flags & SOCK_CLOEXEC != 0 { FdFlags::CLOEXEC } else { FdFlags::empty() };
    let accepted_fd = current_task.files.add_with_flags(accepted_socket_file, fd_flags)?;
    Ok(accepted_fd)
}

pub fn sys_connect(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    address_length: usize,
) -> Result<SyscallResult, Errno> {
    let client_file = current_task.files.get(fd)?;
    let client_socket = client_file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address = parse_socket_address(&current_task, user_socket_address, address_length)?;
    let listening_socket = match address {
        SocketAddress::Unspecified => return error!(ECONNREFUSED),
        SocketAddress::Unix(name) => {
            strace!(
                &current_task,
                "connect to unix socket named {:?}",
                String::from_utf8_lossy(&name)
            );
            if name.is_empty() {
                return error!(ECONNREFUSED);
            }
            if name[0] == b'\0' {
                current_task.abstract_socket_namespace.lookup(&name)?
            } else {
                let (parent, basename) =
                    current_task.lookup_parent_at(FdNumber::AT_FDCWD, &name)?;
                let name = parent
                    .lookup_child(current_task, &mut LookupContext::default(), basename)
                    .map_err(translate_fs_error)?;
                name.entry.node.socket().ok_or_else(|| errno!(ECONNREFUSED))?.clone()
            }
        }
    };

    // TODO(tbodt): Support blocking when the UNIX domain socket queue fills up. This one's weird
    // because as far as I can tell, removing a socket from the queue does not actually trigger
    // FdEvents on anything.
    client_socket.connect(&listening_socket, current_task.as_ucred())?;
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
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address_bytes = socket.getsockname();

    write_socket_address(&current_task, user_socket_address, user_address_length, &address_bytes)?;

    Ok(SUCCESS)
}

pub fn sys_getpeername(
    current_task: &CurrentTask,
    fd: FdNumber,
    user_socket_address: UserAddress,
    user_address_length: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let address_bytes = socket.getpeername()?;

    write_socket_address(&current_task, user_socket_address, user_address_length, &address_bytes)?;

    Ok(SUCCESS)
}

pub fn sys_socketpair(
    current_task: &CurrentTask,
    domain: u32,
    socket_type: u32,
    _protocol: u32,
    user_sockets: UserRef<[FdNumber; 2]>,
) -> Result<SyscallResult, Errno> {
    let flags = socket_type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
    let domain = parse_socket_domain(domain)?;
    let socket_type = parse_socket_type(domain, socket_type)?;
    let open_flags = socket_flags_to_open_flags(flags);

    let (left, right) = Socket::new_pair(
        &current_task.thread_group.kernel,
        domain,
        socket_type,
        current_task.as_ucred(),
        open_flags,
    );

    let fd_flags = socket_flags_to_fd_flags(flags);
    // TODO: Eventually this will need to allocate two fd numbers (each of which could
    // potentially fail), and only populate the fd numbers (which can't fail) if both allocations
    // succeed.
    let left_fd = current_task.files.add_with_flags(left, fd_flags)?;
    let right_fd = current_task.files.add_with_flags(right, fd_flags)?;

    let fds = [left_fd, right_fd];
    current_task.mm.write_object(user_sockets, &fds)?;

    Ok(SUCCESS)
}

fn recvmsg_internal(
    current_task: &CurrentTask,
    file: &FileHandle,
    user_message_header: UserRef<msghdr>,
    flags: u32,
    deadline: Option<zx::Time>,
) -> Result<usize, Errno> {
    let mut message_header = msghdr::default();
    current_task.mm.read_object(user_message_header.clone(), &mut message_header)?;
    let iovec =
        current_task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;

    let flags = SocketMessageFlags::from_bits_truncate(flags);
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    let info = socket_ops.recvmsg(current_task, &file, &iovec, flags, deadline)?;

    message_header.msg_flags = 0;

    if let Some(ancillary_data) = info.ancillary_data {
        let mut num_bytes_to_write = message_header.msg_controllen as usize;
        if !ancillary_data.can_fit_all_data(num_bytes_to_write) {
            // If not all data can fit, set the MSG_CTRUNC flag.
            message_header.msg_flags |= MSG_CTRUNC as u64;
            if !ancillary_data.can_fit_any_data(num_bytes_to_write) {
                // If the length is not large enough to fit any real data, set the number of bytes
                // to write to 0.
                num_bytes_to_write = 0;
            }
        }

        let mut control_message_header = ancillary_data.into_cmsghdr(current_task)?;

        // Cap the number of bytes to write at the actual length of the control message.
        num_bytes_to_write = std::cmp::min(num_bytes_to_write, control_message_header.cmsg_len);
        // Set the cmsg_len to the actual number of bytes written.
        control_message_header.cmsg_len = num_bytes_to_write;

        current_task.mm.write_memory(
            message_header.msg_control,
            &control_message_header.as_bytes()[..num_bytes_to_write],
        )?;

        // TODO(fxb/79405): This length is not correct according to gVisor's socket_test. The
        // expected length is calculated by a CMSG_SPACE macro, which seems to do some alignment.
        message_header.msg_controllen = num_bytes_to_write;
    } else {
        // If there is no control message, make sure to clear the length.
        message_header.msg_controllen = 0;
    }

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
        let mut ts = timespec::default();
        current_task.mm.read_object(user_timeout, &mut ts)?;
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
    let into = socket_ops.recvmsg(
        current_task,
        &file,
        &[UserBuffer { address: user_buffer, length: buffer_length }],
        flags,
        None,
    )?;

    if !user_src_address.is_null() {
        if let Some(address) = into.address {
            write_socket_address(
                &current_task,
                user_src_address,
                user_src_address_length,
                &address.to_bytes(),
            )?;
        } else {
            current_task.mm.write_object(user_src_address_length, &0)?;
        }
    }

    if flags.contains(SocketMessageFlags::TRUNC) {
        Ok(into.message_length)
    } else {
        Ok(into.bytes_read)
    }
}

fn sendmsg_internal(
    current_task: &CurrentTask,
    file: &FileHandle,
    user_message_header: UserRef<msghdr>,
    flags: u32,
) -> Result<usize, Errno> {
    let mut message_header = msghdr::default();
    current_task.mm.read_object(user_message_header, &mut message_header)?;

    let dest_address = maybe_parse_socket_address(
        &current_task,
        message_header.msg_name,
        message_header.msg_namelen as usize,
    )?;
    let iovec =
        current_task.mm.read_iovec(message_header.msg_iov, message_header.msg_iovlen as i32)?;
    let ancillary_data = if message_header.msg_controllen > 0 {
        let mut control_message_header = cmsghdr::default();
        current_task
            .mm
            .read_object(UserRef::new(message_header.msg_control), &mut control_message_header)?;
        Some(AncillaryData::new(current_task, control_message_header)?)
    } else {
        None
    };

    let flags = SocketMessageFlags::from_bits_truncate(flags);
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    socket_ops.sendmsg(current_task, &file, &iovec, dest_address, ancillary_data, flags)
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
        maybe_parse_socket_address(&current_task, user_dest_address, dest_address_length as usize)?;
    let data = &[UserBuffer { address: user_buffer, length: buffer_length }];

    let flags = SocketMessageFlags::from_bits_truncate(flags);
    let socket_ops = file.downcast_file::<SocketFile>().unwrap();
    socket_ops.sendmsg(&current_task, &file, data, dest_address, None, flags)
}

pub fn sys_getsockopt(
    current_task: &CurrentTask,
    fd: FdNumber,
    level: u32,
    optname: u32,
    user_optval: UserAddress,
    user_optlen: UserRef<socklen_t>,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;
    let opt_value = match level {
        SOL_SOCKET => match optname {
            SO_TYPE => socket.socket_type.as_raw().to_ne_bytes().to_vec(),
            // TODO(tbodt): Update when internet sockets exist
            SO_DOMAIN => AF_UNIX.to_ne_bytes().to_vec(),
            SO_PEERCRED => socket
                .peer_cred()
                .unwrap_or(ucred { pid: 0, uid: uid_t::MAX, gid: gid_t::MAX })
                .as_bytes()
                .to_owned(),
            SO_PEERSEC => "unconfined".as_bytes().to_vec(),
            SO_RCVTIMEO => {
                let duration = socket.get_receive_timeout().unwrap_or(zx::Duration::default());
                timeval_from_duration(duration).as_bytes().to_owned()
            }
            SO_SNDTIMEO => {
                let duration = socket.get_send_timeout().unwrap_or(zx::Duration::default());
                timeval_from_duration(duration).as_bytes().to_owned()
            }
            SO_ACCEPTCONN => if socket.is_listening() { 1u32 } else { 0u32 }.to_ne_bytes().to_vec(),
            SO_SNDBUF => (socket.get_send_capacity() as socklen_t).to_ne_bytes().to_vec(),
            SO_RCVBUF => (socket.get_receive_capacity() as socklen_t).to_ne_bytes().to_vec(),
            SO_LINGER => socket.lock().linger.as_bytes().to_vec(),
            _ => return error!(ENOPROTOOPT),
        },
        _ => return error!(ENOPROTOOPT),
    };
    let mut optlen = 0;
    current_task.mm.read_object(user_optlen, &mut optlen)?;
    let actual_optlen = opt_value.len() as socklen_t;
    if optlen < actual_optlen {
        return error!(EINVAL);
    }
    current_task.mm.write_memory(user_optval, &opt_value)?;
    current_task.mm.write_object(user_optlen, &actual_optlen)?;
    Ok(SUCCESS)
}

pub fn sys_setsockopt(
    current_task: &CurrentTask,
    fd: FdNumber,
    level: u32,
    optname: u32,
    user_optval: UserAddress,
    optlen: socklen_t,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or_else(|| errno!(ENOTSOCK))?;

    fn read<T: Default + AsBytes + FromBytes>(
        current_task: &CurrentTask,
        user_optval: UserAddress,
        optlen: socklen_t,
    ) -> Result<T, Errno> {
        let user_ref = UserRef::<T>::new(user_optval);
        if optlen < user_ref.len() as socklen_t {
            return error!(EINVAL);
        }
        let mut value = T::default();
        current_task.mm.read_object(user_ref, &mut value)?;
        Ok(value)
    }

    let read_timeval = || {
        let duration = duration_from_timeval(read::<timeval>(current_task, user_optval, optlen)?)?;
        Ok(if duration == zx::Duration::default() { None } else { Some(duration) })
    };

    match level {
        SOL_SOCKET => match optname {
            SO_RCVTIMEO => {
                socket.set_receive_timeout(read_timeval()?);
            }
            SO_SNDTIMEO => {
                socket.set_send_timeout(read_timeval()?);
            }
            SO_SNDBUF => {
                let requested_capacity =
                    read::<socklen_t>(current_task, user_optval, optlen)? as usize;
                // See StreamUnixSocketPairTest.SetSocketSendBuf for why we multiply by 2 here.
                socket.set_send_capacity(requested_capacity * 2);
            }
            SO_RCVBUF => {
                let requested_capacity =
                    read::<socklen_t>(current_task, user_optval, optlen)? as usize;
                socket.set_receive_capacity(requested_capacity);
            }
            SO_LINGER => {
                let mut linger = read::<uapi::linger>(current_task, user_optval, optlen)?;
                if linger.l_onoff != 0 {
                    linger.l_onoff = 1;
                }
                socket.lock().linger = linger;
            }
            _ => return error!(ENOPROTOOPT),
        },
        _ => return error!(ENOPROTOOPT),
    }
    Ok(SUCCESS)
}

pub fn sys_shutdown(
    current_task: &CurrentTask,
    fd: FdNumber,
    how: u32,
) -> Result<SyscallResult, Errno> {
    let file = current_task.files.get(fd)?;
    let socket = file.node().socket().ok_or(errno!(ENOTSOCK))?;
    let how = match how {
        SHUT_RD => SocketShutdownFlags::READ,
        SHUT_WR => SocketShutdownFlags::WRITE,
        SHUT_RDWR => SocketShutdownFlags::READ | SocketShutdownFlags::WRITE,
        _ => return error!(EINVAL),
    };
    socket.shutdown(how)?;
    Ok(SUCCESS)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;

    #[test]
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
            Err(EAFNOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(
                &current_task,
                AF_UNIX as u32,
                4,
                0,
                UserRef::new(UserAddress::default())
            ),
            Err(EPROTONOSUPPORT)
        );
        assert_eq!(
            sys_socketpair(
                &current_task,
                AF_UNIX as u32,
                SOCK_STREAM,
                0,
                UserRef::new(UserAddress::default())
            ),
            Err(EFAULT)
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
