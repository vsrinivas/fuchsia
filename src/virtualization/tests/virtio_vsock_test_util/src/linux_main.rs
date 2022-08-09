// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    argh::FromArgs,
    libc,
    std::{
        io::{Error, ErrorKind},
        mem, thread,
    },
};

#[derive(FromArgs, PartialEq, Debug)]
/// Top-level command.
pub struct Commands {
    #[argh(subcommand)]
    nested: SubCommands,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommands {
    IntegrationTest(IntegrationTestArgs),
    MicroBenchmark(MicroBenchmarkArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Run the test util in integration test mode.
#[argh(subcommand, name = "integration_test")]
struct IntegrationTestArgs {}

#[derive(FromArgs, PartialEq, Debug)]
/// Run the test util in micro benchmark mode (invoked by the guest tool).
#[argh(subcommand, name = "micro_benchmark")]
struct MicroBenchmarkArgs {}

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

fn run_integration_test() -> std::io::Result<()> {
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

fn run_latency_check(fd: &VsockFd) -> std::io::Result<()> {
    // Read a page sized packet from the socket, and write it back as quickly as possible until
    // the client closes the socket.
    let mut buffer = [0u8; 4096];
    loop {
        let actual = fd.read(buffer.as_mut_ptr(), buffer.len())?;
        if actual == 0 {
            // EOF.
            break;
        } else if actual != buffer.len() as isize {
            return Err(Error::new(
                ErrorKind::InvalidData,
                format!("expected to read {} bytes, but read {}", buffer.len(), actual),
            ));
        }

        let result = fd.write(buffer.as_ptr(), buffer.len());
        if let Err(e) = result {
            if e.kind() == std::io::ErrorKind::BrokenPipe {
                break;
            } else {
                return Err(Error::new(e.kind(), "failed to write to socket"));
            }
        }
    }

    Ok(())
}

// Read and write 128 MiB in 4 KiB chunks concurrently on a single connection.
fn run_single_stream_bidirectional_round_trip(
    data_fd: &VsockFd,
    control_fd: &VsockFd,
    count: u32,
    magic: u8,
) -> std::io::Result<()> {
    // Send the magic byte when ready to start.
    let magic_buf = [magic];
    control_fd
        .write(magic_buf.as_ptr(), magic_buf.len())
        .map_err(|err| Error::new(err.kind(), "failed to write to control socket"))?;

    let read_thread = {
        let read_fd_clone = data_fd.clone();
        thread::spawn(move || {
            let mut buffer = [0u8; 4096]; // 4 KiB
            let total_size = (1 << 20) * 128; // 128 MiB

            for _ in 0..count {
                let mut read_bytes_remaining = total_size;
                while read_bytes_remaining > 0 {
                    let read_segment = std::cmp::min(read_bytes_remaining, buffer.len());
                    let actual = read_fd_clone.read(buffer.as_mut_ptr(), read_segment)?;
                    if actual <= 0 {
                        return Err(Error::new(ErrorKind::BrokenPipe, "Unexpected EOF"));
                    }

                    read_bytes_remaining -= actual as usize;
                }
            }

            Ok(())
        })
    };

    let write_thread = {
        let write_fd_clone = data_fd.clone();
        thread::spawn(move || {
            let buffer = [0u8; 4096]; // 4 KiB
            let total_size = (1 << 20) * 128; // 128 MiB
            let segments = total_size / buffer.len();

            for _ in 0..count {
                for _ in 0..segments {
                    let actual = write_fd_clone
                        .write(buffer.as_ptr(), buffer.len())
                        .map_err(|err| Error::new(err.kind(), "failed to write to socket"))?;
                    if actual != buffer.len() as isize {
                        return Err(Error::new(
                            ErrorKind::InvalidData,
                            format!(
                                "expected to write {} bytes, but wrote {}",
                                buffer.len(),
                                actual
                            ),
                        ));
                    }
                }
            }

            Ok(())
        })
    };

    read_thread.join().expect("failed to join read thread")?;
    write_thread.join().expect("failed to join write thread")?;

    Ok(())
}

// Read 128 MiB in 4 KiB chunks, and then write the same. As the client can measure both TX
// and RX from its end, this is a reliable throughput measurement (although it can't differentiate
// RX and TX speeds).
fn run_single_stream_unidirectional_round_trip(
    data_fd: &VsockFd,
    control_fd: &VsockFd,
    count: u32,
    magic: u8,
) -> std::io::Result<()> {
    let mut buffer = [0u8; 4096]; // 4 KiB
    let total_size = (1 << 20) * 128; // 128 MiB
    let segments = total_size / buffer.len();

    // Send the magic byte when ready to start.
    let magic_buf = [magic];
    control_fd
        .write(magic_buf.as_ptr(), magic_buf.len())
        .map_err(|err| Error::new(err.kind(), "failed to write to control socket"))?;

    for _ in 0..count {
        let mut read_bytes_remaining = total_size;
        while read_bytes_remaining > 0 {
            let read_segment = std::cmp::min(read_bytes_remaining, buffer.len());
            let actual = data_fd.read(buffer.as_mut_ptr(), read_segment)?;
            if actual <= 0 {
                return Err(Error::new(ErrorKind::BrokenPipe, "Unexpected EOF"));
            }

            read_bytes_remaining -= actual as usize;
        }

        for _ in 0..segments {
            let actual = data_fd
                .write(buffer.as_ptr(), buffer.len())
                .map_err(|err| Error::new(err.kind(), "failed to write to socket"))?;
            if actual != buffer.len() as isize {
                return Err(Error::new(
                    ErrorKind::InvalidData,
                    format!("expected to write {} bytes, but wrote {}", buffer.len(), actual),
                ));
            }
        }
    }

    Ok(())
}

// Read 128 MiB in 4 KiB chunks on 5 connections, and then write the same. As the client can measure
// both TX and RX from its end, this is a reliable throughput measurement (although it can't
// differentiate RX and TX speeds).
fn run_multi_stream_unidirectional_round_trip(
    data_fd1: &VsockFd,
    data_fd2: &VsockFd,
    data_fd3: &VsockFd,
    data_fd4: &VsockFd,
    data_fd5: &VsockFd,
    control_fd: &VsockFd,
    count: u32,
) -> std::io::Result<()> {
    let get_thread = |data_fd: &VsockFd, magic: u8| -> thread::JoinHandle<std::io::Result<()>> {
        let data_fd_clone = data_fd.clone();
        let control_fd_clone = control_fd.clone();
        thread::spawn(move || {
            run_single_stream_unidirectional_round_trip(
                &data_fd_clone,
                &control_fd_clone,
                count,
                magic,
            )
        })
    };

    let thread1 = get_thread(data_fd1, 124);
    let thread2 = get_thread(data_fd2, 125);
    let thread3 = get_thread(data_fd3, 126);
    let thread4 = get_thread(data_fd4, 127);
    let thread5 = get_thread(data_fd5, 128);

    thread1.join().expect("failed to join stream1")?;
    thread2.join().expect("failed to join stream1")?;
    thread3.join().expect("failed to join stream1")?;
    thread4.join().expect("failed to join stream1")?;
    thread5.join().expect("failed to join stream1")?;

    Ok(())
}

struct MicroBenchmarkSockets {
    control_fd: VsockFd,
    latency_fd: VsockFd,
    single_stream_fd: VsockFd,
    multi_stream_fd1: VsockFd,
    multi_stream_fd2: VsockFd,
    multi_stream_fd3: VsockFd,
    multi_stream_fd4: VsockFd,
    multi_stream_fd5: VsockFd,
    bidirectional_single_stream_fd: VsockFd,
}

impl MicroBenchmarkSockets {
    pub fn bind() -> std::io::Result<Self> {
        let control_fd = VsockFd::open()?;
        let latency_fd = VsockFd::open()?;
        let single_stream_fd = VsockFd::open()?;
        let multi_stream_fd1 = VsockFd::open()?;
        let multi_stream_fd2 = VsockFd::open()?;
        let multi_stream_fd3 = VsockFd::open()?;
        let multi_stream_fd4 = VsockFd::open()?;
        let multi_stream_fd5 = VsockFd::open()?;
        let bidirectional_single_stream_fd = VsockFd::open()?;

        control_fd.bind(8501)?;
        control_fd.connect(8500)?;

        latency_fd.bind(8502)?;
        latency_fd.connect(8500)?;

        single_stream_fd.bind(8503)?;
        single_stream_fd.connect(8500)?;

        multi_stream_fd1.bind(8504)?;
        multi_stream_fd1.connect(8500)?;

        multi_stream_fd2.bind(8505)?;
        multi_stream_fd2.connect(8500)?;

        multi_stream_fd3.bind(8506)?;
        multi_stream_fd3.connect(8500)?;

        multi_stream_fd4.bind(8507)?;
        multi_stream_fd4.connect(8500)?;

        multi_stream_fd5.bind(8508)?;
        multi_stream_fd5.connect(8500)?;

        bidirectional_single_stream_fd.bind(8509)?;
        bidirectional_single_stream_fd.connect(8500)?;

        Ok(MicroBenchmarkSockets {
            control_fd,
            latency_fd,
            single_stream_fd,
            multi_stream_fd1,
            multi_stream_fd2,
            multi_stream_fd3,
            multi_stream_fd4,
            multi_stream_fd5,
            bidirectional_single_stream_fd,
        })
    }

    pub fn unbind(self) -> std::io::Result<()> {
        self.control_fd.close()?;
        self.latency_fd.close()?;
        self.single_stream_fd.close()?;
        self.multi_stream_fd1.close()?;
        self.multi_stream_fd2.close()?;
        self.multi_stream_fd3.close()?;
        self.multi_stream_fd4.close()?;
        self.multi_stream_fd5.close()?;
        self.bidirectional_single_stream_fd.close()?;

        Ok(())
    }
}

fn run_micro_benchmark() -> std::io::Result<()> {
    let sockets = MicroBenchmarkSockets::bind()?;

    run_latency_check(&sockets.latency_fd)?;
    run_single_stream_bidirectional_round_trip(
        &sockets.bidirectional_single_stream_fd,
        &sockets.control_fd,
        100,
        129,
    )?;
    run_single_stream_unidirectional_round_trip(
        &sockets.single_stream_fd,
        &sockets.control_fd,
        100,
        123,
    )?;
    run_multi_stream_unidirectional_round_trip(
        &sockets.multi_stream_fd1,
        &sockets.multi_stream_fd2,
        &sockets.multi_stream_fd3,
        &sockets.multi_stream_fd4,
        &sockets.multi_stream_fd5,
        &sockets.control_fd,
        50,
    )?;

    sockets.unbind()?;

    Ok(())
}

fn main() -> std::io::Result<()> {
    match argh::from_env::<Commands>().nested {
        SubCommands::IntegrationTest(_) => run_integration_test(),
        SubCommands::MicroBenchmark(_) => run_micro_benchmark(),
    }
}
