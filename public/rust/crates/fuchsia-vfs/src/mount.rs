// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zircon;
use zircon::{DurationNum, HandleBased};
use fdio;

use std;
use std::fs;
use std::os::unix::io::RawFd;
use std::os::unix::io::IntoRawFd;
use std::os::unix::io::FromRawFd;
use std::path::Path;
use std::os::unix::fs::OpenOptionsExt;
use fdio::fdio_sys::{O_ADMIN, O_NOREMOTE, O_DIRECTORY};

#[link(name = "fs-management")]
extern "C" {
    fn vfs_unmount_handle(
        srv: zircon::sys::zx_handle_t,
        deadline: zircon::sys::zx_time_t,
    ) -> zircon::sys::zx_status_t;
}

pub struct Mount {
    mountfd: RawFd,
}

impl Drop for Mount {
    fn drop(&mut self) {
        let mut h: zircon::sys::zx_handle_t = zircon::sys::ZX_HANDLE_INVALID;

        let sz = unsafe {
            fdio::ioctl(
                self.mountfd,
                fdio::IOCTL_VFS_UNMOUNT_NODE,
                std::ptr::null_mut(),
                0,
                &mut h as *mut _ as *mut std::os::raw::c_void,
                std::mem::size_of::<zircon::sys::zx_handle_t>(),
            )
        };

        if sz < 0 {
            // TODO(raggi): report errors somewhere/somehow more appropriate
            eprintln!("fuchsia-vfs: failed to unmount node");
        } else {
            // TODO(raggi): what is a reasonable timeout value here?
            let deadline = 1_000_000.nanos().after_now();
            let status = unsafe { vfs_unmount_handle(h, deadline.nanos()) };
            if status != zircon::sys::ZX_OK {
                eprintln!("fuchsia-vfs: failed to unmount handle: {:?}", status);
            }
        }

        unsafe { zircon::sys::zx_handle_close(h) };

        std::mem::drop(unsafe { fs::File::from_raw_fd(self.mountfd) });
    }
}

pub fn mount(path: &Path, chan: zircon::Channel) -> Result<Mount, zircon::Status> {
    let dir = fs::OpenOptions::new()
        .read(true)
        .custom_flags(O_DIRECTORY | O_ADMIN | O_NOREMOTE)
        .open(&path)
        .unwrap();

    let mount = Mount { mountfd: dir.into_raw_fd() };

    let h = chan.into_handle().into_raw();

    let sz = unsafe {
        fdio::ioctl(
            mount.mountfd,
            fdio::IOCTL_VFS_MOUNT_FS,
            &h as *const _ as *const std::os::raw::c_void,
            std::mem::size_of::<zircon::sys::zx_handle_t>(),
            std::ptr::null_mut(),
            0,
        )
    };

    if sz != 0 {
        unsafe { zircon::sys::zx_handle_close(h) };
        return Err(zircon::Status::from_raw(sz as i32));
    }

    Ok(mount)
}

#[cfg(test)]
mod test {
    use super::*;
    use zircon::AsHandleRef;

    extern crate tempdir;

    #[test]
    fn test_mount_unmount() {
        let (c1, c2) = zircon::Channel::create().unwrap();

        // TODO(raggi): where is the appropriate place to put this, it's part of the mount protocol?
        c2.signal_handle(zircon::Signals::NONE, zircon::Signals::USER_0)
            .unwrap();

        let port = zircon::Port::create().unwrap();

        c2.wait_async_handle(
            &port,
            1,
            zircon::Signals::CHANNEL_PEER_CLOSED,
            zircon::WaitAsyncOpts::Once,
        ).unwrap();

        let td = tempdir::TempDir::new("test_mount_unmount").unwrap();

        let m = mount(&td.path(), c1).unwrap();

        assert_eq!(
            zircon::Status::TIMED_OUT,
            port.wait(2_000_000.nanos().after_now()).expect_err(
                "timeout",
            )
        );

        std::mem::drop(m);

        let packet = port.wait(2_000_000.nanos().after_now()).unwrap();
        match packet.contents() {
            zircon::PacketContents::SignalOne(sp) => {
                assert_eq!(
                    zircon::Signals::CHANNEL_PEER_CLOSED,
                    sp.observed() & zircon::Signals::CHANNEL_PEER_CLOSED
                );
            }
            _ => assert!(false, "expected signalone packet"),
        }
    }
}
