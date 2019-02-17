// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for the Zircon fdio library

#![deny(warnings)]

#[allow(bad_style)]
pub mod fdio_sys;
pub use self::fdio_sys::fdio_ioctl as ioctl_raw;

use {
    fuchsia_zircon::{
        self as zx,
        prelude::*,
        sys,
    },
    std::{
        ffi::{self, CString, CStr},
        fs::File,
        os::{
            raw,
            unix::{
                ffi::OsStrExt,
                io::{
                    AsRawFd,
                    IntoRawFd,
                }
            },
        },
        path::Path,
    },
};

pub unsafe fn ioctl(dev: &File, op: raw::c_int, in_buf: *const raw::c_void, in_len: usize,
         out_buf: *mut raw::c_void, out_len: usize) -> Result<i32, zx::Status> {
   match ioctl_raw(dev.as_raw_fd(), op, in_buf, in_len, out_buf, out_len) as i32 {
     e if e < 0 => Err(zx::Status::from_raw(e)),
     e => Ok(e),
   }
}

/// Connects a channel to a named service.
pub fn service_connect(service_path: &str, channel: zx::Channel) -> Result<(), zx::Status> {
    let c_service_path = CString::new(service_path).map_err(|_| zx::Status::INVALID_ARGS)?;

    // TODO(raggi): this should be convered to an asynchronous FDIO
    // client protocol as soon as that is available (post fidl2) as this
    // call can block indefinitely.
    //
    // fdio_service connect takes a *const c_char service path and a channel.
    // On success, the channel is connected, and on failure, it is closed.
    // In either case, we do not need to clean up the channel so we use
    // `into_raw` so that Rust forgets about it.
    zx::ok(unsafe {
        fdio_sys::fdio_service_connect(
            c_service_path.as_ptr(),
            channel.into_raw())
    })
}

/// Connects a channel to a named service relative to a directory `dir`.
/// `dir` must be a directory protocol channel.
pub fn service_connect_at(dir: &zx::Channel, service_path: &str, channel: zx::Channel)
    -> Result<(), zx::Status>
{
    let c_service_path = CString::new(service_path).map_err(|_| zx::Status::INVALID_ARGS)?;

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
    zx::ok(unsafe {
        fdio_sys::fdio_service_connect_at(
            dir.raw_handle(),
            c_service_path.as_ptr(),
            channel.into_raw())
    })
}

pub fn transfer_fd(file: std::fs::File) -> Result<zx::Handle, zx::Status> {
    unsafe {
        let mut fd_handle = zx::sys::ZX_HANDLE_INVALID;
        let status = fdio_sys::fdio_fd_transfer(file.into_raw_fd(), &mut fd_handle as *mut zx::sys::zx_handle_t);
        if status != zx::sys::ZX_OK {
            return Err(zx::Status::from_raw(status));
        }
        Ok(zx::Handle::from_raw(fd_handle))
    }
}

/// Open a new connection to `file` by sending a request to open
/// a new connection to the sever.
pub fn clone_channel(file: &std::fs::File) -> Result<zx::Channel, zx::Status> {
    unsafe {
        // First, we must open a new connection to the handle, since
        // we must return a newly owned copy.
        let fdio = fdio_sys::fdio_unsafe_fd_to_io(file.as_raw_fd());
        let unowned_handle = fdio_sys::fdio_unsafe_borrow_channel(fdio);
        let handle = fdio_sys::fdio_service_clone(unowned_handle);
        fdio_sys::fdio_unsafe_release(fdio);

        match handle {
            zx::sys::ZX_HANDLE_INVALID => Err(zx::Status::NOT_SUPPORTED),
            _ => Ok(zx::Channel::from(zx::Handle::from_raw(handle))),
        }
    }
}

/// Retrieves the topological path for a device node.
pub fn device_get_topo_path(dev: &File) -> Result<String, zx::Status> {
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
    String::from_utf8(topo).map_err(|_| zx::Status::IO)
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
    F: Sized + FnMut(WatchEvent, &Path) -> Result<(), zx::Status>,
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
/// If the callback returns an error, the watching stops, and the zx::Status is returned.
///
/// This function blocks for the duration of the watch operation. The deadline parameter will stop
/// the watch at the given (absolute) time and return zx::Status::ErrTimedOut. A deadline of
/// zx::ZX_TIME_INFINITE will never expire.
///
/// The callback may use zx::ErrStop as a way to signal to the caller that it wants to
/// stop because it found what it was looking for. Since this error code is not returned by
/// syscalls or public APIs, the callback does not need to worry about it turning up normally.
pub fn watch_directory<F>(dir: &File, deadline: sys::zx_time_t, mut f: F) -> zx::Status
where
    F: Sized + FnMut(WatchEvent, &Path) -> Result<(), zx::Status>,
{
    let cb_ptr: *mut raw::c_void = &mut f as *mut _ as *mut raw::c_void;
    unsafe {
        zx::Status::from_raw(fdio_sys::fdio_watch_directory(
            dir.as_raw_fd(),
            Some(watcher_cb::<F>),
            deadline,
            cb_ptr,
        ))
    }
}

/// Calculates an IOCTL value from kind, family and number.
pub const fn make_ioctl(kind: raw::c_int, family: raw::c_int, number: raw::c_int) -> raw::c_int {
    ((kind & 0xF) << 20) | ((family & 0xFF) << 8) | (number & 0xFF)
}

pub fn get_vmo_copy_from_file(file: &File) -> Result<zx::Vmo, zx::Status> {
    unsafe {
        let mut vmo_handle: zx::sys::zx_handle_t = zx::sys::ZX_HANDLE_INVALID;
        match fdio_sys::fdio_get_vmo_copy(file.as_raw_fd(), &mut vmo_handle) {
            0 => Ok(zx::Vmo::from(zx::Handle::from_raw(vmo_handle))),
            error_code => Err(zx::Status::from_raw(error_code))
        }
    }
}

pub const IOCTL_DEVICE_GET_TOPO_PATH: raw::c_int = make_ioctl(
    fdio_sys::IOCTL_KIND_DEFAULT,
    fdio_sys::IOCTL_FAMILY_DEVICE,
    4
);
