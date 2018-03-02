// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use libc::PATH_MAX;
use futures::io;
use futures::prelude::*;

use async;
use zx;
use fdio;

use std::path::PathBuf;
use std::ffi::OsStr;
use std::os::unix::ffi::OsStrExt;
use std::sync::Arc;

/// Vfs contains filesystem global state and outlives all Vnodes that it
/// services. It fundamentally handles filesystem global concerns such as path
/// walking through mounts, moves and links, watchers, and so on.
pub trait Vfs: 'static + Send + Sync {
    fn open(
        &self,
        _vn: &Arc<Vnode>,
        _path: PathBuf,
        _flags: i32,
        _mode: u32,
    ) -> Result<(Arc<Vnode>, PathBuf), zx::Status> {
        // TODO(raggi): ...
        Err(zx::Status::NOT_SUPPORTED)
    }

    fn register_connection(&self, c: Connection) {
        async::spawn(c.recover(
            |e| eprintln!("fuchsia-vfs: connection error {:?}", e),
        ));
    }
}

/// Vnode represents a single addressable node in the filesystem (that may be
/// addressable via more than one path). It may have file, directory, mount or
/// device semantics.
pub trait Vnode: 'static + Send + Sync {
    fn close(&self) -> zx::Status {
        zx::Status::OK
    }

    fn serve(&self, _vfs: Arc<Vfs>, _chan: async::Channel, _flags: i32) {
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
    chan: async::Channel,
}

impl Connection {
    pub fn new(
        vfs: Arc<Vfs>,
        vn: Arc<Vnode>,
        chan: zx::Channel,
    ) -> Result<Connection, io::Error> {
        let c = Connection {
            vfs: vfs,
            vn: vn,
            chan: async::Channel::from_channel(chan)?,
        };

        Ok(c)
    }

    fn dispatch(&mut self, msg: &mut fdio::rio::Message) -> Result<(), io::Error> {
        // TODO(raggi): in the case of protocol errors for non-pipelined opens,
        // we sometimes will fail to send an appropriate object description back
        // to the serving channel. This needs to be addressed.
        msg.validate().map_err(|_| {
            io::Error::from(io::ErrorKind::InvalidInput)
        })?;

        println!("{:?} <- {:?}", self.chan, msg);

        match msg.op() {
            fdio::fdio_sys::ZXRIO_OPEN => {
                let chan = async::Channel::from_channel(
                    zx::Channel::from(
                        msg.take_handle(0).expect("vfs: handle disappeared"),
                    ),
                )?;

                // TODO(raggi): enforce O_ADMIN
                if msg.datalen() < 1 || msg.datalen() > PATH_MAX as u32 {
                    self.reply_status(&chan, zx::Status::INVALID_ARGS)?;
                    return Err(zx::Status::INVALID_ARGS.into());
                }

                let path = PathBuf::from(OsStr::from_bytes(msg.data()));

                // TODO(raggi): verify if the protocol mistreatment of args signage is intentionally unchecked here:
                self.open(chan, path, msg.arg(), msg.mode())?;
            }
            // ZXRIO_STAT => self.stat(msg, chan, handle),
            // ZXRIO_CLOSE => self.close(msg, chan, handle),
            _ => {
                self.reply_status(
                    &self.chan,
                    zx::Status::NOT_SUPPORTED,
                )?
            }
        }

        Ok(())
    }

    fn open(
        &self,
        chan: async::Channel,
        path: PathBuf,
        flags: i32,
        mode: u32,
    ) -> Result<(), io::Error> {
        let describe = (flags as u32) & fdio::fdio_sys::ZX_FS_FLAG_DESCRIBE != 0;
        let open_flags = flags & (!fdio::fdio_sys::ZX_FS_FLAG_DESCRIBE as i32);

        let mut status = zx::Status::OK;
        let mut proto = fdio::fdio_sys::FDIO_PROTOCOL_SERVICE;
        let mut handles: Vec<zx::Handle> = vec![];

        match self.vfs.open(&self.vn, path, open_flags, mode) {
            Ok((vn, _path)) => {
                // TODO(raggi): get_handles (maybe call it get_extra?)

                // protocols that return handles on open can't be pipelined.
                if !describe && handles.len() > 0 {
                    vn.close();
                    return Err(io::ErrorKind::InvalidInput.into());
                }

                if status != zx::Status::OK {
                    return Err(io::ErrorKind::InvalidInput.into());
                }

                if describe {
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

        if describe {
            return fdio::rio::write_object(&chan, status, proto, &[], &mut handles)
                .map_err(Into::into);
        }
        Ok(())
    }

    fn reply_status(
        &self,
        chan: &async::Channel,
        status: zx::Status,
    ) -> Result<(), io::Error> {
        println!("{:?} -> {:?}", &chan, status);
        fdio::rio::write_object(chan, status, 0, &[], &mut vec![]).map_err(Into::into)
    }
}

impl Future for Connection {
    type Item = ();
    type Error = io::Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        let mut buf = zx::MessageBuf::new();
        buf.ensure_capacity_bytes(fdio::fdio_sys::ZXRIO_MSG_SZ);
        loop {
            try_ready!(self.chan.recv_from(&mut buf, cx));
            let mut msg = buf.into();
            // Note: ignores errors, as they are sent on the protocol
            let _ = self.dispatch(&mut msg);
            buf = msg.into();
            buf.clear();
        }
    }
}
