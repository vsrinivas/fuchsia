// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// rio provides some utilities for working with the Zircon RemoteIO protocol.

use bytes;
use bytes::BufMut;
use zircon;
use fdio_sys::*;
use std::os::raw::c_uint;

pub use fdio_sys::ZXRIO_STATUS;
pub use fdio_sys::ZXRIO_CLOSE;
pub use fdio_sys::ZXRIO_CLONE;
pub use fdio_sys::ZXRIO_OPEN;
pub use fdio_sys::ZXRIO_MISC;
pub use fdio_sys::ZXRIO_READ;
pub use fdio_sys::ZXRIO_WRITE;
pub use fdio_sys::ZXRIO_SEEK;
pub use fdio_sys::ZXRIO_STAT;
pub use fdio_sys::ZXRIO_READDIR;
pub use fdio_sys::ZXRIO_IOCTL;
pub use fdio_sys::ZXRIO_IOCTL_1H;
pub use fdio_sys::ZXRIO_UNLINK;
pub use fdio_sys::ZXRIO_READ_AT;
pub use fdio_sys::ZXRIO_WRITE_AT;
pub use fdio_sys::ZXRIO_TRUNCATE;
pub use fdio_sys::ZXRIO_RENAME;
pub use fdio_sys::ZXRIO_CONNECT;
pub use fdio_sys::ZXRIO_BIND;
pub use fdio_sys::ZXRIO_LISTEN;
pub use fdio_sys::ZXRIO_GETSOCKNAME;
pub use fdio_sys::ZXRIO_GETPEERNAME;
pub use fdio_sys::ZXRIO_GETSOCKOPT;
pub use fdio_sys::ZXRIO_SETSOCKOPT;
pub use fdio_sys::ZXRIO_GETADDRINFO;
pub use fdio_sys::ZXRIO_SETATTR;
pub use fdio_sys::ZXRIO_SYNC;
pub use fdio_sys::ZXRIO_LINK;
pub use fdio_sys::ZXRIO_MMAP;
pub use fdio_sys::ZXRIO_FCNTL;

macro_rules! ZXRIO_HC {
    ($x:expr) => (($x >> 8) & 3);
}

/// Message, given a zircon::MessageBuf that has received a zxrio_msg_t in
/// buf.bytes, provides access to the values in the zxrio_msg_t.
pub struct Message {
    buf: zircon::MessageBuf,
    msg: *const zxrio_msg_t,
}

impl Message {
    pub fn validate(&self) -> Result<(), zircon::Status> {
        if self.buf.bytes().len() < ZXRIO_HDR_SZ {
            return Err(zircon::Status::IO);
        }

        if self.buf.bytes().len() > ZXRIO_MSG_SZ {
            return Err(zircon::Status::IO);
        }

        if self.buf.bytes().len() != ZXRIO_HDR_SZ + unsafe { *self.msg }.datalen as usize {
            return Err(zircon::Status::IO);
        }

        if self.buf.n_handles() != ZXRIO_HC!(unsafe { *self.msg }.op) as usize ||
            self.buf.n_handles() != unsafe { *self.msg }.hcount as usize
        {
            return Err(zircon::Status::INVALID_ARGS);
        }

        Ok(())
    }

    pub fn txid(&self) -> zx_txid_t {
        unsafe { *self.msg }.txid
    }

    pub fn reserved0(&self) -> zx_txid_t {
        unsafe { *self.msg }.reserved0
    }

    pub fn flags(&self) -> zx_txid_t {
        unsafe { *self.msg }.flags
    }

    /// op returns the type of IO operation requested
    pub fn op(&self) -> c_uint {
        unsafe { *self.msg }.op
    }

    pub fn datalen(&self) -> c_uint {
        ::std::cmp::min(unsafe { *self.msg }.datalen, FDIO_CHUNK_SIZE)
    }

    pub fn arg(&self) -> i32 {
        unsafe { *self.msg }.arg
    }

    // NOTE: these arg2 union fields are always being extracted from the result of
    // a buffer read, meaning they have always been initialized.
    pub fn off(&self) -> i64 {
        unsafe { (*self.msg).arg2.off }
    }

    pub fn mode(&self) -> u32 {
        unsafe { (*self.msg).arg2.mode }
    }

    pub fn protocol(&self) -> u32 {
        unsafe { (*self.msg).arg2.protocol }
    }

    pub fn arg2op(&self) -> u32 {
        unsafe { (*self.msg).arg2.op }
    }

    pub fn reserved1(&self) -> i32 {
        unsafe { *self.msg }.reserved1
    }

    pub fn hcount(&self) -> u32 {
        unsafe { *self.msg }.hcount
    }

    pub fn opname(&self) -> &'static str {
        unsafe { ::std::ffi::CStr::from_ptr(fdio_opname(self.op())) }
            .to_str()
            .unwrap()
    }

    /// the size of data is always bounded to initialized memory. if the read was
    /// short, and valid() returns false, data will only return up to the size of
    /// the actually read buffer, or up to msg.datalen, or up to FDIO_CHUNK_SIZE,
    /// whichever is smaller.
    pub fn data(&self) -> &[u8] {
        let len = ::std::cmp::min(
            self.datalen(),
            (self.buf.bytes().len() - ZXRIO_HDR_SZ) as c_uint,
        );
        unsafe { ::std::slice::from_raw_parts(&(*self.msg).data[0], len as usize) }
    }

    pub fn take_handle(&mut self, i: usize) -> Option<zircon::Handle> {
        self.buf.take_handle(i)
    }

    pub fn is_pipelined(&self) -> bool {
        self.arg() & O_PIPELINE != 0
    }
}

impl Into<zircon::MessageBuf> for Message {
    fn into(self) -> zircon::MessageBuf {
        self.buf
    }
}

impl From<zircon::MessageBuf> for Message {
    fn from(mut buf: zircon::MessageBuf) -> Self {
        buf.ensure_initialized_bytes(ZXRIO_HDR_SZ);
        let msg = buf.bytes().as_ptr() as *const _ as *const zxrio_msg_t;
        Self { buf: buf, msg: msg }
    }
}

impl<'a> ::std::fmt::Debug for Message {
    fn fmt(&self, fmt: &mut ::std::fmt::Formatter) -> ::std::fmt::Result {
        fmt.debug_struct("zxrio_msg")
            .field("txid", &self.txid())
            .field("reserved0", &self.reserved0())
            .field("flags", &self.flags())
            .field("op", &self.opname())
            .field("datalen", &self.datalen())
            .field("arg", &self.arg())
            // XXX(raggi): this could read uninitialized memory. As this struct
            // is to be written to and read from the wire, doing so is an
            // information leak that must be fixed. The struct should always be
            // initialized or read from the wire.
            .field("arg2", &self.off())
            .field("reserved1", &self.reserved1())
            .field("hcount", &self.hcount())
            .field("data", &self.data())
            .finish()
    }
}

/// write_object writes a zxrio_object_t to the given channel, along with any given extra data and handles. extra must not exceed ZXRIO_OBJECT_EXTRA, and handles must not exceed FDIO_MAX_HANDLES.
pub fn write_object<C: AsRef<zircon::Channel>>(
    chan: C,
    status: zircon::Status,
    type_: u32,
    extra: &[u8],
    handles: &mut Vec<zircon::Handle>,
) -> Result<(), zircon::Status> {
    if extra.len() > ZXRIO_OBJECT_EXTRA as usize {
        return Err(zircon::Status::INVALID_ARGS);
    }
    if handles.len() > FDIO_MAX_HANDLES as usize {
        return Err(zircon::Status::INVALID_ARGS);
    }

    let mut buf = bytes::BytesMut::with_capacity(ZXRIO_OBJECT_MINSIZE + extra.len());

    buf.put_i32::<bytes::LittleEndian>(status.into_raw());
    buf.put_u32::<bytes::LittleEndian>(type_);

    buf.put_slice(extra);

    chan.as_ref().write(buf.as_ref(), handles)
}

#[cfg(test)]
mod test {
    use super::*;
    use fdio_sys;
    use std;

    fn newmsg() -> zxrio_msg_t {
        let mut m = zxrio_msg_t {
            txid: 2,
            op: ZXRIO_OPEN,
            reserved0: 7,
            flags: 5,
            datalen: 1,
            arg: 3,
            arg2: zxrio_msg__bindgen_ty_1 { mode: fdio_sys::O_PIPELINE as u32 },
            reserved1: 4,
            hcount: 1,
            handle: [5, 0, 0, 0],
            data: [0; 8192],
        };
        m.data[0] = '.' as u8;
        m
    }

    impl Into<Vec<u8>> for zxrio_msg_t {
        fn into(self) -> Vec<u8> {
            unsafe {
                std::slice::from_raw_parts(
                    &self as *const _ as *const u8,
                    std::mem::size_of::<zxrio_msg_t>(),
                )
            }.to_vec()
        }
    }

    #[test]
    fn test_getters() {
        let buf = zircon::MessageBuf::new_with(newmsg().into(), vec![zircon::Handle::invalid()]);
        let m: Message = buf.into();
        assert_eq!(m.txid(), 2);
        assert_eq!(m.reserved0(), 7);
        assert_eq!(m.flags(), 5);
        assert_eq!(m.op(), ZXRIO_OPEN);
        assert_eq!(m.datalen(), 1);
        assert_eq!(m.arg(), 3);
        assert_eq!(m.reserved1(), 4);
        assert_eq!(m.hcount(), 1);
        assert_eq!(m.data().len(), m.datalen() as usize);
        assert_eq!(m.data(), &['.' as u8]);
    }

    #[test]
    fn test_arg2_getters() {
        let mut msg = newmsg();

        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);
        let m: Message = buf.into();
        assert_eq!(m.mode(), fdio_sys::O_PIPELINE as u32);

        msg.arg2.off = 10;
        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);

        let m: Message = buf.into();
        assert_eq!(m.off(), 10);

        msg.arg2.protocol = FDIO_PROTOCOL_SERVICE;
        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);

        let m: Message = buf.into();
        assert_eq!(m.protocol(), FDIO_PROTOCOL_SERVICE);

        msg.arg2.op = ZXRIO_CLOSE;
        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);

        let m: Message = buf.into();
        assert_eq!(m.arg2op(), ZXRIO_CLOSE);
    }

    #[test]
    fn test_opname() {
        let mut msg = newmsg();

        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);

        let m: Message = buf.into();
        assert_eq!(m.opname(), "open");

        msg.op = ZXRIO_CLOSE;
        msg.hcount = 0;
        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![]);

        let m: Message = buf.into();
        assert_eq!(m.opname(), "close");
    }

    #[test]
    fn test_valid() {
        let mut msg = newmsg();

        // this message is missing a required handle
        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![]);

        let m: Message = buf.into();
        assert!(m.validate().is_err());

        // this message is missing a byte from the data
        let mut v: Vec<u8> = msg.clone().into();
        let short = v.len() - 1;
        v.truncate(short);

        let buf = zircon::MessageBuf::new_with(v, vec![zircon::Handle::invalid()]);

        let m: Message = buf.into();
        assert!(m.validate().is_err());

        msg.datalen = 9999;
        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);
        let m: Message = buf.into();
        assert!(m.validate().is_err())
    }

    #[test]
    fn test_data_is_bounded() {
        let mut msg = newmsg();
        msg.datalen = 10_000;

        let buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![]);

        let m: Message = buf.into();
        assert_eq!(m.datalen(), 8192); // pinned to max of FDIO_CHUNK_SIZE
        // the message is corrupt, but bounded to buffer size, in this case a
        // whole zxrio_msg_t.
        assert_eq!(m.data().len(), 8192);

        // apply a similar assertion to above, but emulate a write that "forgot" the data segment
        let bytes: Vec<u8> = msg.clone().into();
        let buf = zircon::MessageBuf::new_with(Vec::<u8>::from(&bytes[0..ZXRIO_HDR_SZ]), vec![]);

        let m: Message = buf.into();
        assert_eq!(m.datalen(), 8192);
        assert_eq!(m.data().len(), 0); // uninitialized memory, i.e. corrupt message, but bounded.
    }

    #[test]
    fn test_write_object() {
        let (tx, rx) = zircon::Channel::create().unwrap();

        let h = zircon::Vmo::create(10).unwrap();

        write_object(
            &tx,
            zircon::Status::BAD_PATH,
            FDIO_PROTOCOL_REMOTE,
            &[1, 2, 3, 4],
            &mut vec![h.into()],
        ).unwrap();

        let mut buf = zircon::MessageBuf::new();

        rx.read(&mut buf).unwrap();

        let ptr = buf.bytes().as_ptr();
        assert_eq!(zircon::Status::BAD_PATH, zircon::Status::from_raw(unsafe {
            *(ptr as *const i32)
        }));
        assert_eq!(FDIO_PROTOCOL_REMOTE, unsafe {
            *(ptr.offset(4) as *const u32)
        });
        assert_eq!([1, 2, 3, 4], unsafe {
            ::std::slice::from_raw_parts(ptr.offset(8) as *const u8, 4)
        });
    }

    #[test]
    fn test_fdio_service_connect_and_serve() {
        use zircon::{AsHandleRef, DurationNum};

        let (client, server) = zircon::Channel::create().unwrap();

        std::thread::spawn(move || {
            let mut buf = zircon::MessageBuf::new();

            server.wait_handle(
                zircon::Signals::CHANNEL_READABLE|zircon::Signals::CHANNEL_PEER_CLOSED,
                10.seconds().after_now()).expect("server wait");
            server.read(&mut buf).expect("server channel read");

            let mut msg : Message = buf.into();

            assert_eq!(msg.op(), ZXRIO_OPEN);
            assert_eq!(msg.hcount(), 1);
            assert!(msg.is_pipelined());

            let svc_chan = zircon::Channel::from(msg.take_handle(0).expect("server take handle"));
            svc_chan.write(b"hello", &mut vec![]).expect("server channel write");
        });

        let (svc_cli, svc_conn) = zircon::Channel::create().expect("client service channel create");

        // TODO(raggi): create a safe wrapper for this function
        let status = unsafe { fdio_sys::fdio_service_connect_at(client.raw_handle(), ".".as_ptr() as *const _, svc_conn.raw_handle()) };
        zircon::Status::ok(status).expect("service connect at ok");
        let mut buf = zircon::MessageBuf::new();
        svc_cli.wait_handle(
            zircon::Signals::CHANNEL_READABLE|zircon::Signals::CHANNEL_PEER_CLOSED,
            10.seconds().after_now()).expect("client wait readable");
        svc_cli.read(&mut buf).expect("service client read");
        assert_eq!(b"hello", buf.bytes());
    }
}
