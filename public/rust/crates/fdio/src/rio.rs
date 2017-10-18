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

#[derive(Debug)]
pub enum MessageError {
  TooShort,

  BadDataSize,

  BadHandleCount,
}

/// Message, given a zircon::MessageBuf that has received a zxrio_msg_t in
/// buf.bytes, provides access to the values in the zxrio_msg_t.
pub struct Message<'a> {
  msg: &'a zxrio_msg_t,
}

impl<'a> Message<'a> {

  /// Create a message wrapper around the given buf, erroring if the buf contains
  /// an invalid message or is too short. Note that the zxrio_msg_t.handles field
  /// is ignored in validation, as users should only consume handles from buf
  /// directly, where the operation is safe.
  pub fn create(buf: &'a mut zircon::MessageBuf) -> Result<Self, MessageError> {
    buf.ensure_capacity_bytes(ZXRIO_MSG_SZ);

    if buf.bytes().len() < ZXRIO_HDR_SZ {
      return Err(MessageError::TooShort);
    }

    let msg: &'a zxrio_msg_t = unsafe { &*(buf.bytes().as_ptr() as *const _) };

    if buf.bytes().len() != ZXRIO_HDR_SZ + msg.datalen as usize {
      return Err(MessageError::BadDataSize);
    }

    if buf.n_handles() != ZXRIO_HC!(msg.op) as usize || buf.n_handles() != msg.hcount as usize {
      return Err(MessageError::BadHandleCount);
    }


    Ok(Self{msg: msg})
  }

  pub fn txid(&self) -> zx_txid_t {
    self.msg.txid
  }

  /// op returns the type of IO operation requested
  pub fn op(&self) -> c_uint {
    self.msg.op
  }

  pub fn datalen(&self) -> c_uint {
    self.msg.datalen
  }

  pub fn arg(&self) -> i32 {
    self.msg.arg
  }

  // NOTE: these arg2 union fields are always being extracted from the result of
  // a buffer read, meaning they have always been initialized.
  pub fn off(&self) -> i64 {
    unsafe { self.msg.arg2.off }
  }

  pub fn mode(&self) -> u32 {
    unsafe { self.msg.arg2.mode }
  }

  pub fn protocol(&self) -> u32 {
    unsafe { self.msg.arg2.protocol }
  }

  pub fn arg2op(&self) -> u32 {
    unsafe { self.msg.arg2.op }
  }

  pub fn reserved(&self) -> i32 {
    self.msg.reserved
  }

  pub fn hcount(&self) -> u32 {
    self.msg.hcount
  }

  pub fn opname(&self) -> &'static str {
    unsafe { ::std::ffi::CStr::from_ptr(fdio_opname(self.op())) }.to_str().unwrap()
  }

  pub fn data(&self) -> &'a [u8] {
    &self.msg.data[0..self.msg.datalen as usize]
  }
}

//TODO(raggi): implement Debug for Message

/// write_object writes a zxrio_object_t to the given channel, along with any given extra data and handles. extra must not exceed ZXRIO_OBJECT_EXTRA, and handles must not exceed FDIO_MAX_HANDLES.
pub fn write_object<C: AsRef<zircon::Channel>>(chan: C, status: zircon::Status, type_: u32, extra: &[u8], handles: &mut Vec<zircon::Handle>) -> Result<(), zircon::Status> {
  if extra.len() > ZXRIO_OBJECT_EXTRA as usize {
    return Err(zircon::Status::ErrInvalidArgs)
  }
  if handles.len() > FDIO_MAX_HANDLES as usize {
    return Err(zircon::Status::ErrInvalidArgs)
  }

  let mut buf = bytes::BytesMut::with_capacity(ZXRIO_OBJECT_MINSIZE + extra.len());

  buf.put_i32::<bytes::LittleEndian>(status as i32);
  buf.put_u32::<bytes::LittleEndian>(type_);

  buf.put_slice(extra);

  chan.as_ref().write(buf.as_ref(), handles, 0)
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
      datalen: 1,
      arg: 3,
      arg2: zxrio_msg__bindgen_ty_1 { mode: fdio_sys::O_PIPELINE as u32 },
      reserved: 4,
      hcount: 1,
      handle: [5, 0, 0, 0],
      data: [0; 8192],
    };
    m.data[0] = '.' as u8;
    m
  }

  impl Into<Vec<u8>> for zxrio_msg_t {
    fn into(self) -> Vec<u8> {
      unsafe { std::slice::from_raw_parts(&self as *const _ as *const u8, ZXRIO_HDR_SZ + self.datalen as usize) }.to_vec()
    }
  }

  #[test]
  fn test_getters() {
    let mut buf = zircon::MessageBuf::new_with(newmsg().into(), vec![zircon::Handle::invalid()]);
    let m = Message::create(&mut buf).unwrap();
    assert_eq!(m.txid(), 2);
    assert_eq!(m.op(), ZXRIO_OPEN);
    assert_eq!(m.datalen(), 1);
    assert_eq!(m.arg(), 3);
    assert_eq!(m.reserved(), 4);
    assert_eq!(m.hcount(), 1);
    assert_eq!(m.data().len(), m.datalen() as usize);
    assert_eq!(m.data(), &['.' as u8]);
  }

  #[test]
  fn test_arg2_getters() {
    let mut msg = newmsg();

    let mut buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);
    let m = Message::create(&mut buf).unwrap();
    assert_eq!(m.mode(), fdio_sys::O_PIPELINE as u32);

    msg.arg2.off = 10;
    let mut buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);
    let m = Message::create(&mut buf).unwrap();
    assert_eq!(m.off(), 10);

    msg.arg2.protocol = FDIO_PROTOCOL_SERVICE;
    let mut buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);
    let m = Message::create(&mut buf).unwrap();
    assert_eq!(m.protocol(), FDIO_PROTOCOL_SERVICE);

    msg.arg2.op = ZXRIO_CLOSE;
    let mut buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);
    let m = Message::create(&mut buf).unwrap();
    assert_eq!(m.arg2op(), ZXRIO_CLOSE);
  }

  #[test]
  fn test_opname() {
    let mut msg = newmsg();

    let mut buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![zircon::Handle::invalid()]);
    let m = Message::create(&mut buf).unwrap();
    assert_eq!(m.opname(), "open");

    msg.op = ZXRIO_CLOSE;
    msg.hcount = 0;
    let mut buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![]);
    let m = Message::create(&mut buf).unwrap();
    assert_eq!(m.opname(), "close");
  }

  #[test]
  fn test_valid() {
    let msg = newmsg();

    // this message is missing a required handle
    let mut buf = zircon::MessageBuf::new_with(msg.clone().into(), vec![]);
    let m = Message::create(&mut buf);
    assert!(m.is_err());

    // this message is missing a byte from the data
    let mut v: Vec<u8> = msg.clone().into();
    let short = v.len() - 1;
    v.truncate(short);

    let mut buf = zircon::MessageBuf::new_with(v, vec![zircon::Handle::invalid()]);
    let m = Message::create(&mut buf);
    assert!(m.is_err());
  }

  #[test]
  fn test_write_object() {
    let (tx, rx) = zircon::Channel::create(zircon::ChannelOpts::default()).unwrap();

    let h = zircon::Vmo::create(10, zircon::VmoOpts::default()).unwrap();

    write_object(&tx, zircon::Status::ErrBadPath, FDIO_PROTOCOL_REMOTE, &[1, 2, 3, 4], &mut vec![h.into()]).unwrap();

    let mut buf = zircon::MessageBuf::new();

    rx.read(0, &mut buf).unwrap();

    let ptr = buf.bytes().as_ptr();
    assert_eq!(zircon::Status::ErrBadPath as i32, unsafe { *(ptr as *const i32) });
    assert_eq!(FDIO_PROTOCOL_REMOTE, unsafe { *(ptr.offset(4) as *const u32) });
    assert_eq!([1,2,3,4], unsafe { ::std::slice::from_raw_parts(ptr.offset(8) as *const u8, 4) });
  }
}
