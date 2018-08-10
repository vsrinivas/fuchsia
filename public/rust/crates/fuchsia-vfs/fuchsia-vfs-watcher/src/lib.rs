// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Stream-based Fuchsia VFS directory watcher

#![deny(missing_docs)]
#![feature(futures_api, pin, arbitrary_self_types)]

#[macro_use]
extern crate fdio;
extern crate fuchsia_async as async;
#[macro_use]
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate pin_utils;

use fdio::fdio_sys;
use futures::{Poll, Stream, task};
use std::ffi::OsStr;
use std::fs::File;
use std::io;
use std::mem::PinMut;
use std::marker::Unpin;
use std::os::raw;
use std::os::unix::ffi::OsStrExt;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;
use zx::HandleBased;

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
    ch: async::Channel,
    // If idx >= buf.bytes().len(), you must call reset_buf() before get_next_msg().
    buf: zx::MessageBuf,
    idx: usize,
}

impl Unpin for Watcher {}

impl Watcher {
    /// Creates a new `Watcher` for the directory given by `dir`.
    pub fn new(dir: &File) -> Result<Watcher, zx::Status> {
        let (h0, h1) = zx::Channel::create()?;
        let vwd = vfs_watch_dir_t {
            h: h1.into_raw(),
            mask: VFS_WATCH_MASK_ALL,
            options: 0,
        };
        zx::Status::ok(
            // This is safe because no memory ownership is passed via fdio::ioctl.
            unsafe { fdio::ioctl_raw(dir.as_raw_fd(),
                                 IOCTL_VFS_WATCH_DIR,
                                 &vwd as *const _ as *const raw::c_void,
                                 ::std::mem::size_of::<vfs_watch_dir_t>(),
                                 std::ptr::null_mut(),
                                 0) as i32 }
            )?;

        let mut buf = zx::MessageBuf::new();
        buf.ensure_capacity_bytes(VFS_WATCH_MSG_MAX);
        Ok(Watcher{ ch: async::Channel::from_channel(h0)?, buf: buf, idx: 0})
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
    type Item = Result<WatchMessage, io::Error>;

    fn poll_next(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Option<Self::Item>> {
        let this = &mut *self;
        if this.idx >= this.buf.bytes().len() {
            this.reset_buf();
        }
        if this.idx == 0 {
            match this.ch.recv_from(&mut this.buf, cx) {
                Poll::Ready(Ok(())) => {},
                Poll::Ready(Err(e)) => return Poll::Ready(Some(Err(e.into()))),
                Poll::Pending => return Poll::Pending,
            }
        }
        Poll::Ready(Some(Ok(this.get_next_msg())))
    }
}

#[repr(C)]
#[derive(Debug)]
struct vfs_watch_dir_t {
    h: zx::sys::zx_handle_t,
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

    use super::*;

    use async::TimeoutExt;
    use futures::prelude::*;
    use self::tempdir::TempDir;
    use std::fmt::Debug;
    use std::path::Path;
    use zx::prelude::*;

    fn one_step<S, OK, ERR>(exec: &mut async::Executor, s: &mut S) -> OK
        where S: Stream<Item = Result<OK, ERR>> + Unpin,
              ERR: Debug
    {
        let f = s.next();
        let f = f.on_timeout(
            500.millis().after_now(),
            || panic!("timeout waiting for watcher")
        );

        let next = exec.run_singlethreaded(f);

        next.expect("the stream yielded no next item")
            .unwrap_or_else(|e| panic!("Error waiting for watcher: {:?}", e))
    }

    #[test]
    fn test_existing() {
        let tmp_dir = TempDir::new("vfs-watcher-test-existing").unwrap();
        let _ = File::create(tmp_dir.path().join("file1")).unwrap();

        let exec = &mut async::Executor::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let w = Watcher::new(&dir).unwrap();

        // TODO(tkilbourn): this assumes "." always comes before "file1". If this test ever starts
        // flaking, handle the case of unordered EXISTING files.
        pin_mut!(w);
        let msg = one_step(exec, &mut w);
        assert_eq!(WatchEvent::EXISTING, msg.event);
        assert_eq!(Path::new("."), msg.filename);

        let msg = one_step(exec, &mut w);
        assert_eq!(WatchEvent::EXISTING, msg.event);
        assert_eq!(Path::new("file1"), msg.filename);

        let msg = one_step(exec, &mut w);
        assert_eq!(WatchEvent::IDLE, msg.event);
    }

    #[test]
    fn test_add() {
        let tmp_dir = TempDir::new("vfs-watcher-test-add").unwrap();

        let exec = &mut async::Executor::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let w = Watcher::new(&dir).unwrap();
        pin_mut!(w);

        loop {
            let msg = one_step(exec, &mut w);
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        let _ = File::create(tmp_dir.path().join("file1")).unwrap();
        let msg = one_step(exec, &mut w);
        assert_eq!(WatchEvent::ADD_FILE, msg.event);
        assert_eq!(Path::new("file1"), msg.filename);
    }

    #[test]
    fn test_remove() {
        let tmp_dir = TempDir::new("vfs-watcher-test-remove").unwrap();
        let filename = "file1";
        let filepath = tmp_dir.path().join(filename);
        let _ = File::create(&filepath).unwrap();

        let exec = &mut async::Executor::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let w = Watcher::new(&dir).unwrap();
        pin_mut!(w);

        loop {
            let msg = one_step(exec, &mut w);
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        ::std::fs::remove_file(&filepath).unwrap();
        let msg = one_step(exec, &mut w);
        assert_eq!(WatchEvent::REMOVE_FILE, msg.event);
        assert_eq!(Path::new(filename), msg.filename);
    }

    #[test]
    #[should_panic]
    fn test_timeout() {
        let tmp_dir = TempDir::new("vfs-watcher-test-timeout").unwrap();

        let exec = &mut async::Executor::new().unwrap();
        let dir = File::open(tmp_dir.path()).unwrap();
        let w = Watcher::new(&dir).unwrap();
        pin_mut!(w);

        loop {
            let msg = one_step(exec, &mut w);
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        // Ensure that our test timeouts actually work by waiting for another event that will never
        // arrive.
        let _ = one_step(exec, &mut w);
    }
}
