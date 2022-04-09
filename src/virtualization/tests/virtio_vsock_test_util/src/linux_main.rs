// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use libc;
use std::io::{Error, ErrorKind};
use std::mem;
use std::thread;

const VMADDR_CID_HOST: libc::c_uint = 2;
const VMADDR_CID_ANY: libc::c_uint = std::u32::MAX;
const TEST_DATA_LEN: usize = 60000;

macro_rules! get_return_value {
    ($r:expr) => {
        if $r == -1 {
            Err(Error::last_os_error())
        } else {
            Ok($r)
        }
    };
}

// VSOCK Address structure. Defined in Linux:include/uapi/linux/vm_sockets.h
#[repr(C, packed)]
struct sockaddr_vm {
    svm_family: libc::sa_family_t,
    svm_reserved1: libc::c_ushort,
    svm_port: libc::c_uint,
    svm_cid: libc::c_uint,
    svm_zero: [u8; 4],
}

#[derive(Copy, Clone)]
struct VsockFd(i32);

impl VsockFd {
    // Open a socket and return the file descriptor.
    pub fn open() -> std::io::Result<VsockFd> {
        let fd = unsafe {
            libc::socket(libc::AF_VSOCK, libc::SOCK_STREAM, 0 /* protocol */)
        };
        Ok(VsockFd(get_return_value!(fd)?))
    }

    // Safe wrapper around POSIX close.
    pub fn close(self) -> std::io::Result<()> {
        let r = unsafe { libc::close(self.0) };
        get_return_value!(r)?;
        Ok(())
    }

    // Safe wrapper around POSIX connect, tailored for VSOCK from guests.
    pub fn connect(&self, port: u32) -> std::io::Result<()> {
        let addr = sockaddr_vm {
            svm_family: libc::AF_VSOCK as u16,
            svm_reserved1: 0,
            svm_port: port,
            svm_cid: VMADDR_CID_HOST,
            svm_zero: [0; 4],
        };
        let r = unsafe {
            let addr_ptr = &addr as *const sockaddr_vm as *const libc::sockaddr;
            libc::connect(self.0, addr_ptr, mem::size_of::<sockaddr_vm>() as libc::socklen_t)
        };
        get_return_value!(r)?;
        Ok(())
    }

    // Safe wrapper around POSIX bind, tailored for VSOCK from guests.
    pub fn bind(&self, port: u32) -> std::io::Result<()> {
        let addr = sockaddr_vm {
            svm_family: libc::AF_VSOCK as u16,
            svm_reserved1: 0,
            svm_port: port,
            svm_cid: VMADDR_CID_ANY,
            svm_zero: [0; 4],
        };
        let r = unsafe {
            let addr_ptr = &addr as *const sockaddr_vm as *const libc::sockaddr;
            libc::bind(self.0, addr_ptr, mem::size_of::<sockaddr_vm>() as libc::socklen_t)
        };
        get_return_value!(r)?;
        Ok(())
    }

    // Safe wrapper around POSIX listen.
    pub fn listen(&self) -> std::io::Result<()> {
        let r = unsafe {
            libc::listen(self.0, 1 /* backlog */)
        };
        get_return_value!(r)?;
        Ok(())
    }

    // Safe wrapper around POSIX accept.
    pub fn accept(&self) -> std::io::Result<VsockFd> {
        let r = unsafe { libc::accept(self.0, std::ptr::null_mut(), std::ptr::null_mut()) };
        Ok(VsockFd(get_return_value!(r)?))
    }

    // Safe wrapper around POSIX write.
    pub fn write(&self, data: *const u8, len: usize) -> std::io::Result<isize> {
        let r = unsafe { libc::write(self.0, data as *const libc::c_void, len) };
        get_return_value!(r)
    }

    // Safe wrapper around POSIX read.
    pub fn read(&self, data: *mut u8, len: usize) -> std::io::Result<isize> {
        let r = unsafe { libc::read(self.0, data as *mut libc::c_void, len) };
        get_return_value!(r)
    }
}

fn test_read_write(fd: &VsockFd) -> std::io::Result<()> {
    let data = Box::new([42u8; TEST_DATA_LEN]);
    for _ in 0..4 {
        fd.write(data.as_ptr(), TEST_DATA_LEN)?;
    }

    let mut val: [u8; 1] = [0];
    let actual = fd.read(val.as_mut_ptr(), 1)?;
    if actual != 1 {
        return Err(Error::new(
            ErrorKind::InvalidData,
            format!("Expected to read 1 byte not {}", actual),
        ));
    }
    if val[0] != 42 {
        return Err(Error::new(
            ErrorKind::InvalidData,
            format!("Expected to read '42' not '{}'", actual),
        ));
    }
    Ok(())
}

// Spawn a thread that waits for the host to connect to the socket. Returns the local fd and a
// JoinHandle for the remote fd.
fn spawn_server(
    port: u32,
) -> std::io::Result<(VsockFd, thread::JoinHandle<std::io::Result<VsockFd>>)> {
    let server_fd = VsockFd::open()?;
    server_fd.bind(port)?;
    server_fd.listen()?;
    let fd = server_fd.clone();
    let thread = thread::spawn(move || fd.accept());
    Ok((fd, thread))
}

fn main() -> std::io::Result<()> {
    let client_fd = VsockFd::open()?;

    // Register listeners early to avoid race conditions.
    let (server_fd1, server_thread1) = spawn_server(8001)?;
    let (server_fd2, server_thread2) = spawn_server(8002)?;
    let (server_fd3, server_thread3) = spawn_server(8003)?;

    // Test a connection initiated by the guest.
    client_fd.bind(49152)?;
    client_fd.connect(8000)?;
    test_read_write(&client_fd)?;
    client_fd.close()?;

    // Test a connection initiated by the host.
    let remote_fd = server_thread1.join().expect("Failed to join server thread 1")?;
    test_read_write(&remote_fd)?;
    server_fd1.close()?;

    // With this connection test closing from the host. Continuously writes until the fd is closed.
    let remote_fd = server_thread2.join().expect("Failed to join server thread 2")?;
    let data = Box::new([42u8; TEST_DATA_LEN as usize]);
    loop {
        let result = remote_fd.write(data.as_ptr(), TEST_DATA_LEN);
        if let Err(e) = result {
            if e.kind() == std::io::ErrorKind::BrokenPipe {
                break;
            }
        }
    }
    server_fd2.close()?;

    // With this connection test closing from the guest. Reads one byte then closes the fd.
    let remote_fd = server_thread3.join().expect("Failed to join server thread 3")?;
    let mut val: [u8; 1] = [0];
    remote_fd.read(val.as_mut_ptr(), 1)?;
    server_fd3.close()?;

    println!("PASS");
    Ok(())
}
