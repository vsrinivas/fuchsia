// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! fidlhdl_xxx implementation to provide handles for non-Fuchsia platforms
//! Declarations for this interface are found in //garnet/public/lib/fidl/rust/fidl/src/handle.rs

#![cfg(not(target_os = "fuchsia"))]

use {
    fidl::{
        FidlHdlPairCreateResult, FidlHdlReadResult, FidlHdlWriteResult, Handle, SocketOpts,
        INVALID_HANDLE,
    },
    parking_lot::Mutex,
    slab::Slab,
    std::collections::VecDeque,
    std::sync::Arc,
};

struct ChannelMessage {
    bytes: Vec<u8>,
    handles: Vec<fidl::Handle>,
}

struct HalfChannelState {
    messages: VecDeque<ChannelMessage>,
    need_read: bool,
}

impl HalfChannelState {
    fn new() -> HalfChannelState {
        HalfChannelState { messages: VecDeque::new(), need_read: false }
    }
}

enum ChannelState {
    Closed,
    Open(HalfChannelState, HalfChannelState),
}

struct HalfSocketState {
    bytes: VecDeque<u8>,
    need_read: bool,
}

impl HalfSocketState {
    fn new() -> HalfSocketState {
        HalfSocketState { bytes: VecDeque::new(), need_read: false }
    }
}

enum SocketState {
    Closed,
    Open(HalfSocketState, HalfSocketState),
}

enum FidlHandle {
    LeftChannel(Arc<Mutex<ChannelState>>, u32),
    RightChannel(Arc<Mutex<ChannelState>>, u32),
    LeftSocket(Arc<Mutex<SocketState>>, u32),
    RightSocket(Arc<Mutex<SocketState>>, u32),
}

lazy_static::lazy_static! {
    static ref HANDLES: Mutex<Slab<FidlHandle>> = Mutex::new(Slab::new());
}

fn with_handle<R>(hdl: u32, f: impl FnOnce(&mut FidlHandle) -> R) -> R {
    f(&mut HANDLES.lock()[hdl as usize])
}

/// Close the handle: no action if hdl==INVALID_HANDLE
#[no_mangle]
pub extern "C" fn fidlhdl_close(hdl: u32) {
    if hdl == INVALID_HANDLE {
        return;
    }
    let wakeup = match HANDLES.lock().remove(hdl as usize) {
        FidlHandle::LeftChannel(cs, peer) => {
            let st = &mut *cs.lock();
            let wakeup = match st {
                ChannelState::Closed => false,
                ChannelState::Open(_, st) => st.need_read,
            };
            *st = ChannelState::Closed;
            if wakeup {
                peer
            } else {
                INVALID_HANDLE
            }
        }
        FidlHandle::RightChannel(cs, peer) => {
            let st = &mut *cs.lock();
            let wakeup = match st {
                ChannelState::Closed => false,
                ChannelState::Open(st, _) => st.need_read,
            };
            *st = ChannelState::Closed;
            if wakeup {
                peer
            } else {
                INVALID_HANDLE
            }
        }
        FidlHandle::LeftSocket(cs, peer) => {
            let st = &mut *cs.lock();
            let wakeup = match st {
                SocketState::Closed => false,
                SocketState::Open(_, st) => st.need_read,
            };
            *st = SocketState::Closed;
            if wakeup {
                peer
            } else {
                INVALID_HANDLE
            }
        }
        FidlHandle::RightSocket(cs, peer) => {
            let st = &mut *cs.lock();
            let wakeup = match st {
                SocketState::Closed => false,
                SocketState::Open(st, _) => st.need_read,
            };
            *st = SocketState::Closed;
            if wakeup {
                peer
            } else {
                INVALID_HANDLE
            }
        }
    };
    if wakeup != INVALID_HANDLE {
        fidl::awaken_hdl(wakeup);
    }
}

/// Create a channel pair
#[no_mangle]
pub extern "C" fn fidlhdl_channel_create() -> FidlHdlPairCreateResult {
    let cs =
        Arc::new(Mutex::new(ChannelState::Open(HalfChannelState::new(), HalfChannelState::new())));
    let mut h = HANDLES.lock();
    let left = h.insert(FidlHandle::LeftChannel(cs.clone(), INVALID_HANDLE)) as u32;
    let right = h.insert(FidlHandle::RightChannel(cs, left)) as u32;
    if let FidlHandle::LeftChannel(_, peer) = &mut h[left as usize] {
        *peer = right;
    } else {
        unreachable!();
    }
    FidlHdlPairCreateResult { left, right }
}

unsafe fn complete_channel_read(
    st: &mut HalfChannelState,
    bytes: *mut u8,
    mut handles: *mut u32,
    num_bytes: usize,
    num_handles: usize,
    actual_bytes: *mut usize,
    actual_handles: *mut usize,
) -> FidlHdlReadResult {
    if let Some(mut msg) = st.messages.pop_front() {
        let msg_bytes = msg.bytes.len();
        let msg_handles = msg.handles.len();
        *actual_bytes = msg_bytes;
        *actual_handles = msg_handles;
        if num_bytes >= msg.bytes.len() && num_handles >= msg.handles.len() {
            std::ptr::copy_nonoverlapping(msg.bytes.as_ptr(), bytes, num_bytes);
            for h in msg.handles.iter_mut() {
                *handles = h.raw_take();
                handles = handles.offset(1);
            }
            FidlHdlReadResult::Ok
        } else {
            st.messages.push_front(msg);
            FidlHdlReadResult::BufferTooSmall
        }
    } else {
        FidlHdlReadResult::Pending
    }
}

/// Read from a channel - takes ownership of all handles
#[no_mangle]
pub unsafe extern "C" fn fidlhdl_channel_read(
    hdl: u32,
    bytes: *mut u8,
    handles: *mut u32,
    num_bytes: usize,
    num_handles: usize,
    actual_bytes: *mut usize,
    actual_handles: *mut usize,
) -> FidlHdlReadResult {
    with_handle(hdl, |obj| match obj {
        FidlHandle::LeftChannel(cs, _) => match *cs.lock() {
            ChannelState::Closed => FidlHdlReadResult::PeerClosed,
            ChannelState::Open(ref mut st, _) => complete_channel_read(
                st,
                bytes,
                handles,
                num_bytes,
                num_handles,
                actual_bytes,
                actual_handles,
            ),
        },
        FidlHandle::RightChannel(cs, _) => match *cs.lock() {
            ChannelState::Closed => FidlHdlReadResult::PeerClosed,
            ChannelState::Open(_, ref mut st) => complete_channel_read(
                st,
                bytes,
                handles,
                num_bytes,
                num_handles,
                actual_bytes,
                actual_handles,
            ),
        },
        _ => panic!("Non channel passed to channel_read"),
    })
}

fn complete_channel_write(
    st: &mut HalfChannelState,
    peer: u32,
    bytes: &[u8],
    handles: &mut [Handle],
) -> (FidlHdlWriteResult, u32) {
    let mut b = Vec::new();
    b.extend_from_slice(bytes);
    st.messages.push_back(ChannelMessage {
        bytes: b,
        handles: handles.into_iter().map(|h| h.take()).collect(),
    });
    let wakeup = st.need_read;
    st.need_read = false;
    (FidlHdlWriteResult::Ok, if wakeup { peer } else { INVALID_HANDLE })
}

fn channel_write(hdl: u32, bytes: &[u8], handles: &mut [Handle]) -> FidlHdlWriteResult {
    let (result, wakeup) = with_handle(hdl, |obj| match obj {
        FidlHandle::LeftChannel(cs, peer) => match *cs.lock() {
            ChannelState::Closed => (FidlHdlWriteResult::PeerClosed, INVALID_HANDLE),
            ChannelState::Open(_, ref mut st) => complete_channel_write(st, *peer, bytes, handles),
        },
        FidlHandle::RightChannel(cs, peer) => match *cs.lock() {
            ChannelState::Closed => (FidlHdlWriteResult::PeerClosed, INVALID_HANDLE),
            ChannelState::Open(ref mut st, _) => complete_channel_write(st, *peer, bytes, handles),
        },
        _ => panic!("Non channel passed to channel_write"),
    });
    if wakeup != INVALID_HANDLE {
        fidl::awaken_hdl(wakeup);
    }
    result
}

#[no_mangle]
pub unsafe extern "C" fn fidlhdl_channel_write(
    hdl: u32,
    bytes: *const u8,
    handles: *mut Handle,
    num_bytes: usize,
    num_handles: usize,
) -> FidlHdlWriteResult {
    channel_write(
        hdl,
        std::slice::from_raw_parts(bytes, num_bytes),
        std::slice::from_raw_parts_mut(handles, num_handles),
    )
}

unsafe fn complete_socket_read(
    st: &mut HalfSocketState,
    mut bytes: *mut u8,
    num_bytes: usize,
    actual_bytes: *mut usize,
) -> FidlHdlReadResult {
    if num_bytes == 0 {
        return FidlHdlReadResult::Ok;
    }
    let copy_bytes = std::cmp::min(num_bytes, st.bytes.len());
    if copy_bytes == 0 {
        return FidlHdlReadResult::Pending;
    }
    for b in st.bytes.drain(..copy_bytes) {
        *bytes = b;
        bytes = bytes.add(1);
    }
    *actual_bytes = copy_bytes;
    FidlHdlReadResult::Ok
}

/// Read from a socket
#[no_mangle]
pub unsafe extern "C" fn fidlhdl_socket_read(
    hdl: u32,
    bytes: *mut u8,
    num_bytes: usize,
    actual_bytes: *mut usize,
) -> FidlHdlReadResult {
    with_handle(hdl, |obj| match obj {
        FidlHandle::LeftSocket(cs, _) => match *cs.lock() {
            SocketState::Closed => FidlHdlReadResult::PeerClosed,
            SocketState::Open(ref mut st, _) => {
                complete_socket_read(st, bytes, num_bytes, actual_bytes)
            }
        },
        FidlHandle::RightSocket(cs, _) => match *cs.lock() {
            SocketState::Closed => FidlHdlReadResult::PeerClosed,
            SocketState::Open(_, ref mut st) => {
                complete_socket_read(st, bytes, num_bytes, actual_bytes)
            }
        },
        _ => panic!("Non socket passed to socket_read"),
    })
}

unsafe fn complete_socket_write(
    st: &mut HalfSocketState,
    peer: u32,
    bytes: *const u8,
    num_bytes: usize,
) -> (FidlHdlWriteResult, u32) {
    if num_bytes > 0 {
        st.bytes.extend(std::slice::from_raw_parts(bytes, num_bytes));
        let wakeup = st.need_read;
        st.need_read = false;
        (FidlHdlWriteResult::Ok, if wakeup { peer } else { INVALID_HANDLE })
    } else {
        (FidlHdlWriteResult::Ok, INVALID_HANDLE)
    }
}

/// Write to a socket
#[no_mangle]
pub unsafe extern "C" fn fidlhdl_socket_write(
    hdl: u32,
    bytes: *const u8,
    num_bytes: usize,
) -> FidlHdlWriteResult {
    let (result, wakeup) = with_handle(hdl, |obj| match obj {
        FidlHandle::LeftSocket(cs, peer) => match *cs.lock() {
            SocketState::Closed => (FidlHdlWriteResult::PeerClosed, INVALID_HANDLE),
            SocketState::Open(_, ref mut st) => complete_socket_write(st, *peer, bytes, num_bytes),
        },
        FidlHandle::RightSocket(cs, peer) => match *cs.lock() {
            SocketState::Closed => (FidlHdlWriteResult::PeerClosed, INVALID_HANDLE),
            SocketState::Open(ref mut st, _) => complete_socket_write(st, *peer, bytes, num_bytes),
        },
        _ => panic!("Non socket passed to socket_write"),
    });
    if wakeup != INVALID_HANDLE {
        fidl::awaken_hdl(wakeup);
    }
    result
}

/// Create a socket pair
#[no_mangle]
pub unsafe extern "C" fn fidlhdl_socket_create(sock_opts: SocketOpts) -> FidlHdlPairCreateResult {
    // TODO: This method currently only works for stream type sockets... rectify this at some point
    // (this provides a compile time assert to that fact)
    match sock_opts {
        SocketOpts::STREAM => (),
    };
    let cs =
        Arc::new(Mutex::new(SocketState::Open(HalfSocketState::new(), HalfSocketState::new())));
    let mut h = HANDLES.lock();
    let left = h.insert(FidlHandle::LeftSocket(cs.clone(), INVALID_HANDLE)) as u32;
    let right = h.insert(FidlHandle::RightSocket(cs, left)) as u32;
    if let FidlHandle::LeftSocket(_, peer) = &mut h[left as usize] {
        *peer = right;
    } else {
        unreachable!();
    }
    FidlHdlPairCreateResult { left, right }
}

/// Signal that a read is required
#[no_mangle]
pub extern "C" fn fidlhdl_need_read(hdl: u32) {
    let wakeup = with_handle(hdl, |obj| match obj {
        FidlHandle::LeftChannel(cs, _) => match *cs.lock() {
            ChannelState::Closed => true,
            ChannelState::Open(ref mut st, _) => {
                if st.messages.is_empty() {
                    st.need_read = true;
                    false
                } else {
                    true
                }
            }
        },
        FidlHandle::RightChannel(cs, _) => match *cs.lock() {
            ChannelState::Closed => true,
            ChannelState::Open(_, ref mut st) => {
                if st.messages.is_empty() {
                    st.need_read = true;
                    false
                } else {
                    true
                }
            }
        },
        FidlHandle::LeftSocket(cs, _) => match *cs.lock() {
            SocketState::Closed => true,
            SocketState::Open(ref mut st, _) => {
                if st.bytes.is_empty() {
                    st.need_read = true;
                    false
                } else {
                    true
                }
            }
        },
        FidlHandle::RightSocket(cs, _) => match *cs.lock() {
            SocketState::Closed => true,
            SocketState::Open(_, ref mut st) => {
                if st.bytes.is_empty() {
                    st.need_read = true;
                    false
                } else {
                    true
                }
            }
        },
    });
    if wakeup {
        fidl::awaken_hdl(hdl);
    }
}

#[no_mangle]
pub extern "C" fn fidlhdl_type(hdl: u32) -> fidl::FidlHdlType {
    with_handle(hdl, |obj| match obj {
        FidlHandle::LeftChannel(_, _) => fidl::FidlHdlType::Channel,
        FidlHandle::RightChannel(_, _) => fidl::FidlHdlType::Channel,
        FidlHandle::LeftSocket(_, _) => fidl::FidlHdlType::Channel,
        FidlHandle::RightSocket(_, _) => fidl::FidlHdlType::Channel,
    })
}

#[cfg(test)]
mod test {

    use fuchsia_zircon_status as zx_status;
    use futures::executor::block_on;
    use futures::io::{AsyncReadExt, AsyncWriteExt};
    use futures::task::{noop_waker, Context};
    use futures::Future;
    use std::pin::Pin;

    #[test]
    fn channel_write_read() {
        let (a, b) = fidl::Channel::create().unwrap();
        let (c, d) = fidl::Channel::create().unwrap();
        let mut incoming = fidl::MessageBuf::new();

        assert_eq!(b.read(&mut incoming).err().unwrap(), zx_status::Status::SHOULD_WAIT);
        d.write(&[4, 5, 6], &mut vec![]).unwrap();
        a.write(&[1, 2, 3], &mut vec![c.into(), d.into()]).unwrap();

        b.read(&mut incoming).unwrap();
        assert_eq!(incoming.bytes(), &[1, 2, 3]);
        assert_eq!(incoming.n_handles(), 2);
        let c: fidl::Channel = incoming.take_handle(0).unwrap().into();
        let d: fidl::Channel = incoming.take_handle(1).unwrap().into();
        c.read(&mut incoming).unwrap();
        drop(d);
        assert_eq!(incoming.bytes(), &[4, 5, 6]);
        assert_eq!(incoming.n_handles(), 0);
    }

    #[test]
    fn socket_write_read() {
        let (a, b) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
        a.write(&[1, 2, 3]).unwrap();
        let mut buf = [0u8; 128];
        assert_eq!(b.read(&mut buf).unwrap(), 3);
        assert_eq!(&buf[0..3], &[1, 2, 3]);
    }

    #[test]
    fn async_channel_write_read() {
        let (a, b) = fidl::Channel::create().unwrap();
        let (a, b) = (
            fidl::AsyncChannel::from_channel(a).unwrap(),
            fidl::AsyncChannel::from_channel(b).unwrap(),
        );
        let mut buf = fidl::MessageBuf::new();

        let waker = noop_waker();
        let mut cx = Context::from_waker(&waker);

        let mut rx = b.recv_msg(&mut buf);
        assert_eq!(Pin::new(&mut rx).poll(&mut cx), futures::Poll::Pending);
        a.write(&[1, 2, 3], &mut vec![]).unwrap();
        block_on(Pin::new(&mut rx)).unwrap();
        assert_eq!(buf.bytes(), &[1, 2, 3]);

        let mut rx = a.recv_msg(&mut buf);
        assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
        b.write(&[1, 2, 3], &mut vec![]).unwrap();
        block_on(Pin::new(&mut rx)).unwrap();
        assert_eq!(buf.bytes(), &[1, 2, 3]);
    }

    #[test]
    fn async_socket_write_read() {
        let (a, b) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
        let (mut a, mut b) = (
            fidl::AsyncSocket::from_socket(a).unwrap(),
            fidl::AsyncSocket::from_socket(b).unwrap(),
        );
        let mut buf = [0u8; 128];

        let waker = noop_waker();
        let mut cx = Context::from_waker(&waker);

        let mut rx = b.read(&mut buf);
        assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
        assert!(Pin::new(&mut a.write(&[1, 2, 3])).poll(&mut cx).is_ready());
        block_on(Pin::new(&mut rx)).unwrap();
        assert_eq!(&buf[0..3], &[1, 2, 3]);

        let mut rx = a.read(&mut buf);
        assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
        assert!(Pin::new(&mut b.write(&[1, 2, 3])).poll(&mut cx).is_ready());
        block_on(Pin::new(&mut rx)).unwrap();
        assert_eq!(&buf[0..3], &[1, 2, 3]);
    }
}
