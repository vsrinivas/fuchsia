// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Stream-based Fuchsia VFS directory watcher

#![deny(warnings)]
#![deny(missing_docs)]

#[macro_use]
extern crate fdio;
#[macro_use]
extern crate fuchsia_zircon as zircon;
extern crate futures;
#[macro_use]
extern crate tokio_core;
extern crate tokio_fuchsia;

use fdio::fdio_sys;
use futures::{Async, Stream};
use std::ffi::OsStr;
use std::fs::File;
use std::io;
use std::os::raw;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
use tokio_core::reactor::Handle as TokioHandle;
use zircon::HandleBased;

/// Describes the type of event that occurred in the direcotry being watched.
#[repr(C)]
#[derive(Copy, Clone, Eq, PartialEq)]
pub struct WatchEvent(u8);

assoc_values!(WatchEvent, [
    /// A file was added.
    ADD_FILE    = VFS_WATCH_EVT_ADDED;
    /// A file was removed.
    REMOVE_FILE = VFS_WATCH_EVT_REMOVED;
    /// A file existed at the time the Watcher was created.
    EXISTING    = VFS_WATCH_EVT_EXISTING;
    /// All existing files have been enumerated.
    IDLE        = VFS_WATCH_EVT_IDLE;
]);

/// A message containing a `WatchEvent` and the filename (relative to the directory being watched)
/// that triggered the event.
#[derive(Debug)]
pub struct WatchMessage {
    /// The event that occurred.
    pub event: WatchEvent,
    /// The filename that triggered the message.
    pub filename: PathBuf,
}

/// Provides a Stream of WatchMessages corresponding to filesystem events for a given directory.
#[derive(Debug)]
#[must_use = "futures/streams must be polled"]
pub struct Watcher {
    ch: tokio_fuchsia::Channel,
    // If idx >= buf.bytes().len(), you must call reset_buf() before get_next_msg().
    buf: zircon::MessageBuf,
    idx: usize,
}

impl Watcher {
    /// Creates a new `Watcher` for the directory given by `dir`.
    pub fn new(dir: &File, handle: &TokioHandle) -> Result<Watcher, zircon::Status> {
        let (h0, h1) = zircon::Channel::create()?;
        let vwd = vfs_watch_dir_t {
            h: h1.into_raw(),
            mask: VFS_WATCH_MASK_ALL,
            options: 0,
        };
        zircon::Status::ok(
            // This is safe because no memory ownership is passed via fdio::ioctl.
            unsafe { fdio::ioctl(dir.as_raw_fd(),
                                 IOCTL_VFS_WATCH_DIR,
                                 &vwd as *const _ as *const raw::c_void,
                                 ::std::mem::size_of::<vfs_watch_dir_t>(),
                                 std::ptr::null_mut(),
                                 0) as i32 }
            )?;

        let mut buf = zircon::MessageBuf::new();
        buf.ensure_capacity_bytes(VFS_WATCH_MSG_MAX);
        Ok(Watcher{ ch: tokio_fuchsia::Channel::from_channel(h0, handle)?, buf: buf, idx: 0})
    }

    fn reset_buf(&mut self) {
        self.idx = 0;
        self.buf.clear();
    }

    fn get_next_msg(&mut self) -> WatchMessage {
        assert!(self.idx < self.buf.bytes().len());
        let next_msg = VfsWatchMsg::from_raw(&self.buf.bytes()[self.idx..])
            .expect("Invalid buffer received by Watcher!");
        self.idx += next_msg.len();

        let mut pathbuf = PathBuf::new();
        pathbuf.push(OsStr::from_bytes(next_msg.name()));
        let event = next_msg.event();
        WatchMessage { event: event, filename: pathbuf }
    }
}

impl Stream for Watcher {
    type Item = WatchMessage;
    type Error = io::Error;

    fn poll(&mut self) -> futures::Poll<Option<Self::Item>, Self::Error> {
        if self.idx >= self.buf.bytes().len() {
            self.reset_buf();
        }
        if self.idx == 0 {
            try_nb!(self.ch.recv_from(&mut self.buf));
        }
        Ok(Async::Ready(Some(self.get_next_msg())))
    }
}

#[repr(C)]
#[derive(Debug)]
struct vfs_watch_dir_t {
    h: zircon::sys::zx_handle_t,
    mask: u32,
    options: u32,
}

#[repr(C)]
#[derive(Debug)]
struct vfs_watch_msg_t {
    event: u8,
    len: u8,
    name: fdio_sys::__IncompleteArrayField<u8>,
}

#[derive(Debug)]
struct VfsWatchMsg<'a> {
    inner: &'a vfs_watch_msg_t,
}

impl<'a> VfsWatchMsg<'a> {
    fn from_raw(buf: &'a [u8]) -> Option<VfsWatchMsg<'a>> {
        if buf.len() < ::std::mem::size_of::<vfs_watch_msg_t>() {
            return None;
        }
        // This is safe as long as the buffer is at least as large as a vfs_watch_msg_t, which we
        // just verified. Further, we verify that the buffer has enough bytes to hold the
        // "incomplete array field" member.
        let m = unsafe { VfsWatchMsg{ inner: &*(buf.as_ptr() as *const vfs_watch_msg_t) } };
        if buf.len() < ::std::mem::size_of::<vfs_watch_msg_t>() + m.namelen() {
            return None;
        }
        Some(m)
    }

    fn len(&self) -> usize {
        ::std::mem::size_of::<vfs_watch_msg_t>() + self.namelen()
    }

    fn event(&self) -> WatchEvent {
        WatchEvent(self.inner.event)
    }

    fn namelen(&self) -> usize {
        self.inner.len as usize
    }

    fn name(&self) -> &'a [u8] {
        // This is safe because we verified during construction that the inner name field has at
        // least namelen() bytes in it.
        unsafe { self.inner.name.as_slice(self.namelen()) }
    }
}

const VFS_WATCH_EVT_ADDED: u8 = 1;
const VFS_WATCH_EVT_REMOVED: u8 = 2;
const VFS_WATCH_EVT_EXISTING: u8 = 3;
const VFS_WATCH_EVT_IDLE: u8 = 4;

const VFS_WATCH_MASK_ALL: u32 = 0x1fu32;
const VFS_WATCH_MSG_MAX: usize = 8192;

const IOCTL_VFS_WATCH_DIR: raw::c_int = make_ioctl!(
    fdio_sys::IOCTL_KIND_SET_HANDLE,
    fdio_sys::IOCTL_FAMILY_VFS,
    8
);

#[cfg(test)]
mod tests {
    extern crate tempdir;
    extern crate tokio_core;

    use futures::Future;
    use futures::future::Either;
    use self::tempdir::TempDir;
    use self::tokio_core::reactor;
    use std::path::Path;
    use std::time::Duration;
    use super::*;

    macro_rules! one_step {
        ($core:ident, $s:expr) => {
            {
                let timeout = reactor::Timeout::new(Duration::from_millis(50), &$core.handle()).unwrap();
                match $core.run($s.into_future().select2(timeout)) {
                    Ok(Either::A(((msg, rest), _))) => (msg.unwrap(), rest),
                    Ok(Either::B(_)) => panic!("Timed out waiting for watcher!"),
                    Err(Either::A((e, _))) => panic!("Error waiting for watcher: {:?}", e),
                    Err(Either::B((e, _))) => panic!("Error waiting for timeout: {:?}", e),
                }
            }
        }
    }

    #[test]
    fn test_existing() {
        let tmp_dir = TempDir::new("vfs-watcher-test-existing").unwrap();
        let _ = File::create(tmp_dir.path().join("file1")).unwrap();

        let mut core = reactor::Core::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let w = Watcher::new(&dir, &core.handle()).unwrap();

        // TODO(tkilbourn): this assumes "." always comes before "file1". If this test ever starts
        // flaking, handle the case of unordered EXISTING files.
        let (msg, rest) = one_step!(core, w);
        assert_eq!(WatchEvent::EXISTING, msg.event);
        assert_eq!(Path::new("."), msg.filename);

        let (msg, rest) = one_step!(core, rest);
        assert_eq!(WatchEvent::EXISTING, msg.event);
        assert_eq!(Path::new("file1"), msg.filename);

        let (msg, _) = one_step!(core, rest);
        assert_eq!(WatchEvent::IDLE, msg.event);
    }

    #[test]
    fn test_add() {
        let tmp_dir = TempDir::new("vfs-watcher-test-add").unwrap();

        let mut core = reactor::Core::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let mut w = Watcher::new(&dir, &core.handle()).unwrap();

        loop {
            let (msg, rest) = one_step!(core, w);
            w = rest;
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        let _ = File::create(tmp_dir.path().join("file1")).unwrap();
        let (msg, _) = one_step!(core, w);
        assert_eq!(WatchEvent::ADD_FILE, msg.event);
        assert_eq!(Path::new("file1"), msg.filename);
    }

    #[test]
    fn test_remove() {
        let tmp_dir = TempDir::new("vfs-watcher-test-remove").unwrap();
        let filename = "file1";
        let filepath = tmp_dir.path().join(filename);
        let _ = File::create(&filepath).unwrap();

        let mut core = reactor::Core::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let mut w = Watcher::new(&dir, &core.handle()).unwrap();

        loop {
            let (msg, rest) = one_step!(core, w);
            w = rest;
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        ::std::fs::remove_file(&filepath).unwrap();
        let (msg, _) = one_step!(core, w);
        assert_eq!(WatchEvent::REMOVE_FILE, msg.event);
        assert_eq!(Path::new(filename), msg.filename);
    }

    #[test]
    #[should_panic]
    fn test_timeout() {
        let tmp_dir = TempDir::new("vfs-watcher-test-timeout").unwrap();

        let mut core = reactor::Core::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let mut w = Watcher::new(&dir, &core.handle()).unwrap();

        loop {
            let (msg, rest) = one_step!(core, w);
            w = rest;
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        // Ensure that our test timeouts actually work by waiting for another event that will never
        // arrive.
        let _ = one_step!(core, w);
    }
}
