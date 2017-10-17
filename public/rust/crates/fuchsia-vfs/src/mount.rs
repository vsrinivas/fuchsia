// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use zircon;
use zircon_sys;
use zircon::AsHandleRef;
use zircon::HandleBased;
use fdio;

use std;
use std::fs;
use std::os::unix::io::RawFd;
use std::os::unix::io::IntoRawFd;
use std::os::unix::io::FromRawFd;
use std::path::Path;
use std::os::unix::fs::OpenOptionsExt;

// TODO(raggi): this should be able to come from libc instead.
const O_DIRECTORY: ::std::os::raw::c_int = 0o0200000;
const O_NOREMOTE: ::std::os::raw::c_int = 0o0100000000;
const O_ADMIN: ::std::os::raw::c_int = 0o0200000000;

#[link(name = "fs-management")]
extern "C" {
    fn vfs_unmount_handle(
        srv: zircon_sys::zx_handle_t,
        deadline: zircon_sys::zx_time_t,
    ) -> zircon_sys::zx_status_t;
}

pub struct Mount {
    mountfd: RawFd,
}

impl Drop for Mount {
    fn drop(&mut self) {
        let mut h: zircon_sys::zx_handle_t = zircon_sys::ZX_HANDLE_INVALID;

        let sz = unsafe {
            fdio::ioctl(
                self.mountfd,
                fdio::IOCTL_VFS_UNMOUNT_NODE,
                std::ptr::null_mut(),
                0,
                &mut h as *mut _ as *mut std::os::raw::c_void,
                std::mem::size_of::<zircon_sys::zx_handle_t>(),
            )
        };

        if sz < 0 {
            // TODO(raggi): report errors somewhere/somehow more appropriate
            eprintln!("fuchsia-vfs: failed to unmount node");
        } else {
            // TODO(raggi): what is a reasonable timeout value here?
            let status = unsafe { vfs_unmount_handle(h, zircon::deadline_after(1_000_000)) };
            if status != zircon_sys::ZX_OK {
                eprintln!("fuchsia-vfs: failed to unmount handle: {:?}", status);
            }
        }

        unsafe { zircon_sys::zx_handle_close(h) };

        std::mem::drop(unsafe { fs::File::from_raw_fd(self.mountfd) });
    }
}

pub fn mount(path: &Path, chan: zircon::Channel) -> Result<Mount, zircon::Status> {
    let dir = fs::OpenOptions::new()
        .read(true)
        .custom_flags(O_DIRECTORY | O_ADMIN | O_NOREMOTE)
        .open(&path)
        .unwrap();

    let mount = Mount {
        mountfd: dir.into_raw_fd(),
    };

    let h = chan.into_handle().into_raw();

    let sz = unsafe {
        fdio::ioctl(
            mount.mountfd,
            fdio::IOCTL_VFS_MOUNT_FS,
            &h as *const _ as *const std::os::raw::c_void,
            std::mem::size_of::<zircon_sys::zx_handle_t>(),
            std::ptr::null_mut(),
            0,
        )
    };

    if sz != 0 {
        unsafe { zircon_sys::zx_handle_close(h) };
        return Err(zircon::Status::from_raw(sz as i32));
    }

    Ok(mount)
}

#[cfg(test)]
mod test {
    use super::*;

    extern crate tempdir;

    #[test]
    fn test_mount_unmount() {
        let (c1, c2) = zircon::Channel::create(zircon::ChannelOpts::default()).unwrap();

        // TODO(raggi): where is the appropriate place to put this, it's part of the mount protocol?
        c2.signal_handle(zircon_sys::ZX_SIGNAL_NONE, zircon_sys::ZX_USER_SIGNAL_0)
            .unwrap();

        let port = zircon::Port::create(zircon::PortOpts::default()).unwrap();

        c2.wait_async_handle(
            &port,
            1,
            zircon_sys::ZX_CHANNEL_PEER_CLOSED,
            zircon::WaitAsyncOpts::Once,
        ).unwrap();

        let td = tempdir::TempDir::new("test_mount_unmount").unwrap();

        let m = mount(&td.path(), c1).unwrap();

        assert_eq!(
            zircon::Status::ErrTimedOut,
            port.wait(zircon::deadline_after(2_000_000))
                .expect_err("timeout")
        );

        std::mem::drop(m);

        let packet = port.wait(zircon::deadline_after(2_000_000)).unwrap();
        match packet.contents() {
            zircon::PacketContents::SignalOne(sp) => {
                assert_eq!(
                    zircon_sys::ZX_CHANNEL_PEER_CLOSED,
                    sp.observed() & zircon_sys::ZX_CHANNEL_PEER_CLOSED
                );
            }
            _ => assert!(false, "expected signalone packet"),
        }
    }
}
