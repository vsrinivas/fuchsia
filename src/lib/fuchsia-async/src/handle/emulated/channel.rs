// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides async Channel type wrapped around an emulated zircon channel.

// TODO(ctiller): merge this implementation with the implementation in zircon_handle?

use super::{Handle, HandleInfo, MessageBuf, MessageBufEtc};
use fuchsia_zircon_status as zx_status;
use std::{
    pin::Pin,
    task::{Context, Poll},
};

/// An I/O object representing a `Channel`.
pub struct Channel {
    channel: super::Channel,
}

impl std::fmt::Debug for Channel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.channel.fmt(f)
    }
}
impl Channel {
    /// Writes a message into the channel.
    pub fn write(&self, bytes: &[u8], handles: &mut Vec<Handle>) -> Result<(), zx_status::Status> {
        self.channel.write(bytes, handles)
    }

    /// Consumes self and returns the underlying Channel (named thusly for compatibility with
    /// fasync variant)
    pub fn into_zx_channel(self) -> super::Channel {
        self.channel
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `io::std::ErrorKind::WouldBlock`.
    ///
    /// Identical to `recv_from` except takes separate bytes and handles buffers
    /// rather than a single `MessageBuf`.
    pub fn read(
        &self,
        cx: &mut Context<'_>,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<Handle>,
    ) -> Poll<Result<(), zx_status::Status>> {
        self.channel.poll_read(cx, bytes, handles)
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `io::std::ErrorKind::WouldBlock`.
    ///
    /// Identical to `recv_etc_from` except takes separate bytes and handles
    /// buffers rather than a single `MessageBuf`.
    pub fn read_etc(
        &self,
        cx: &mut Context<'_>,
        bytes: &mut Vec<u8>,
        handles: &mut Vec<HandleInfo>,
    ) -> Poll<Result<(), zx_status::Status>> {
        self.channel.poll_read_etc(cx, bytes, handles)
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `io::std::ErrorKind::WouldBlock`.
    pub fn recv_from(
        &self,
        ctx: &mut Context<'_>,
        buf: &mut MessageBuf,
    ) -> Poll<Result<(), zx_status::Status>> {
        let (bytes, handles) = buf.split_mut();
        self.read(ctx, bytes, handles)
    }

    /// Receives a message on the channel and registers this `Channel` as
    /// needing a read on receiving a `io::std::ErrorKind::WouldBlock`.
    pub fn recv_etc_from(
        &self,
        ctx: &mut Context<'_>,
        buf: &mut MessageBufEtc,
    ) -> Poll<Result<(), zx_status::Status>> {
        let (bytes, handles) = buf.split_mut();
        self.read_etc(ctx, bytes, handles)
    }

    /// Creates a future that receive a message to be written to the buffer
    /// provided.
    ///
    /// The returned future will return after a message has been received on
    /// this socket and been placed into the buffer.
    pub fn recv_msg<'a>(&'a self, buf: &'a mut MessageBuf) -> RecvMsg<'a> {
        RecvMsg { channel: self, buf }
    }

    /// Creates a future that receive a message to be written to the buffer
    /// provided.
    ///
    /// The returned future will return after a message has been received on
    /// this socket and been placed into the buffer.
    pub fn recv_etc_msg<'a>(&'a self, buf: &'a mut MessageBufEtc) -> RecvEtcMsg<'a> {
        RecvEtcMsg { channel: self, buf }
    }

    /// Creates a new `Channel` from a previously-created `emulated_handle::Channel`.
    pub fn from_channel(channel: super::Channel) -> std::io::Result<Channel> {
        Ok(Channel { channel })
    }
}

/// A future used to receive a message from a channel.
///
/// This is created by the `Channel::recv_msg` method.
#[must_use = "futures do nothing unless polled"]
pub struct RecvMsg<'a> {
    channel: &'a Channel,
    buf: &'a mut MessageBuf,
}

impl<'a> futures::Future for RecvMsg<'a> {
    type Output = Result<(), zx_status::Status>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        this.channel.recv_from(cx, this.buf)
    }
}

/// A future used to receive a message from a channel.
///
/// This is created by the `Channel::recv_etc_msg` method.
#[must_use = "futures do nothing unless polled"]
pub struct RecvEtcMsg<'a> {
    channel: &'a Channel,
    buf: &'a mut MessageBufEtc,
}

impl<'a> futures::Future for RecvEtcMsg<'a> {
    type Output = Result<(), zx_status::Status>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        this.channel.recv_etc_from(cx, this.buf)
    }
}

#[cfg(test)]
mod test {
    use super::super::Channel;
    use super::super::{Handle, ObjectType, Rights};
    use super::Channel as AsyncChannel;
    use super::{MessageBuf, MessageBufEtc};
    use futures::executor::block_on;
    use futures::task::noop_waker_ref;
    use std::future::Future;
    use std::pin::Pin;
    use std::task::Context;

    #[test]
    fn async_channel_write_read() {
        block_on(async move {
            let (a, b) = Channel::create().unwrap();
            let (a, b) =
                (AsyncChannel::from_channel(a).unwrap(), AsyncChannel::from_channel(b).unwrap());
            let mut buf = MessageBuf::new();

            let mut cx = Context::from_waker(noop_waker_ref());

            let mut rx = b.recv_msg(&mut buf);
            assert_eq!(Pin::new(&mut rx).poll(&mut cx), std::task::Poll::Pending);
            a.write(&[1, 2, 3], &mut vec![]).unwrap();
            rx.await.unwrap();
            assert_eq!(buf.bytes(), &[1, 2, 3]);

            let mut rx = a.recv_msg(&mut buf);
            assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
            b.write(&[1, 2, 3], &mut vec![]).unwrap();
            rx.await.unwrap();
            assert_eq!(buf.bytes(), &[1, 2, 3]);
        })
    }

    #[test]
    fn async_channel_write_read_etc() {
        block_on(async move {
            let (a, b) = Channel::create().unwrap();
            let (a, b) =
                (AsyncChannel::from_channel(a).unwrap(), AsyncChannel::from_channel(b).unwrap());
            let mut buf = MessageBufEtc::new();

            let mut cx = Context::from_waker(noop_waker_ref());

            let mut rx = b.recv_etc_msg(&mut buf);
            assert_eq!(Pin::new(&mut rx).poll(&mut cx), std::task::Poll::Pending);
            a.write(&[1, 2, 3], &mut vec![]).unwrap();
            rx.await.unwrap();
            assert_eq!(buf.bytes(), &[1, 2, 3]);

            let mut rx = a.recv_etc_msg(&mut buf);
            assert!(Pin::new(&mut rx).poll(&mut cx).is_pending());
            let (c, _) = Channel::create().unwrap();
            b.write(&[1, 2, 3], &mut vec![c.into()]).unwrap();
            rx.await.unwrap();
            assert_eq!(buf.bytes(), &[1, 2, 3]);
            assert_eq!(buf.n_handle_infos(), 1);
            let hi = &buf.handle_infos[0];
            assert_ne!(hi.handle, Handle::invalid());
            assert_eq!(hi.object_type, ObjectType::CHANNEL);
            assert_eq!(hi.rights, Rights::TRANSFER | Rights::WRITE | Rights::READ);
        })
    }
}
