// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia VFS Server Bindings

extern crate fuchsia_zircon as zircon;
extern crate fuchsia_zircon_sys as zircon_sys;

use zircon::AsHandleRef;

use std::fs;
use std::os::unix::io::RawFd;
use std::os::unix::io::IntoRawFd;
use std::os::unix::io::FromRawFd;
use std::path::PathBuf;
use std::os::unix::fs::OpenOptionsExt;

const IOCTL_FAMILY_VFS: ::std::os::raw::c_int = 2;
const IOCTL_KIND_GET_HANDLE: ::std::os::raw::c_int = 1;
const IOCTL_KIND_SET_HANDLE: ::std::os::raw::c_int = 3;
const IOCTL_VFS_MOUNT_FS: ::std::os::raw::c_int =
    ((IOCTL_KIND_SET_HANDLE & 0xF) << 20) | (((IOCTL_FAMILY_VFS & 0xFF) << 8) | (0 & 0xFF));

const IOCTL_VFS_UNMOUNT_NODE: ::std::os::raw::c_int =
    ((IOCTL_KIND_GET_HANDLE & 0xF) << 20) | (((IOCTL_FAMILY_VFS & 0xFF) << 8) | (2 & 0xFF));

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

extern "C" {
    fn fdio_ioctl(
        fd: RawFd,
        op: i32,
        in_buf: *const u8,
        in_len: usize,
        out_buf: *mut u8,
        out_len: usize,
    ) -> isize;
}

pub struct Mount {
    mountfd: RawFd,
}

impl Drop for Mount {
    fn drop(&mut self) {
        let mut h: zircon_sys::zx_handle_t = unsafe { std::mem::uninitialized() };

        let sz = unsafe {
            fdio_ioctl(
                self.mountfd,
                IOCTL_VFS_UNMOUNT_NODE,
                std::ptr::null_mut(),
                0,
                &mut h as *mut _ as *mut u8,
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

pub fn mount(path: PathBuf, chan: zircon::Channel) -> Result<(), zircon::Status> {
    let dir = fs::OpenOptions::new()
        .read(true)
        .custom_flags(O_DIRECTORY | O_ADMIN | O_NOREMOTE)
        .open(&path)
        .unwrap();

    let mount = Mount {
        mountfd: dir.into_raw_fd(),
    };

    let h = chan.raw_handle();

    let sz = unsafe {
        fdio_ioctl(
            mount.mountfd,
            IOCTL_VFS_MOUNT_FS,
            &h as *const _ as *const u8,
            std::mem::size_of::<zircon_sys::zx_handle_t>(),
            std::ptr::null_mut(),
            0,
        )
    };

    if sz != 0 {
        return Err(zircon::Status::from_raw(sz as i32));
    }

    // prevent Handle.drop
    std::mem::forget(chan);

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    // TODO(raggi): switch out for tempdir crate once rand crate links zircon
    struct Tempdir {
        path: String
    }

    impl Tempdir {
        fn create(name: &str) -> Tempdir {
            let mut b = [0; 8];
            zircon::cprng_draw(&mut b).unwrap();
            let rs : String = b.iter().map(|&c| format!("{:X}", c)).collect();
            Tempdir{
                path: format!("/tmp/{}-{}", name, rs)
            }
        }
    }

    impl Drop for Tempdir {
        fn drop(&mut self) {
            fs::remove_dir_all(&self.path);
        }
    }

    #[test]
    fn test_mount_unmount() {
        let (c1, c2) = zircon::Channel::create(zircon::ChannelOpts::default()).unwrap();

        // TODO(raggi): where is the appropriate place to put this, it's part of the mount protocol.
        c2.signal_handle(zircon_sys::ZX_SIGNAL_NONE, zircon_sys::ZX_USER_SIGNAL_0).unwrap();

        let port = zircon::Port::create(zircon::PortOpts::default()).unwrap();

        c2.wait_async_handle(&port, 1,
                             zircon_sys::ZX_CHANNEL_PEER_CLOSED,
                             zircon::WaitAsyncOpts::Once).unwrap();

        let td = Tempdir::create("test_mount_unmount");

        fs::create_dir(&td.path).expect("mkdir");

        // this drops the serving side, which should result in c2 getting closed.
        mount(PathBuf::from(&td.path), c1).unwrap();

        let packet = port.wait(zircon::deadline_after(2_000_000)).unwrap();
        match packet.contents() {
            zircon::PacketContents::SignalOne(sp) => {
                assert_eq!(zircon_sys::ZX_CHANNEL_PEER_CLOSED,
                           sp.observed() & zircon_sys::ZX_CHANNEL_PEER_CLOSED);
            }
            _ => { assert!(false, "expected signalone packet") }
        }
    }
}
