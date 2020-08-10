// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth_bredr, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{io, stream::Stream, Future, TryFutureExt},
    std::convert::TryFrom,
    std::{
        ops::Deref,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// The Channel mode in use for a BR/EDR channel.
#[derive(PartialEq, Debug, Clone)]
pub enum ChannelMode {
    Basic,
    EnhancedRetransmissionMode,
}

impl From<fidl_fuchsia_bluetooth_bredr::ChannelMode> for ChannelMode {
    fn from(fidl: fidl_fuchsia_bluetooth_bredr::ChannelMode) -> Self {
        match fidl {
            fidl_fuchsia_bluetooth_bredr::ChannelMode::Basic => ChannelMode::Basic,
            fidl_fuchsia_bluetooth_bredr::ChannelMode::EnhancedRetransmission => {
                ChannelMode::EnhancedRetransmissionMode
            }
        }
    }
}

impl From<ChannelMode> for fidl_fuchsia_bluetooth_bredr::ChannelMode {
    fn from(x: ChannelMode) -> Self {
        match x {
            ChannelMode::Basic => fidl_fuchsia_bluetooth_bredr::ChannelMode::Basic,
            ChannelMode::EnhancedRetransmissionMode => {
                fidl_fuchsia_bluetooth_bredr::ChannelMode::EnhancedRetransmission
            }
        }
    }
}

/// A data channel to a remote Peer. Channels are the primary data transfer mechanism for
/// Bluetooth profiles and protocols.
/// Channel currently implements Deref<Target = Socket> to easily access the underlying
/// socket, and also implements AsyncWrite using a forwarding implementation.
#[derive(Debug)]
pub struct Channel {
    socket: fasync::Socket,
    mode: ChannelMode,
    max_tx_size: usize,
}

// The default max tx size is the default MTU size for L2CAP minus the channel header content.
// See the Bluetooth Core Specification, Vol 3, Part A, Sec 5.1
const DEFAULT_MAX_TX: usize = 672;

impl Channel {
    pub fn from_socket(socket: zx::Socket, max_tx_size: usize) -> Result<Self, zx::Status> {
        Ok(Channel {
            socket: fasync::Socket::from_socket(socket)?,
            mode: ChannelMode::Basic,
            max_tx_size,
        })
    }

    /// Makes a pair of channels which are connected to each other, used commonly for testing.
    /// The max_tx_size is set to the default TX size.
    pub fn create() -> (Self, Self) {
        Self::create_with_max_tx(DEFAULT_MAX_TX)
    }

    /// Make a pair of channels which are connected to each other, used commonly for testing.
    /// The maximum transmittable unit is taken from `max_tx_size`.
    pub fn create_with_max_tx(max_tx_size: usize) -> (Self, Self) {
        let (remote, local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        (
            Channel::from_socket(remote, max_tx_size).unwrap(),
            Channel::from_socket(local, max_tx_size).unwrap(),
        )
    }

    /// The maximum transmittable size of a packet, in bytes.
    /// Trying to send packets larger than this may cause the channel to be closed.
    pub fn max_tx_size(&self) -> usize {
        self.max_tx_size
    }

    pub fn channel_mode(&self) -> &ChannelMode {
        &self.mode
    }

    pub fn closed<'a>(&'a self) -> impl Future<Output = Result<(), zx::Status>> + 'a {
        let close_signals = zx::Signals::SOCKET_PEER_CLOSED;
        let close_wait = fasync::OnSignals::new(&self.socket, close_signals);
        close_wait.map_ok(|o| ())
    }
}

impl TryFrom<fidl_fuchsia_bluetooth_bredr::Channel> for Channel {
    type Error = zx::Status;

    fn try_from(fidl: fidl_fuchsia_bluetooth_bredr::Channel) -> Result<Self, Self::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(fidl.socket.ok_or(zx::Status::INVALID_ARGS)?)?,
            mode: fidl
                .channel_mode
                .unwrap_or(fidl_fuchsia_bluetooth_bredr::ChannelMode::Basic)
                .into(),
            max_tx_size: fidl.max_tx_sdu_size.ok_or(zx::Status::INVALID_ARGS)? as usize,
        })
    }
}

impl From<Channel> for fidl_fuchsia_bluetooth_bredr::Channel {
    fn from(channel: Channel) -> Self {
        fidl_fuchsia_bluetooth_bredr::Channel {
            socket: Some(channel.socket.into_zx_socket().expect("only owner of this socket")),
            channel_mode: Some(channel.mode.into()),
            max_tx_sdu_size: Some(channel.max_tx_size as u16),
            ..Decodable::new_empty()
        }
    }
}

impl Stream for Channel {
    type Item = Result<Vec<u8>, zx::Status>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut res = Vec::<u8>::new();
        match self.socket.poll_datagram(cx, &mut res) {
            Poll::Ready(Ok(_size)) => Poll::Ready(Some(Ok(res))),
            Poll::Ready(Err(zx::Status::PEER_CLOSED)) => Poll::Ready(None),
            Poll::Ready(Err(e)) => Poll::Ready(Some(Err(e))),
            Poll::Pending => Poll::Pending,
        }
    }
}

impl Deref for Channel {
    type Target = fasync::Socket;

    fn deref(&self) -> &Self::Target {
        &self.socket
    }
}

impl io::AsyncWrite for Channel {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<Result<usize, io::Error>> {
        Pin::new(&mut self.socket).as_mut().poll_write(cx, buf)
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        Pin::new(&mut self.socket).as_mut().poll_flush(cx)
    }

    fn poll_close(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<(), io::Error>> {
        Pin::new(&mut self.socket).as_mut().poll_close(cx)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::{pin_mut, FutureExt};

    #[test]
    fn test_channel_create_and_write() {
        let _exec = fasync::Executor::new().unwrap();
        let (recv, send) = Channel::create();

        let mut vec = Vec::new();
        let datagram_fut = recv.read_datagram(&mut vec);

        let heart: &[u8] = &[0xF0, 0x9F, 0x92, 0x96];
        send.as_ref().write(heart).expect("should write successfully");

        assert_eq!(Some(Ok(4)), datagram_fut.now_or_never());
        assert_eq!(heart, vec.as_slice());
    }

    #[test]
    fn test_channel_from_fidl() {
        let _exec = fasync::Executor::new().unwrap();
        let empty = fidl_fuchsia_bluetooth_bredr::Channel::new_empty();
        assert!(Channel::try_from(empty).is_err());

        let (remote, local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        let okay = fidl_fuchsia_bluetooth_bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(fidl_fuchsia_bluetooth_bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            ..Decodable::new_empty()
        };

        let chan = Channel::try_from(okay).expect("okay channel to be converted");

        assert_eq!(1004, chan.max_tx_size());
        assert_eq!(&ChannelMode::Basic, chan.channel_mode());
    }

    #[test]
    fn test_channel_closed() {
        let mut exec = fasync::Executor::new().unwrap();

        let (recv, send) = Channel::create();

        let closed_fut = recv.closed();
        pin_mut!(closed_fut);

        assert!(exec.run_until_stalled(&mut closed_fut).is_pending());

        drop(send);

        assert!(exec.run_until_stalled(&mut closed_fut).is_ready());
    }
}
