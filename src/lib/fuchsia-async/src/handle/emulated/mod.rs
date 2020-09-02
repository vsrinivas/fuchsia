// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Emulation of Zircon handles for non-Zircon based operating systems.

pub mod channel;
pub mod socket;

use fuchsia_zircon_status as zx_status;
use futures::task::noop_waker_ref;
use slab::Slab;
use std::sync::Mutex;
use std::{
    collections::VecDeque,
    sync::atomic::{AtomicU64, AtomicUsize, Ordering},
    task::{Context, Poll, Waker},
};

/// Invalid handle value
const INVALID_HANDLE: u32 = 0xffff_ffff;

/// The type of a handle
#[derive(Debug, PartialEq)]
pub enum HandleType {
    /// An invalid handle
    Invalid,
    /// A channel
    Channel,
    /// A socket
    Socket,
}

/// A borrowed reference to an underlying handle
pub struct HandleRef<'a>(u32, std::marker::PhantomData<&'a u32>);

impl<'a> HandleRef<'a> {
    /// Returns an invalid handle ref
    pub fn invalid() -> Self {
        Self(INVALID_HANDLE, std::marker::PhantomData)
    }
}

/// A trait to get a reference to the underlying handle of an object.
pub trait AsHandleRef {
    /// Get a reference to the handle.
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a>;

    /// Return true if this handle is invalid
    fn is_invalid(&self) -> bool {
        self.as_handle_ref().0 == INVALID_HANDLE
    }

    /// Interpret the reference as a raw handle (an integer type). Two distinct
    /// handles will have different raw values (so it can perhaps be used as a
    /// key in a data structure).
    fn raw_handle(&self) -> u32 {
        self.as_handle_ref().0
    }
}

/// An extension of `AsHandleRef` that adds non-Fuchsia-only operations.
pub trait EmulatedHandleRef: AsHandleRef {
    /// Return the type of a handle.
    fn handle_type(&self) -> HandleType {
        if self.is_invalid() {
            HandleType::Invalid
        } else {
            let (_, _, ty, _) = unpack_handle(self.as_handle_ref().0);
            match ty {
                HdlType::Channel => HandleType::Channel,
                HdlType::StreamSocket => HandleType::Socket,
                HdlType::DatagramSocket => HandleType::Socket,
            }
        }
    }

    /// Return a reference to the other end of a handle.
    fn related<'a>(&'a self) -> HandleRef<'a> {
        if self.is_invalid() {
            HandleRef(INVALID_HANDLE, std::marker::PhantomData)
        } else {
            HandleRef(self.as_handle_ref().0 ^ 1, std::marker::PhantomData)
        }
    }

    /// Return a "koid" like value.
    fn koid_pair(&self) -> (u64, u64) {
        if self.is_invalid() {
            (0, 0)
        } else {
            with_handle(self.as_handle_ref().0, |h, side| {
                let koid_left = match h {
                    HdlRef::Channel(h) => h.koid_left,
                    HdlRef::StreamSocket(h) => h.koid_left,
                    HdlRef::DatagramSocket(h) => h.koid_left,
                };
                match side {
                    Side::Left => (koid_left, koid_left + 1),
                    Side::Right => (koid_left + 1, koid_left),
                }
            })
        }
    }

    /// Return true if the handle appears valid (i.e. not `Handle::invalid()`),
    /// but does not exist in the table. This should not normally return true;
    /// when it does, dropping the handle will cause a panic.
    fn is_dangling(&self) -> bool {
        if self.is_invalid() {
            false
        } else {
            let (shard, slot, ty, side) = unpack_handle(self.as_handle_ref().0);
            let present = match ty {
                HdlType::Channel => table_contains(&CHANNELS, shard, slot, side),
                HdlType::StreamSocket => table_contains(&STREAM_SOCKETS, shard, slot, side),
                HdlType::DatagramSocket => table_contains(&DATAGRAM_SOCKETS, shard, slot, side),
            };
            !present
        }
    }
}

impl<T: AsHandleRef> EmulatedHandleRef for T {}

impl AsHandleRef for HandleRef<'_> {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

/// A trait implemented by all handle-based types.
pub trait HandleBased: AsHandleRef + From<Handle> + Into<Handle> {
    /// Creates an instance of this type from a handle.
    ///
    /// This is a convenience function which simply forwards to the `From` trait.
    fn from_handle(handle: Handle) -> Self {
        Self::from(handle)
    }

    /// Converts the value into its inner handle.
    ///
    /// This is a convenience function which simply forwards to the `Into` trait.
    fn into_handle(self) -> Handle {
        self.into()
    }
}

/// Representation of a handle-like object
#[derive(PartialEq, Eq, Debug, Ord, PartialOrd, Hash)]
pub struct Handle(u32);

impl Drop for Handle {
    fn drop(&mut self) {
        hdl_close(self.0);
    }
}

impl AsHandleRef for Handle {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

impl HandleBased for Handle {}

impl Handle {
    /// Return an invalid handle
    pub const fn invalid() -> Handle {
        Handle(INVALID_HANDLE)
    }

    /// If a raw handle is obtained from some other source, this method converts
    /// it into a type-safe owned handle.
    pub unsafe fn from_raw(hdl: u32) -> Handle {
        Handle(hdl)
    }

    /// Take this handle and return a new handle (leaves this handle invalid)
    pub fn take(&mut self) -> Handle {
        let h = Handle(self.0);
        self.0 = INVALID_HANDLE;
        h
    }

    /// Take this handle and return a new raw handle (leaves this handle invalid)
    pub unsafe fn raw_take(&mut self) -> u32 {
        let h = self.0;
        self.0 = INVALID_HANDLE;
        h
    }
}

/// An emulated Zircon Channel
#[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
pub struct Channel(u32);

impl From<Handle> for Channel {
    fn from(mut hdl: Handle) -> Channel {
        let out = Channel(hdl.0);
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl From<Channel> for Handle {
    fn from(mut hdl: Channel) -> Handle {
        let out = unsafe { Handle::from_raw(hdl.0) };
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl HandleBased for Channel {}
impl AsHandleRef for Channel {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

impl Drop for Channel {
    fn drop(&mut self) {
        hdl_close(self.0);
    }
}

impl Channel {
    /// Create a channel, resulting in a pair of `Channel` objects representing both
    /// sides of the channel. Messages written into one maybe read from the opposite.
    pub fn create() -> Result<(Channel, Channel), zx_status::Status> {
        let (shard, slot) = CHANNELS.new_handle_slot();
        let left = pack_handle(shard, slot, HdlType::Channel, Side::Left);
        let right = pack_handle(shard, slot, HdlType::Channel, Side::Right);
        Ok((Channel(left), Channel(right)))
    }

    /// Read a message from a channel.
    pub fn read(&self, buf: &mut MessageBuf) -> Result<(), zx_status::Status> {
        let (bytes, handles) = buf.split_mut();
        self.read_split(bytes, handles)
    }

    /// Read a message from a channel into a separate byte vector and handle vector.
    pub fn read_split(
        &self,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<Handle>,
    ) -> Result<(), zx_status::Status> {
        match self.poll_read(&mut Context::from_waker(noop_waker_ref()), bytes, handles) {
            Poll::Ready(r) => r,
            Poll::Pending => Err(zx_status::Status::SHOULD_WAIT),
        }
    }

    fn poll_read(
        &self,
        cx: &mut Context<'_>,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<Handle>,
    ) -> Poll<Result<(), zx_status::Status>> {
        with_handle(self.0, |h, side| {
            if let HdlRef::Channel(obj) = h {
                if let Some(mut msg) = obj.q.side_mut(side.opposite()).pop_front() {
                    std::mem::swap(bytes, &mut msg.bytes);
                    std::mem::swap(handles, &mut msg.handles);
                    Poll::Ready(Ok(()))
                } else if obj.liveness.is_open() {
                    *obj.wakers.side_mut(side.opposite()) = Some(cx.waker().clone());
                    Poll::Pending
                } else {
                    Poll::Ready(Err(zx_status::Status::PEER_CLOSED))
                }
            } else {
                unreachable!();
            }
        })
    }

    /// Write a message to a channel.
    pub fn write(&self, bytes: &[u8], handles: &mut Vec<Handle>) -> Result<(), zx_status::Status> {
        let bytes = bytes.to_vec();
        with_handle(self.0, |h, side| {
            if let HdlRef::Channel(obj) = h {
                if !obj.liveness.is_open() {
                    return Err(zx_status::Status::PEER_CLOSED);
                }
                obj.q.side_mut(side).push_back(ChannelMessage {
                    bytes,
                    handles: std::mem::replace(handles, Vec::new()),
                });
                obj.wakers.side_mut(side).take().map(|w| w.wake());
                Ok(())
            } else {
                unreachable!();
            }
        })
    }
}

/// Socket options available portable
#[derive(Clone, Copy)]
pub enum SocketOpts {
    /// A bytestream style socket
    STREAM,
    /// A datagram style socket
    DATAGRAM,
}

/// Emulation of a Zircon Socket.
#[derive(PartialEq, Eq, Debug, PartialOrd, Ord, Hash)]
pub struct Socket(u32);

impl From<Handle> for Socket {
    fn from(mut hdl: Handle) -> Socket {
        let out = Socket(hdl.0);
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl From<Socket> for Handle {
    fn from(mut hdl: Socket) -> Handle {
        let out = unsafe { Handle::from_raw(hdl.0) };
        hdl.0 = INVALID_HANDLE;
        out
    }
}
impl HandleBased for Socket {}
impl AsHandleRef for Socket {
    fn as_handle_ref<'a>(&'a self) -> HandleRef<'a> {
        HandleRef(self.0, std::marker::PhantomData)
    }
}

impl Drop for Socket {
    fn drop(&mut self) {
        hdl_close(self.0);
    }
}

impl Socket {
    /// Create a pair of sockets
    pub fn create(sock_opts: SocketOpts) -> Result<(Socket, Socket), zx_status::Status> {
        match sock_opts {
            SocketOpts::STREAM => {
                let (shard, slot) = STREAM_SOCKETS.new_handle_slot();
                let left = pack_handle(shard, slot, HdlType::StreamSocket, Side::Left);
                let right = pack_handle(shard, slot, HdlType::StreamSocket, Side::Right);
                Ok((Socket(left), Socket(right)))
            }
            SocketOpts::DATAGRAM => {
                let (shard, slot) = DATAGRAM_SOCKETS.new_handle_slot();
                let left = pack_handle(shard, slot, HdlType::DatagramSocket, Side::Left);
                let right = pack_handle(shard, slot, HdlType::DatagramSocket, Side::Right);
                Ok((Socket(left), Socket(right)))
            }
        }
    }

    /// Write the given bytes into the socket.
    /// Return value (on success) is number of bytes actually written.
    pub fn write(&self, bytes: &[u8]) -> Result<usize, zx_status::Status> {
        with_handle(self.0, |h, side| {
            match h {
                HdlRef::StreamSocket(obj) => {
                    if !obj.liveness.is_open() {
                        return Err(zx_status::Status::PEER_CLOSED);
                    }
                    obj.q.side_mut(side).extend(bytes);
                    obj.wakers.side_mut(side).take().map(|w| w.wake());
                }
                HdlRef::DatagramSocket(obj) => {
                    if !obj.liveness.is_open() {
                        return Err(zx_status::Status::PEER_CLOSED);
                    }
                    obj.q.side_mut(side).push_back(bytes.to_vec());
                    obj.wakers.side_mut(side).take().map(|w| w.wake());
                }
                _ => panic!("Non socket passed to Socket::write"),
            }
            Ok(bytes.len())
        })
    }

    /// Return how many bytes are buffered in the socket
    pub fn outstanding_read_bytes(&self) -> Result<usize, zx_status::Status> {
        let (len, open) = with_handle(self.0, |h, side| match h {
            HdlRef::StreamSocket(obj) => {
                (obj.q.side(side.opposite()).len(), obj.liveness.is_open())
            }
            HdlRef::DatagramSocket(obj) => (
                obj.q.side(side.opposite()).front().map(|frame| frame.len()).unwrap_or(0),
                obj.liveness.is_open(),
            ),
            _ => panic!("Non socket passed to Socket::outstanding_read_bytes"),
        });
        if len > 0 {
            return Ok(len);
        }
        if !open {
            return Err(zx_status::Status::PEER_CLOSED);
        }
        Ok(0)
    }

    fn poll_read(
        &self,
        bytes: &mut [u8],
        ctx: &mut Context<'_>,
    ) -> Poll<Result<usize, zx_status::Status>> {
        with_handle(self.0, |h, side| match h {
            HdlRef::StreamSocket(obj) => {
                if bytes.is_empty() {
                    if obj.liveness.is_open() {
                        return Poll::Ready(Ok(0));
                    } else {
                        return Poll::Ready(Err(zx_status::Status::PEER_CLOSED));
                    }
                }
                let read = obj.q.side_mut(side.opposite());
                let copy_bytes = std::cmp::min(bytes.len(), read.len());
                if copy_bytes == 0 {
                    if obj.liveness.is_open() {
                        *obj.wakers.side_mut(side.opposite()) = Some(ctx.waker().clone());
                        return Poll::Pending;
                    } else {
                        return Poll::Ready(Err(zx_status::Status::PEER_CLOSED));
                    }
                }
                for (i, b) in read.drain(..copy_bytes).enumerate() {
                    bytes[i] = b;
                }
                Poll::Ready(Ok(copy_bytes))
            }
            HdlRef::DatagramSocket(obj) => {
                if let Some(frame) = obj.q.side_mut(side.opposite()).pop_front() {
                    let n = std::cmp::min(bytes.len(), frame.len());
                    bytes[..n].clone_from_slice(&frame[..n]);
                    Poll::Ready(Ok(n))
                } else if !obj.liveness.is_open() {
                    Poll::Ready(Err(zx_status::Status::PEER_CLOSED))
                } else {
                    *obj.wakers.side_mut(side.opposite()) = Some(ctx.waker().clone());
                    Poll::Pending
                }
            }
            _ => panic!("Non socket passed to Socket::read"),
        })
    }

    /// Read bytes from the socket.
    /// Return value (on success) is number of bytes actually read.
    pub fn read(&self, bytes: &mut [u8]) -> Result<usize, zx_status::Status> {
        match self.poll_read(bytes, &mut Context::from_waker(noop_waker_ref())) {
            Poll::Ready(r) => r,
            Poll::Pending => Err(zx_status::Status::SHOULD_WAIT),
        }
    }
}

/// A buffer for _receiving_ messages from a channel.
///
/// A `MessageBuf` is essentially a byte buffer and a vector of
/// handles, but move semantics for "taking" handles requires special handling.
///
/// Note that for sending messages to a channel, the caller manages the buffers,
/// using a plain byte slice and `Vec<Handle>`.
#[derive(Debug, Default)]
pub struct MessageBuf {
    bytes: Vec<u8>,
    handles: Vec<Handle>,
}

impl MessageBuf {
    /// Create a new, empty, message buffer.
    pub fn new() -> Self {
        Default::default()
    }

    /// Create a new non-empty message buffer.
    pub fn new_with(v: Vec<u8>, h: Vec<Handle>) -> Self {
        Self { bytes: v, handles: h }
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handles.
    pub fn split_mut(&mut self) -> (&mut Vec<u8>, &mut Vec<Handle>) {
        (&mut self.bytes, &mut self.handles)
    }

    /// Splits apart the message buf into a vector of bytes and a vector of handles.
    pub fn split(self) -> (Vec<u8>, Vec<Handle>) {
        (self.bytes, self.handles)
    }

    /// Ensure that the buffer has the capacity to hold at least `n_bytes` bytes.
    pub fn ensure_capacity_bytes(&mut self, n_bytes: usize) {
        ensure_capacity(&mut self.bytes, n_bytes);
    }

    /// Ensure that the buffer has the capacity to hold at least `n_handles` handles.
    pub fn ensure_capacity_handles(&mut self, n_handles: usize) {
        ensure_capacity(&mut self.handles, n_handles);
    }

    /// Ensure that at least n_bytes bytes are initialized (0 fill).
    pub fn ensure_initialized_bytes(&mut self, n_bytes: usize) {
        if n_bytes <= self.bytes.len() {
            return;
        }
        self.bytes.resize(n_bytes, 0);
    }

    /// Get a reference to the bytes of the message buffer, as a `&[u8]` slice.
    pub fn bytes(&self) -> &[u8] {
        self.bytes.as_slice()
    }

    /// The number of handles in the message buffer. Note this counts the number
    /// available when the message was received; `take_handle` does not affect
    /// the count.
    pub fn n_handles(&self) -> usize {
        self.handles.len()
    }

    /// Take the handle at the specified index from the message buffer. If the
    /// method is called again with the same index, it will return `None`, as
    /// will happen if the index exceeds the number of handles available.
    pub fn take_handle(&mut self, index: usize) -> Option<Handle> {
        self.handles.get_mut(index).and_then(|handle| {
            if handle.is_invalid() {
                None
            } else {
                Some(std::mem::replace(handle, Handle::invalid()))
            }
        })
    }

    /// Clear the bytes and handles contained in the buf. This will drop any
    /// contained handles, resulting in their resources being freed.
    pub fn clear(&mut self) {
        self.bytes.clear();
        self.handles.clear();
    }
}

fn ensure_capacity<T>(vec: &mut Vec<T>, size: usize) {
    let len = vec.len();
    if size > len {
        vec.reserve(size - len);
    }
}

#[derive(Default)]
struct Sided<T> {
    left: T,
    right: T,
}

impl<T> Sided<T> {
    fn side_mut(&mut self, side: Side) -> &mut T {
        match side {
            Side::Left => &mut self.left,
            Side::Right => &mut self.right,
        }
    }

    fn side(&self, side: Side) -> &T {
        match side {
            Side::Left => &self.left,
            Side::Right => &self.right,
        }
    }
}

struct ChannelMessage {
    bytes: Vec<u8>,
    handles: Vec<Handle>,
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum HdlType {
    Channel,
    StreamSocket,
    DatagramSocket,
}

#[derive(Clone, Copy, Debug, PartialEq)]
enum Side {
    Left,
    Right,
}

impl Side {
    fn opposite(self) -> Side {
        match self {
            Side::Left => Side::Right,
            Side::Right => Side::Left,
        }
    }
}

#[derive(Clone, Copy, Debug)]
enum Liveness {
    Open,
    Left,
    Right,
}

impl Liveness {
    fn close(self, side: Side) -> Option<Liveness> {
        match (self, side) {
            (Liveness::Open, Side::Left) => Some(Liveness::Right),
            (Liveness::Open, Side::Right) => Some(Liveness::Left),
            (Liveness::Left, Side::Right) => unreachable!(),
            (Liveness::Right, Side::Left) => unreachable!(),
            (Liveness::Left, Side::Left) => None,
            (Liveness::Right, Side::Right) => None,
        }
    }

    fn is_open(self) -> bool {
        match self {
            Liveness::Open => true,
            _ => false,
        }
    }
}

type Wakers = Sided<Option<Waker>>;

struct Hdl<T> {
    q: Sided<VecDeque<T>>,
    wakers: Wakers,
    liveness: Liveness,
    koid_left: u64,
}

enum HdlRef<'a> {
    Channel(&'a mut Hdl<ChannelMessage>),
    StreamSocket(&'a mut Hdl<u8>),
    DatagramSocket(&'a mut Hdl<Vec<u8>>),
}

fn with_handle<R>(handle: u32, f: impl FnOnce(HdlRef<'_>, Side) -> R) -> R {
    let (shard, slot, ty, side) = unpack_handle(handle);
    match ty {
        HdlType::Channel => {
            f(HdlRef::Channel(&mut CHANNELS.shards[shard].lock().unwrap()[slot]), side)
        }
        HdlType::StreamSocket => {
            f(HdlRef::StreamSocket(&mut STREAM_SOCKETS.shards[shard].lock().unwrap()[slot]), side)
        }
        HdlType::DatagramSocket => f(
            HdlRef::DatagramSocket(&mut DATAGRAM_SOCKETS.shards[shard].lock().unwrap()[slot]),
            side,
        ),
    }
}

const SHARD_COUNT: usize = 16;

struct HandleTable<T> {
    shards: [Mutex<Slab<Hdl<T>>>; SHARD_COUNT],
    next_shard: AtomicUsize,
    _t: std::marker::PhantomData<T>,
}

lazy_static::lazy_static! {
    static ref CHANNELS: HandleTable<ChannelMessage> = Default::default();
    static ref STREAM_SOCKETS: HandleTable<u8> = Default::default();
    static ref DATAGRAM_SOCKETS: HandleTable<Vec<u8>> = Default::default();
}

static NEXT_KOID: AtomicU64 = AtomicU64::new(1);

impl<T> Default for HandleTable<T> {
    fn default() -> Self {
        Self {
            shards: Default::default(),
            next_shard: Default::default(),
            _t: std::marker::PhantomData,
        }
    }
}

impl<T> HandleTable<T> {
    fn new_handle_slot(&self) -> (usize, usize) {
        let shard = self.next_shard.fetch_add(1, Ordering::Relaxed) & 15;
        let mut h = self.shards[shard].lock().unwrap();
        (
            shard,
            h.insert(Hdl {
                q: Default::default(),
                wakers: Default::default(),
                liveness: Liveness::Open,
                koid_left: NEXT_KOID.fetch_add(2, Ordering::Relaxed),
            }),
        )
    }
}

fn pack_handle(shard: usize, slot: usize, ty: HdlType, side: Side) -> u32 {
    assert!(shard <= SHARD_COUNT);
    let side_bit = match side {
        Side::Left => 0,
        Side::Right => 1,
    };
    let ty_bits = match ty {
        HdlType::Channel => 0,
        HdlType::StreamSocket => 1,
        HdlType::DatagramSocket => 2,
    };
    let shard_bits = shard as u32;
    let slot_bits = slot as u32;
    (slot_bits << 7) | (shard_bits << 3) | (ty_bits << 1) | side_bit
}

fn unpack_handle(handle: u32) -> (usize, usize, HdlType, Side) {
    let side = if handle & 1 == 0 { Side::Left } else { Side::Right };
    let ty = match (handle >> 1) & 0x3 {
        0 => HdlType::Channel,
        1 => HdlType::StreamSocket,
        2 => HdlType::DatagramSocket,
        x => panic!("Bad handle type {}", x),
    };
    let shard = ((handle >> 3) & 15) as usize;
    let slot = (handle >> 7) as usize;
    (shard, slot, ty, side)
}

fn table_contains<T>(tbl: &HandleTable<T>, shard: usize, slot: usize, side: Side) -> bool {
    match tbl.shards[shard].lock().unwrap().get(slot) {
        None => false,
        Some(h) => match (h.liveness, side) {
            (Liveness::Open, _) => true,
            (Liveness::Left, Side::Left) => true,
            (Liveness::Right, Side::Right) => true,
            _ => false,
        },
    }
}

fn close_in_table<T>(tbl: &HandleTable<T>, shard: usize, slot: usize, side: Side) {
    let mut tbl = tbl.shards[shard].lock().unwrap();
    let h = &mut tbl[slot];
    let removed = match h.liveness.close(side) {
        None => Some(tbl.remove(slot)),
        Some(liveness) => {
            h.liveness = liveness;
            h.wakers.side_mut(side).take().map(|w| w.wake());
            None
        }
    };
    drop(tbl);
    drop(removed);
}

/// Close the handle: no action if hdl==INVALID_HANDLE
fn hdl_close(hdl: u32) {
    if hdl == INVALID_HANDLE {
        return;
    }
    let (shard, slot, ty, side) = unpack_handle(hdl);
    match ty {
        HdlType::Channel => close_in_table(&CHANNELS, shard, slot, side),
        HdlType::StreamSocket => close_in_table(&STREAM_SOCKETS, shard, slot, side),
        HdlType::DatagramSocket => close_in_table(&DATAGRAM_SOCKETS, shard, slot, side),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_zircon_status as zx_status;
    use zx_status::Status;

    #[test]
    fn channel_write_read() {
        let (a, b) = Channel::create().unwrap();
        let (c, d) = Channel::create().unwrap();
        let mut incoming = MessageBuf::new();

        assert_eq!(b.read(&mut incoming).err().unwrap(), Status::SHOULD_WAIT);
        d.write(&[4, 5, 6], &mut vec![]).unwrap();
        a.write(&[1, 2, 3], &mut vec![c.into(), d.into()]).unwrap();

        b.read(&mut incoming).unwrap();
        assert_eq!(incoming.bytes(), &[1, 2, 3]);
        assert_eq!(incoming.n_handles(), 2);
        let c: Channel = incoming.take_handle(0).unwrap().into();
        let d: Channel = incoming.take_handle(1).unwrap().into();
        c.read(&mut incoming).unwrap();
        drop(d);
        assert_eq!(incoming.bytes(), &[4, 5, 6]);
        assert_eq!(incoming.n_handles(), 0);
    }

    #[test]
    fn socket_write_read() {
        let (a, b) = Socket::create(SocketOpts::STREAM).unwrap();
        a.write(&[1, 2, 3]).unwrap();
        let mut buf = [0u8; 128];
        assert_eq!(b.read(&mut buf).unwrap(), 3);
        assert_eq!(&buf[0..3], &[1, 2, 3]);
    }

    #[test]
    fn channel_basic() {
        let (p1, p2) = Channel::create().unwrap();

        let mut empty = vec![];
        assert!(p1.write(b"hello", &mut empty).is_ok());

        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(buf.bytes(), b"hello");
    }

    #[test]
    fn channel_send_handle() {
        let hello_length: usize = 5;

        // Create a pair of channels and a pair of sockets.
        let (p1, p2) = Channel::create().unwrap();
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();

        // Send one socket down the channel
        let mut handles_to_send: Vec<Handle> = vec![s1.into_handle()];
        assert!(p1.write(b"", &mut handles_to_send).is_ok());
        // Handle should be removed from vector.
        assert!(handles_to_send.is_empty());

        // Read the handle from the receiving channel.
        let mut buf = MessageBuf::new();
        assert!(p2.read(&mut buf).is_ok());
        assert_eq!(buf.n_handles(), 1);
        // Take the handle from the buffer.
        let received_handle = buf.take_handle(0).unwrap();
        // Should not affect number of handles.
        assert_eq!(buf.n_handles(), 1);
        // Trying to take it again should fail.
        assert!(buf.take_handle(0).is_none());

        // Now to test that we got the right handle, try writing something to it...
        let received_socket = Socket::from(received_handle);
        assert!(received_socket.write(b"hello").is_ok());

        // ... and reading it back from the original VMO.
        let mut read_vec = vec![0; hello_length];
        assert!(s2.read(&mut read_vec).is_ok());
        assert_eq!(read_vec, b"hello");
    }

    #[test]
    fn socket_basic() {
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();

        // Write two packets and read from other end
        assert_eq!(s1.write(b"hello").unwrap(), 5);
        assert_eq!(s1.write(b"world").unwrap(), 5);

        let mut read_vec = vec![0; 11];
        assert_eq!(s2.read(&mut read_vec).unwrap(), 10);
        assert_eq!(&read_vec[0..10], b"helloworld");

        // Try reading when there is nothing to read.
        assert_eq!(s2.read(&mut read_vec), Err(Status::SHOULD_WAIT));
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn handle_type_is_correct() {
        let (c1, c2) = Channel::create().unwrap();
        let (s1, s2) = Socket::create(SocketOpts::STREAM).unwrap();
        assert_eq!(c1.into_handle().handle_type(), HandleType::Channel);
        assert_eq!(c2.into_handle().handle_type(), HandleType::Channel);
        assert_eq!(s1.into_handle().handle_type(), HandleType::Socket);
        assert_eq!(s2.into_handle().handle_type(), HandleType::Socket);
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn invalid_handle_is_not_dangling() {
        let h = Handle::invalid();
        assert!(!h.is_dangling());
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn live_valid_handle_is_not_dangling() {
        let (c1, c2) = Channel::create().unwrap();
        assert!(!c1.is_dangling());
        assert!(!c2.is_dangling());
    }

    #[cfg(not(target_os = "fuchsia"))]
    #[test]
    fn dropped_handle_is_dangling() {
        let (c, _) = Channel::create().unwrap();
        unsafe {
            drop(Handle::from_raw(c.raw_handle()));
        }
        assert!(c.is_dangling());
        std::mem::forget(c);
    }
}
