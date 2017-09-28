// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes;
use bytes::BufMut;
use std::sync::Arc;
use tokio_core;
use zircon;
use tokio_fuchsia;
use futures;
use futures::Future;
use std;
use std::io;
use std::borrow::Borrow;

use remoteio::*;

/// Vfs contains filesystem global state and outlives all Vnodes that it
/// services. It fundamentally handles filesystem global concerns such as path
/// walking through mounts, moves and links, watchers, and so on.
pub trait Vfs {
    /*
TODOs:
  // TODO(raggi): support remote node dance, returning a path too (open):
  fn open(vn: Vnode, path: Path, flags: usize, mode: usize) -> Result<Vnode, zircon::Status>
  fn unlink(vn: Vnode, path: Path) -> Result<(), zircon::Status>
  fn ioctl(vn: Vnode, ...) -> Result<..., zircon::Status>

  fn link()
  fn rename()
  fn readdir()
  */

    fn register_connection(&self, c: Connection, handle: &tokio_core::reactor::Handle) {
        handle.spawn(c.map_err(|e| eprintln!("fuchsia-vfs: connection error {:?}", e)))
    }
}

/// Vnode represents a single addressable node in the filesystem (that may be
/// addressable via more than one path). It may have file, directory, mount or
/// device semantics.
pub trait Vnode {}

/// Connection represents a single client connection to a Vnode within a Vfs. It
/// contains applicable IO state such as current position, as well as the channel
/// on which the IO is served.
pub struct Connection {
    vfs: Arc<Vfs>,
    vn: Arc<Vnode>,
    chan: tokio_fuchsia::Channel,
    buf: zircon::MessageBuf,
    handle: tokio_core::reactor::Handle,
}

impl Connection {
    pub fn new(
        vfs: Arc<Vfs>,
        vn: Arc<Vnode>,
        chan: zircon::Channel,
        handle: &tokio_core::reactor::Handle,
    ) -> Result<Connection, io::Error> {
        let mut c = Connection {
            vfs: vfs,
            vn: vn,
            chan: tokio_fuchsia::Channel::from_channel(chan, handle)?,
            buf: zircon::MessageBuf::new(),
            handle: handle.clone(),
        };

        c.buf.ensure_capacity_bytes(ZXRIO_MSG_SZ);
        c.buf.ensure_capacity_handles(FDIO_MAX_HANDLES);

        Ok(c)
    }

    fn dispatch(&mut self) {
        let len = self.buf.bytes().len();

        if len < ZXRIO_HDR_SZ {
            eprintln!("vfs: channel read too short: {} {}", len, ZXRIO_HDR_SZ);
            return;
        }

        // NOTE(raggi): we're explicitly and deliberately breaching the type and
        // the mutability here. We own the messagebuf.
        let msg: &mut zxrio_msg_t = unsafe { &mut *(self.buf.bytes().as_ptr() as *mut _) };

        if len > FDIO_CHUNK_SIZE || self.buf.n_handles() > FDIO_MAX_HANDLES
            || len != msg.datalen as usize + ZXRIO_HDR_SZ
            || self.buf.n_handles() != ZXRIO_HC!(msg.op) as usize
        {
            eprintln!(
                "vfs: invalid message: len: {}, nhandles: {}, {:?}",
                len,
                self.buf.n_handles(),
                msg
            );
            self.reply_status(&self.chan, zircon::Status::ErrInvalidArgs);
            return;
        }

        println!("{:?} <- {:?}", self.chan, msg);

        match ZXRIO_OP!(msg.op) {
            // ZXRIO_OPEN => self.open(msg, chan, handle),
            // ZXRIO_STAT => self.stat(msg, chan, handle),
            // ZXRIO_CLOSE => self.close(msg, chan, handle),
            _ => {
                // TODO(raggi): remove this special case when open is implemented.
                if msg.op == ZXRIO_OPEN {
                    if let Some(h) = self.buf.take_handle(0) {
                        let chan: tokio_fuchsia::Channel = tokio_fuchsia::Channel::from_channel(
                            zircon::Channel::from(h),
                            &self.handle,
                        ).unwrap();

                        self.reply_status(&chan, zircon::Status::ErrNotSupported);
                    }
                } else {
                    self.reply_status(&self.chan, zircon::Status::ErrNotSupported);
                }
            }
        }
    }

    fn reply_status(&self, chan: &tokio_fuchsia::Channel, status: zircon::Status) {
        println!("{:?} -> {:?}", &chan, status);

        if let Err(e) = self.write_zxrio_object(chan, status, 0, &[], &mut vec![]) {
            eprintln!("vfs: unable to write error {:?}", e);
        }
    }

    fn write_zxrio_object(
        &self,
        chan: &tokio_fuchsia::Channel,
        status: zircon::Status,
        typ: u32,
        extra: &[u8],
        handles: &mut Vec<zircon::Handle>,
    ) -> Result<(), io::Error> {
        if extra.len() > ZXRIO_OBJECT_EXTRA || handles.len() > FDIO_MAX_HANDLES as usize {
            return Err(io::ErrorKind::InvalidInput.into());
        }

        let mut buf = bytes::BytesMut::with_capacity(ZXRIO_OBJECT_MINSIZE + extra.len());
        buf.put_i32::<bytes::LittleEndian>(status as i32);
        buf.put_u32::<bytes::LittleEndian>(typ);
        buf.put_slice(extra);

        chan.write(buf.borrow(), handles, 0)
    }
}

impl Future for Connection {
    type Item = ();
    type Error = io::Error;

    fn poll(&mut self) -> futures::Poll<Self::Item, Self::Error> {
        loop {
            try_nb!(self.chan.recv_from(0, &mut self.buf));
            self.dispatch();
            for i in 0..self.buf.n_handles() {
                if let Some(h) = self.buf.take_handle(i) {
                    std::mem::drop(h);
                }
            }
        }
    }
}
