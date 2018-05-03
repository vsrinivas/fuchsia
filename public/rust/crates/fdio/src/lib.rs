// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for the Zircon fdio library

#![deny(warnings)]

extern crate fuchsia_zircon as zircon;
extern crate bytes;

#[allow(non_camel_case_types)]
#[allow(non_snake_case)]
pub mod fdio_sys;

use zircon::prelude::*;
use zircon::sys as sys;

use std::ffi::{CString, CStr};
use std::fs::File;
use std::os::raw;
use std::ffi;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::io::AsRawFd;
use std::path::Path;

pub use fdio_sys::fdio_ioctl as ioctl_raw;

pub unsafe fn ioctl(dev: &File, op: raw::c_int, in_buf: *const raw::c_void, in_len: usize,
         out_buf: *mut raw::c_void, out_len: usize) -> Result<i32, zircon::Status> {
   match ioctl_raw(dev.as_raw_fd(), op, in_buf, in_len, out_buf, out_len) as i32 {
     e if e < 0 => Err(zircon::Status::from_raw(e)),
     e => Ok(e),
   }
}

/// Connects a channel to a named service.
pub fn service_connect(service_path: &str, channel: zircon::Channel) -> Result<(), zircon::Status> {
    let c_service_path = CString::new(service_path).map_err(|_| zircon::Status::INVALID_ARGS)?;

    // TODO(raggi): this should be convered to an asynchronous FDIO
    // client protocol as soon as that is available (post fidl2) as this
    // call can block indefinitely.
    //
    // fdio_service connect takes a *const c_char service path and a channel.
    // On success, the channel is connected, and on failure, it is closed.
    // In either case, we do not need to clean up the channel so we use
    // `into_raw` so that Rust forgets about it.
    zircon::ok(unsafe {
        fdio_sys::fdio_service_connect(
            c_service_path.as_ptr(),
            channel.into_raw())
    })
}

/// Connects a channel to a named service relative to a directory `dir`.
/// `dir` must be a directory protocol channel.
pub fn service_connect_at(dir: &zircon::Channel, service_path: &str, channel: zircon::Channel)
    -> Result<(), zircon::Status>
{
    let c_service_path = CString::new(service_path).map_err(|_| zircon::Status::INVALID_ARGS)?;

    // TODO(raggi): this should be convered to an asynchronous FDIO
    // client protocol as soon as that is available (post fidl2) as this
    // call can block indefinitely.
    //
    // fdio_service_connect_at takes a directory handle,
    // a *const c_char service path, and a channel to connect.
    // The directory handle is never consumed, so we borrow the raw handle.
    // On success, the channel is connected, and on failure, it is closed.
    // In either case, we do not need to clean up the channel so we use
    // `into_raw` so that Rust forgets about it.
    zircon::ok(unsafe {
        fdio_sys::fdio_service_connect_at(
            dir.raw_handle(),
            c_service_path.as_ptr(),
            channel.into_raw())
    })
}

/// Retrieves the topological path for a device node.
pub fn device_get_topo_path(dev: &File) -> Result<String, zircon::Status> {
    let mut topo = vec![0; 1024];

    // This is safe because the length of the output buffer is computed from the vector, and the
    // callee does not retain any pointers.
    let size = unsafe {
        ioctl(
            dev,
            IOCTL_DEVICE_GET_TOPO_PATH,
            ::std::ptr::null(),
            0,
            topo.as_mut_ptr() as *mut raw::c_void,
            topo.len())?
    };
    topo.truncate((size - 1) as usize);
    String::from_utf8(topo).map_err(|_| zircon::Status::IO)
}

/// Events that can occur while watching a directory, including files that already exist prior to
/// running a Watcher.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum WatchEvent {
    /// A file was added.
    AddFile,

    /// A file was removed.
    RemoveFile,

    /// The Watcher has enumerated all the existing files and has started to wait for new files to
    /// be added.
    Idle,

    Unknown(i32),

    #[doc(hidden)]
    #[allow(non_camel_case_types)]
    // Try to prevent exhaustive matching since this enum may grow if fdio's events expand.
    __do_not_match,
}

impl From<raw::c_int> for WatchEvent {
    fn from(i: raw::c_int) -> WatchEvent {
        match i {
            fdio_sys::WATCH_EVENT_ADD_FILE => WatchEvent::AddFile,
            fdio_sys::WATCH_EVENT_REMOVE_FILE => WatchEvent::RemoveFile,
            fdio_sys::WATCH_EVENT_IDLE => WatchEvent::Idle,
            _ => WatchEvent::Unknown(i),
        }
    }
}

impl From<WatchEvent> for raw::c_int {
    fn from(i: WatchEvent) -> raw::c_int {
        match i {
            WatchEvent::AddFile => fdio_sys::WATCH_EVENT_ADD_FILE,
            WatchEvent::RemoveFile => fdio_sys::WATCH_EVENT_REMOVE_FILE,
            WatchEvent::Idle => fdio_sys::WATCH_EVENT_IDLE,
            WatchEvent::Unknown(i) => i as raw::c_int,
            _ => -1 as raw::c_int,
        }
    }
}

unsafe extern "C" fn watcher_cb<F>(
    _dirfd: raw::c_int,
    event: raw::c_int,
    fn_: *const raw::c_char,
    watcher: *mut raw::c_void,
) -> sys::zx_status_t
where
    F: Sized + FnMut(WatchEvent, &Path) -> Result<(), zircon::Status>,
{
    let cb: &mut F = &mut *(watcher as *mut F);
    let filename = ffi::OsStr::from_bytes(CStr::from_ptr(fn_).to_bytes());
    match cb(WatchEvent::from(event), Path::new(filename)) {
        Ok(()) => sys::ZX_OK,
        Err(e) => e.into_raw(),
    }
}

/// Runs the given callback for each file in the directory and each time a new file is
/// added to the directory.
///
/// If the callback returns an error, the watching stops, and the zircon::Status is returned.
///
/// This function blocks for the duration of the watch operation. The deadline parameter will stop
/// the watch at the given (absolute) time and return zircon::Status::ErrTimedOut. A deadline of
/// zircon::ZX_TIME_INFINITE will never expire.
///
/// The callback may use zircon::ErrStop as a way to signal to the caller that it wants to
/// stop because it found what it was looking for. Since this error code is not returned by
/// syscalls or public APIs, the callback does not need to worry about it turning up normally.
pub fn watch_directory<F>(dir: &File, deadline: sys::zx_time_t, mut f: F) -> zircon::Status
where
    F: Sized + FnMut(WatchEvent, &Path) -> Result<(), zircon::Status>,
{
    let cb_ptr: *mut raw::c_void = &mut f as *mut _ as *mut raw::c_void;
    unsafe {
        zircon::Status::from_raw(fdio_sys::fdio_watch_directory(
            dir.as_raw_fd(),
            Some(watcher_cb::<F>),
            deadline,
            cb_ptr,
        ))
    }
}

// TODO(raggi): when const fn is stable, replace the macro with const fn.
#[macro_export]
macro_rules! make_ioctl {
    ($kind:expr, $family:expr, $number:expr) => {
        (((($kind) & 0xF) << 20) | ((($family) & 0xFF) << 8) | (($number) & 0xFF))
    };
}
/// Calculates an IOCTL value from kind, family and number.
pub fn make_ioctl(kind: raw::c_int, family: raw::c_int, number: raw::c_int) -> raw::c_int {
    make_ioctl!(kind, family, number)
}

pub const IOCTL_DEVICE_GET_TOPO_PATH: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_DEVICE,
    4
);

pub const IOCTL_VFS_MOUNT_FS: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_SET_HANDLE,
    fdio_sys::IOCTL_FAMILY_VFS,
    0
);
pub const IOCTL_VFS_UNMOUNT_NODE: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_GET_HANDLE,
    fdio_sys::IOCTL_FAMILY_VFS,
    2
);
