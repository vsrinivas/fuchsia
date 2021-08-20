// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

use std::sync::Arc;

use crate::errno;
use crate::fd_impl_nonseekable;
use crate::from_status_like_fdio;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub struct FuchsiaPipe {
    socket: zx::Socket,
}

impl FuchsiaPipe {
    pub fn from_socket(kern: &Kernel, socket: zx::Socket) -> Result<FileHandle, zx::Status> {
        // TODO: Distinguish between stream and datagram sockets.
        Ok(Anon::new_file(anon_fs(kern), Box::new(FuchsiaPipe { socket }), OpenFlags::RDWR))
    }
}

// See zxio::wait_begin_inner in FDIO.
fn get_signals_from_events(events: FdEvents) -> zx::Signals {
    let mut signals = zx::Signals::NONE;
    if events & FdEvents::POLLIN {
        signals |= zx::Signals::SOCKET_READABLE
            | zx::Signals::SOCKET_PEER_CLOSED
            | zx::Signals::SOCKET_PEER_WRITE_DISABLED;
    }
    if events & FdEvents::POLLOUT {
        signals |= zx::Signals::SOCKET_WRITABLE | zx::Signals::SOCKET_PEER_CLOSED;
    }
    if events & FdEvents::POLLRDHUP {
        signals |= zx::Signals::SOCKET_PEER_WRITE_DISABLED | zx::Signals::SOCKET_PEER_CLOSED;
    }
    return signals;
}

impl FileOps for FuchsiaPipe {
    fd_impl_nonseekable!();

    fn write(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut size = 0;
        task.mm.read_each(data, |bytes| {
            let actual =
                self.socket.write(&bytes).map_err(|status| from_status_like_fdio!(status))?;
            size += actual;
            if actual != bytes.len() {
                return Ok(None);
            }
            Ok(Some(()))
        })?;
        Ok(size)
    }

    fn read(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut size = 0;
        task.mm.write_each(data, |bytes| {
            let actual =
                self.socket.read(bytes).map_err(|status| from_status_like_fdio!(status))?;
            size += actual;
            Ok(&bytes[0..actual])
        })?;
        Ok(size)
    }

    fn wait_async(&self, _file: &FileObject, waiter: &Arc<Waiter>, events: FdEvents) {
        waiter.wait_async(&self.socket, get_signals_from_events(events)).unwrap();
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;

    use crate::mm::PAGE_SIZE;
    use crate::syscalls::*;
    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_blocking_io() -> Result<(), anyhow::Error> {
        let (kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);
        let task = Arc::clone(ctx.task);

        let address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        let (client, server) = zx::Socket::create(zx::SocketOpts::empty())?;
        let pipe = FuchsiaPipe::from_socket(&kernel, client)?;

        let thread = std::thread::spawn(move || {
            assert_eq!(64, pipe.read(&task, &[UserBuffer { address, length: 64 }]).unwrap());
        });

        // Wait for the thread to become blocked on the read.
        zx::Duration::from_seconds(2).sleep();

        let bytes = [0u8; 64];
        assert_eq!(64, server.write(&bytes)?);

        // The thread should unblock and join us here.
        let _ = thread.join();

        Ok(())
    }
}
