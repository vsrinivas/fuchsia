// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use libc::PATH_MAX;

use std::sync::Arc;
use tokio_core;
use zircon;
use tokio_fuchsia;
use futures;
use futures::Future;
use std;
use std::io;
use std::os::unix::ffi::OsStrExt;
use fdio;

/// Vfs contains filesystem global state and outlives all Vnodes that it
/// services. It fundamentally handles filesystem global concerns such as path
/// walking through mounts, moves and links, watchers, and so on.
pub trait Vfs {
    fn open(
        &self,
        _vn: &Arc<Vnode>,
        _path: std::path::PathBuf,
        _flags: i32,
        _mode: u32,
    ) -> Result<(Arc<Vnode>, std::path::PathBuf), zircon::Status> {
        // TODO(raggi): ...
        Err(zircon::Status::NOT_SUPPORTED)
    }

    fn register_connection(&self, c: Connection, handle: &tokio_core::reactor::Handle) {
        handle.spawn(c.map_err(
            |e| eprintln!("fuchsia-vfs: connection error {:?}", e),
        ))
    }
}

/// Vnode represents a single addressable node in the filesystem (that may be
/// addressable via more than one path). It may have file, directory, mount or
/// device semantics.
pub trait Vnode {
    fn close(&self) -> zircon::Status {
        zircon::Status::OK
    }

    fn serve(&self, _vfs: Arc<Vfs>, _chan: tokio_fuchsia::Channel, _flags: i32) {
        // TODO(raggi): ...
        // TODO(raggi): ...
        // TODO(raggi): ...
        // TODO(raggi): ...
    }
}

/// Connection represents a single client connection to a Vnode within a Vfs. It
/// contains applicable IO state such as current position, as well as the channel
/// on which the IO is served.
pub struct Connection {
    vfs: Arc<Vfs>,
    vn: Arc<Vnode>,
    chan: tokio_fuchsia::Channel,
    handle: tokio_core::reactor::Handle,
}

impl Connection {
    pub fn new(
        vfs: Arc<Vfs>,
        vn: Arc<Vnode>,
        chan: zircon::Channel,
        handle: &tokio_core::reactor::Handle,
    ) -> Result<Connection, io::Error> {
        let c = Connection {
            vfs: vfs,
            vn: vn,
            chan: tokio_fuchsia::Channel::from_channel(chan, handle)?,
            handle: handle.clone(),
        };

        Ok(c)
    }

    fn dispatch(&mut self, msg: &mut fdio::rio::Message) -> Result<(), std::io::Error> {
        // TODO(raggi): in the case of protocol errors for non-pipelined opens,
        // we sometimes will fail to send an appropriate object description back
        // to the serving channel. This needs to be addressed.
        msg.validate().map_err(|_| {
            std::io::Error::from(std::io::ErrorKind::InvalidInput)
        })?;

        println!("{:?} <- {:?}", self.chan, msg);

        match msg.op() {
            fdio::fdio_sys::ZXRIO_OPEN => {
                let chan = tokio_fuchsia::Channel::from_channel(
                    zircon::Channel::from(
                        msg.take_handle(0).expect("vfs: handle disappeared"),
                    ),
                    &self.handle,
                )?;

                // TODO(raggi): enforce O_ADMIN
                if msg.datalen() < 1 || msg.datalen() > PATH_MAX as u32 {
                    self.reply_status(&chan, zircon::Status::INVALID_ARGS)?;
                    return Err(zircon::Status::INVALID_ARGS.into());
                }

                let path = std::path::PathBuf::from(std::ffi::OsStr::from_bytes(msg.data()));

                // TODO(raggi): verify if the protocol mistreatment of args signage is intentionally unchecked here:
                self.open(chan, path, msg.arg(), msg.mode())?;
            }
            // ZXRIO_STAT => self.stat(msg, chan, handle),
            // ZXRIO_CLOSE => self.close(msg, chan, handle),
            _ => {
                self.reply_status(
                    &self.chan,
                    zircon::Status::NOT_SUPPORTED,
                )?
            }
        }

        Ok(())
    }

    fn open(
        &self,
        chan: tokio_fuchsia::Channel,
        path: std::path::PathBuf,
        flags: i32,
        mode: u32,
    ) -> Result<(), std::io::Error> {
        let pipeline = flags & fdio::fdio_sys::O_PIPELINE != 0;
        let open_flags = flags & !fdio::fdio_sys::O_PIPELINE;

        let mut status = zircon::Status::OK;
        let mut proto = fdio::fdio_sys::FDIO_PROTOCOL_REMOTE;
        let mut handles: Vec<zircon::Handle> = vec![];

        match self.vfs.open(&self.vn, path, open_flags, mode) {
            Ok((vn, _path)) => {
                // TODO(raggi): get_handles (maybe call it get_extra?)

                // protocols that return handles on open can't be pipelined.
                if pipeline && handles.len() > 0 {
                    vn.close();
                    return Err(std::io::ErrorKind::InvalidInput.into());
                }

                if status != zircon::Status::OK {
                    return Err(std::io::ErrorKind::InvalidInput.into());
                }

                if !pipeline {
                    fdio::rio::write_object(&chan, status, proto, &[], &mut handles).ok();
                }

                // TODO(raggi): construct connection...
                vn.serve(Arc::clone(&self.vfs), chan, open_flags);

                return Ok(());
            }
            Err(e) => {
                proto = 0;
                status = e;
                eprintln!("vfs: open error: {:?}", e);
            }
        }

        if !pipeline {
            return fdio::rio::write_object(&chan, status, proto, &[], &mut handles)
                .map_err(Into::into);
        }
        Ok(())
    }

    fn reply_status(
        &self,
        chan: &tokio_fuchsia::Channel,
        status: zircon::Status,
    ) -> Result<(), io::Error> {
        println!("{:?} -> {:?}", &chan, status);
        fdio::rio::write_object(chan, status, 0, &[], &mut vec![]).map_err(Into::into)
    }
}

impl Future for Connection {
    type Item = ();
    type Error = io::Error;

    fn poll(&mut self) -> futures::Poll<Self::Item, Self::Error> {
        let mut buf = zircon::MessageBuf::new();
        buf.ensure_capacity_bytes(fdio::fdio_sys::ZXRIO_MSG_SZ);
        loop {
            try_nb!(self.chan.recv_from(0, &mut buf));
            let mut msg = buf.into();
            // Note: ignores errors, as they are sent on the protocol
            let _ = self.dispatch(&mut msg);
            buf = msg.into();
            buf.clear();
        }
    }
}
