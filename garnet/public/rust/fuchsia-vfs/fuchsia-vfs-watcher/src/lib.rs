// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Stream-based Fuchsia VFS directory watcher

#![deny(missing_docs)]

use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, assoc_values};

use fdio::fdio_sys;
use fidl_fuchsia_io::WATCH_MASK_ALL;
use futures::stream::{FusedStream, Stream};
use std::ffi::OsStr;
use std::io;
use std::marker::Unpin;
use std::os::unix::ffi::OsStrExt;
use std::path::PathBuf;
use std::pin::Pin;
use std::task::{Context, Poll};

/// Describes the type of event that occurred in the direcotry being watched.
#[repr(C)]
#[derive(Copy, Clone, Eq, PartialEq)]
pub struct WatchEvent(u8);

assoc_values!(WatchEvent, [
    /// A file was added.
    ADD_FILE    = fidl_fuchsia_io::WATCH_EVENT_ADDED;
    /// A file was removed.
    REMOVE_FILE = fidl_fuchsia_io::WATCH_EVENT_REMOVED;
    /// A file existed at the time the Watcher was created.
    EXISTING    = fidl_fuchsia_io::WATCH_EVENT_EXISTING;
    /// All existing files have been enumerated.
    IDLE        = fidl_fuchsia_io::WATCH_EVENT_IDLE;
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
    ch: fasync::Channel,
    // If idx >= buf.bytes().len(), you must call reset_buf() before get_next_msg().
    buf: zx::MessageBuf,
    idx: usize,
}

impl Unpin for Watcher {}

impl Watcher {
    /// Creates a new `Watcher` for the directory given by `dir`.
    pub async fn new(dir: fidl_fuchsia_io::DirectoryProxy) -> Result<Watcher, failure::Error> {
        let (h0, h1) = zx::Channel::create()?;
        let options = 0u32;
        let status = dir.watch(WATCH_MASK_ALL, options, h1).await?;
        zx::Status::ok(status)?;
        let mut buf = zx::MessageBuf::new();
        buf.ensure_capacity_bytes(fidl_fuchsia_io::MAX_BUF as usize);
        Ok(Watcher { ch: fasync::Channel::from_channel(h0)?, buf, idx: 0 })
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

impl FusedStream for Watcher {
    fn is_terminated(&self) -> bool {
        // `Watcher` never completes
        // (FIXME: or does it? is an error completion?)
        false
    }
}

impl Stream for Watcher {
    type Item = Result<WatchMessage, io::Error>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = &mut *self;
        if this.idx >= this.buf.bytes().len() {
            this.reset_buf();
        }
        if this.idx == 0 {
            match this.ch.recv_from(cx, &mut this.buf) {
                Poll::Ready(Ok(())) => {}
                Poll::Ready(Err(e)) => return Poll::Ready(Some(Err(e.into()))),
                Poll::Pending => return Poll::Pending,
            }
        }
        Poll::Ready(Some(Ok(this.get_next_msg())))
    }
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
        let m = unsafe { VfsWatchMsg { inner: &*(buf.as_ptr() as *const vfs_watch_msg_t) } };
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

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async::{self as fasync, DurationExt, TimeoutExt};
    use fuchsia_zircon::prelude::*;
    use futures::prelude::*;
    use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
    use std::fmt::Debug;
    use std::fs::File;
    use std::path::Path;
    use tempfile::tempdir;

    fn one_step<'a, S, OK, ERR>(s: &'a mut S) -> impl Future<Output = OK> + 'a
    where
        S: Stream<Item = Result<OK, ERR>> + Unpin,
        ERR: Debug,
    {
        let f = s.next();
        let f = f.on_timeout(500.millis().after_now(), || panic!("timeout waiting for watcher"));
        f.map(|next| {
            next.expect("the stream yielded no next item")
                .unwrap_or_else(|e| panic!("Error waiting for watcher: {:?}", e))
        })
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_existing() {
        let tmp_dir = tempdir().unwrap();
        let _ = File::create(tmp_dir.path().join("file1")).unwrap();

        let dir =
            open_directory_in_namespace(tmp_dir.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
                .unwrap();
        let mut w = Watcher::new(dir).await.unwrap();

        // TODO(tkilbourn): this assumes "." always comes before "file1". If this test ever starts
        // flaking, handle the case of unordered EXISTING files.
        let msg = one_step(&mut w).await;
        assert_eq!(WatchEvent::EXISTING, msg.event);
        assert_eq!(Path::new("."), msg.filename);

        let msg = one_step(&mut w).await;
        assert_eq!(WatchEvent::EXISTING, msg.event);
        assert_eq!(Path::new("file1"), msg.filename);

        let msg = one_step(&mut w).await;
        assert_eq!(WatchEvent::IDLE, msg.event);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_add() {
        let tmp_dir = tempdir().unwrap();

        let dir =
            open_directory_in_namespace(tmp_dir.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
                .unwrap();
        let mut w = Watcher::new(dir).await.unwrap();

        loop {
            let msg = one_step(&mut w).await;
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        let _ = File::create(tmp_dir.path().join("file1")).unwrap();
        let msg = one_step(&mut w).await;
        assert_eq!(WatchEvent::ADD_FILE, msg.event);
        assert_eq!(Path::new("file1"), msg.filename);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_remove() {
        let tmp_dir = tempdir().unwrap();

        let filename = "file1";
        let filepath = tmp_dir.path().join(filename);
        let _ = File::create(&filepath).unwrap();

        let dir =
            open_directory_in_namespace(tmp_dir.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
                .unwrap();
        let mut w = Watcher::new(dir).await.unwrap();

        loop {
            let msg = one_step(&mut w).await;
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        ::std::fs::remove_file(&filepath).unwrap();
        let msg = one_step(&mut w).await;
        assert_eq!(WatchEvent::REMOVE_FILE, msg.event);
        assert_eq!(Path::new(filename), msg.filename);
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic]
    async fn test_timeout() {
        let tmp_dir = tempdir().unwrap();

        let dir =
            open_directory_in_namespace(tmp_dir.path().to_str().unwrap(), OPEN_RIGHT_READABLE)
                .unwrap();
        let mut w = Watcher::new(dir).await.unwrap();

        loop {
            let msg = one_step(&mut w).await;
            match msg.event {
                WatchEvent::EXISTING => continue,
                WatchEvent::IDLE => break,
                _ => panic!("Unexpected watch event!"),
            }
        }

        // Ensure that our test timeouts actually work by waiting for another event that will never
        // arrive.
        let _ = one_step(&mut w).await;
    }
}
