// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::{encoding::Decodable, endpoints::ClientEnd},
    fidl_fuchsia_bluetooth, fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync,
    fuchsia_zircon as zx,
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

pub enum A2dpDirection {
    Normal,
    Source,
    Sink,
}

impl From<A2dpDirection> for bredr::A2dpDirectionPriority {
    fn from(pri: A2dpDirection) -> Self {
        match pri {
            A2dpDirection::Normal => bredr::A2dpDirectionPriority::Normal,
            A2dpDirection::Source => bredr::A2dpDirectionPriority::Source,
            A2dpDirection::Sink => bredr::A2dpDirectionPriority::Sink,
        }
    }
}

impl From<fidl_fuchsia_bluetooth_bredr::ChannelMode> for ChannelMode {
    fn from(fidl: bredr::ChannelMode) -> Self {
        match fidl {
            bredr::ChannelMode::Basic => ChannelMode::Basic,
            bredr::ChannelMode::EnhancedRetransmission => ChannelMode::EnhancedRetransmissionMode,
        }
    }
}

impl From<ChannelMode> for bredr::ChannelMode {
    fn from(x: ChannelMode) -> Self {
        match x {
            ChannelMode::Basic => bredr::ChannelMode::Basic,
            ChannelMode::EnhancedRetransmissionMode => bredr::ChannelMode::EnhancedRetransmission,
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
    audio_direction_ext: Option<bredr::AudioDirectionExtProxy>,
}

impl Channel {
    /// Attempt to make a Channel from a zircon socket and a Maximum TX size received out of band.
    /// Returns Err(status) if there is an error.
    pub fn from_socket(socket: zx::Socket, max_tx_size: usize) -> Result<Self, zx::Status> {
        Ok(Channel {
            socket: fasync::Socket::from_socket(socket)?,
            mode: ChannelMode::Basic,
            max_tx_size,
            audio_direction_ext: None,
        })
    }

    /// The default max tx size is the default MTU size for L2CAP minus the channel header content.
    /// See the Bluetooth Core Specification, Vol 3, Part A, Sec 5.1
    pub const DEFAULT_MAX_TX: usize = 672;

    /// Makes a pair of channels which are connected to each other, used commonly for testing.
    /// The max_tx_size is set to `Channel::DEFAULT_MAX_TX`.
    pub fn create() -> (Self, Self) {
        Self::create_with_max_tx(Self::DEFAULT_MAX_TX)
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

    /// Returns a future which will set the audio priority of the channel.
    /// The future will return Err if setting the priority is not supported.
    pub fn set_audio_priority(
        &self,
        dir: A2dpDirection,
    ) -> impl Future<Output = Result<(), Error>> {
        let proxy = self.audio_direction_ext.clone();
        async move {
            match proxy {
                None => return Err(format_err!("Audio Priority not supported")),
                Some(proxy) => proxy
                    .set_priority(dir.into())
                    .await?
                    .map_err(|e| format_err!("Setting priority failed: {:?}", e)),
            }
        }
    }

    pub fn closed<'a>(&'a self) -> impl Future<Output = Result<(), zx::Status>> + 'a {
        let close_signals = zx::Signals::SOCKET_PEER_CLOSED;
        let close_wait = fasync::OnSignals::new(&self.socket, close_signals);
        close_wait.map_ok(|o| ())
    }
}

impl TryFrom<fidl_fuchsia_bluetooth_bredr::Channel> for Channel {
    type Error = zx::Status;

    fn try_from(fidl: bredr::Channel) -> Result<Self, Self::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(fidl.socket.ok_or(zx::Status::INVALID_ARGS)?)?,
            mode: fidl.channel_mode.unwrap_or(bredr::ChannelMode::Basic).into(),
            max_tx_size: fidl.max_tx_sdu_size.ok_or(zx::Status::INVALID_ARGS)? as usize,
            audio_direction_ext: fidl.ext_direction.and_then(|e| e.into_proxy().ok()),
        })
    }
}

impl TryFrom<Channel> for bredr::Channel {
    type Error = anyhow::Error;

    fn try_from(channel: Channel) -> Result<Self, Self::Error> {
        let socket = channel.socket.into_zx_socket().map_err(|_| format_err!("socket in use"))?;
        let ext_direction = match channel.audio_direction_ext {
            None => None,
            Some(proxy) => {
                let chan = proxy
                    .into_channel()
                    .map_err(|_| format_err!("Audio Direction proxy in use"))?;
                Some(ClientEnd::new(chan.into()))
            }
        };
        Ok(bredr::Channel {
            socket: Some(socket),
            channel_mode: Some(channel.mode.into()),
            max_tx_sdu_size: Some(channel.max_tx_size as u16),
            ext_direction,
            ..Decodable::new_empty()
        })
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
    use fidl::endpoints::create_request_stream;
    use futures::{pin_mut, FutureExt, StreamExt};

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
        let empty = bredr::Channel::new_empty();
        assert!(Channel::try_from(empty).is_err());

        let (remote, local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        let okay = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
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

    #[test]
    fn test_direction_ext() {
        let mut exec = fasync::Executor::new().unwrap();

        let (remote, local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let no_ext = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            ..Decodable::new_empty()
        };
        let channel = Channel::try_from(no_ext).unwrap();

        assert!(exec
            .run_singlethreaded(channel.set_audio_priority(A2dpDirection::Normal))
            .is_err());
        assert!(exec.run_singlethreaded(channel.set_audio_priority(A2dpDirection::Sink)).is_err());

        let (remote, local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let (client_end, mut direction_request_stream) =
            create_request_stream::<bredr::AudioDirectionExtMarker>().unwrap();
        let ext = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            ext_direction: Some(client_end),
            ..Decodable::new_empty()
        };

        let channel = Channel::try_from(ext).unwrap();

        let audio_direction_fut = channel.set_audio_priority(A2dpDirection::Normal);
        pin_mut!(audio_direction_fut);

        assert!(exec.run_until_stalled(&mut audio_direction_fut).is_pending());

        match exec.run_until_stalled(&mut direction_request_stream.next()) {
            Poll::Ready(Some(Ok(bredr::AudioDirectionExtRequest::SetPriority {
                priority,
                responder,
            }))) => {
                assert_eq!(bredr::A2dpDirectionPriority::Normal, priority);
                responder.send(&mut Ok(())).expect("response to send cleanly");
            }
            x => panic!("Expected a item to be ready on the request stream, got {:?}", x),
        };

        match exec.run_until_stalled(&mut audio_direction_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected ok result from audio direction response"),
        };

        let audio_direction_fut = channel.set_audio_priority(A2dpDirection::Sink);
        pin_mut!(audio_direction_fut);

        assert!(exec.run_until_stalled(&mut audio_direction_fut).is_pending());

        match exec.run_until_stalled(&mut direction_request_stream.next()) {
            Poll::Ready(Some(Ok(bredr::AudioDirectionExtRequest::SetPriority {
                priority,
                responder,
            }))) => {
                assert_eq!(bredr::A2dpDirectionPriority::Sink, priority);
                responder
                    .send(&mut Err(fidl_fuchsia_bluetooth::ErrorCode::Failed))
                    .expect("response to send cleanly");
            }
            x => panic!("Expected a item to be ready on the request stream, got {:?}", x),
        };

        match exec.run_until_stalled(&mut audio_direction_fut) {
            Poll::Ready(Err(_)) => {}
            x => panic!("Expected error result from audio direction response"),
        };
    }
}
