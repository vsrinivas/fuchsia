// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::{ClientEnd, Proxy},
    fidl_fuchsia_bluetooth_bredr as bredr, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        io,
        stream::{FusedStream, Stream},
        Future, TryFutureExt,
    },
    std::convert::TryFrom,
    std::{
        fmt,
        ops::Deref,
        pin::Pin,
        sync::{Arc, Mutex},
        task::{Context, Poll},
    },
};

/// The Channel mode in use for a BR/EDR channel.
#[derive(PartialEq, Debug, Clone)]
pub enum ChannelMode {
    Basic,
    EnhancedRetransmissionMode,
}

impl fmt::Display for ChannelMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ChannelMode::Basic => write!(f, "Basic"),
            ChannelMode::EnhancedRetransmissionMode => write!(f, "ERTM"),
        }
    }
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
    flush_timeout: Arc<Mutex<Option<zx::Duration>>>,
    audio_direction_ext: Option<bredr::AudioDirectionExtProxy>,
    l2cap_parameters_ext: Option<bredr::L2capParametersExtProxy>,
    terminated: bool,
}

impl Channel {
    /// Attempt to make a Channel from a zircon socket and a Maximum TX size received out of band.
    /// Returns Err(status) if there is an error.
    pub fn from_socket(socket: zx::Socket, max_tx_size: usize) -> Result<Self, zx::Status> {
        Ok(Channel {
            socket: fasync::Socket::from_socket(socket)?,
            mode: ChannelMode::Basic,
            max_tx_size,
            flush_timeout: Arc::new(Mutex::new(None)),
            audio_direction_ext: None,
            l2cap_parameters_ext: None,
            terminated: false,
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

    pub fn flush_timeout(&self) -> Option<zx::Duration> {
        self.flush_timeout.lock().unwrap().clone()
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

    /// Attempt to set the flush timeout for this channel.
    /// If the timeout is not already set within 1ms of `duration`, we attempt to set it using the
    /// L2cap parameter extension.
    /// `duration` can be infinite to set packets flushable without a timeout.
    /// Returns a future that when polled will set the flush timeout and return the new timeout,
    /// or return an error setting the parameter is not supported.
    pub fn set_flush_timeout(
        &self,
        duration: Option<zx::Duration>,
    ) -> impl Future<Output = Result<Option<zx::Duration>, Error>> {
        let flush_timeout = self.flush_timeout.clone();
        let current = self.flush_timeout.lock().unwrap().clone();
        let proxy = self.l2cap_parameters_ext.clone();
        async move {
            match (current, duration) {
                (None, None) => return Ok(None),
                (Some(old), Some(new)) if (old - new).into_millis().abs() < 2 => {
                    return Ok(current)
                }
                _ => {}
            };
            let proxy = proxy.ok_or(format_err!("L2Cap parameter changing not supported"))?;
            let parameters = bredr::ChannelParameters {
                flush_timeout: duration.clone().map(zx::Duration::into_nanos),
                ..bredr::ChannelParameters::EMPTY
            };
            let new_params = proxy.request_parameters(parameters).await?;
            let new_timeout = new_params.flush_timeout.map(zx::Duration::from_nanos);
            *(flush_timeout.lock().unwrap()) = new_timeout.clone();
            Ok(new_timeout)
        }
    }

    pub fn closed<'a>(&'a self) -> impl Future<Output = Result<(), zx::Status>> + 'a {
        let close_signals = zx::Signals::SOCKET_PEER_CLOSED;
        let close_wait = fasync::OnSignals::new(&self.socket, close_signals);
        close_wait.map_ok(|_o| ())
    }
}

impl TryFrom<fidl_fuchsia_bluetooth_bredr::Channel> for Channel {
    type Error = zx::Status;

    fn try_from(fidl: bredr::Channel) -> Result<Self, Self::Error> {
        Ok(Self {
            socket: fasync::Socket::from_socket(fidl.socket.ok_or(zx::Status::INVALID_ARGS)?)?,
            mode: fidl.channel_mode.unwrap_or(bredr::ChannelMode::Basic).into(),
            max_tx_size: fidl.max_tx_sdu_size.ok_or(zx::Status::INVALID_ARGS)? as usize,
            flush_timeout: Arc::new(Mutex::new(fidl.flush_timeout.map(zx::Duration::from_nanos))),
            audio_direction_ext: fidl.ext_direction.and_then(|e| e.into_proxy().ok()),
            l2cap_parameters_ext: fidl.ext_l2cap.and_then(|e| e.into_proxy().ok()),
            terminated: false,
        })
    }
}

impl TryFrom<Channel> for bredr::Channel {
    type Error = anyhow::Error;

    fn try_from(channel: Channel) -> Result<Self, Self::Error> {
        let socket = channel.socket.into_zx_socket();
        let ext_direction = channel
            .audio_direction_ext
            .map(|proxy| {
                let chan = proxy.into_channel()?;
                Ok(ClientEnd::new(chan.into()))
            })
            .transpose()
            .map_err(|_: bredr::AudioDirectionExtProxy| {
                format_err!("Audio Direction proxy in use")
            })?;
        let ext_l2cap = channel
            .l2cap_parameters_ext
            .map(|proxy| {
                let chan = proxy.into_channel()?;
                Ok(ClientEnd::new(chan.into()))
            })
            .transpose()
            .map_err(|_: bredr::L2capParametersExtProxy| {
                format_err!("L2cap parameters proxy in use")
            })?;
        let flush_timeout = channel.flush_timeout.lock().unwrap().map(zx::Duration::into_nanos);
        Ok(bredr::Channel {
            socket: Some(socket),
            channel_mode: Some(channel.mode.into()),
            max_tx_sdu_size: Some(channel.max_tx_size as u16),
            ext_direction,
            flush_timeout,
            ext_l2cap,
            ..bredr::Channel::EMPTY
        })
    }
}

impl Stream for Channel {
    type Item = Result<Vec<u8>, zx::Status>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            panic!("Channel polled after terminated");
        }

        let mut res = Vec::<u8>::new();
        match self.socket.poll_datagram(cx, &mut res) {
            Poll::Ready(Ok(_size)) => Poll::Ready(Some(Ok(res))),
            Poll::Ready(Err(zx::Status::PEER_CLOSED)) => {
                self.terminated = true;
                Poll::Ready(None)
            }
            Poll::Ready(Err(e)) => Poll::Ready(Some(Err(e))),
            Poll::Pending => Poll::Pending,
        }
    }
}

impl FusedStream for Channel {
    fn is_terminated(&self) -> bool {
        self.terminated
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
        let _exec = fasync::TestExecutor::new().unwrap();
        let (recv, send) = Channel::create();

        let mut vec = Vec::new();
        let datagram_fut = recv.read_datagram(&mut vec);

        let heart: &[u8] = &[0xF0, 0x9F, 0x92, 0x96];
        assert_eq!(heart.len(), send.as_ref().write(heart).expect("should write successfully"));

        assert_eq!(Some(Ok(4)), datagram_fut.now_or_never());
        assert_eq!(heart, vec.as_slice());
    }

    #[test]
    fn test_channel_from_fidl() {
        let _exec = fasync::TestExecutor::new().unwrap();
        let empty = bredr::Channel::EMPTY;
        assert!(Channel::try_from(empty).is_err());

        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();

        let okay = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            ..bredr::Channel::EMPTY
        };

        let chan = Channel::try_from(okay).expect("okay channel to be converted");

        assert_eq!(1004, chan.max_tx_size());
        assert_eq!(&ChannelMode::Basic, chan.channel_mode());
    }

    #[test]
    fn test_channel_closed() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (recv, send) = Channel::create();

        let closed_fut = recv.closed();
        pin_mut!(closed_fut);

        assert!(exec.run_until_stalled(&mut closed_fut).is_pending());

        drop(send);

        assert!(exec.run_until_stalled(&mut closed_fut).is_ready());
    }

    #[test]
    fn test_direction_ext() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let no_ext = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            ..bredr::Channel::EMPTY
        };
        let channel = Channel::try_from(no_ext).unwrap();

        assert!(exec
            .run_singlethreaded(channel.set_audio_priority(A2dpDirection::Normal))
            .is_err());
        assert!(exec.run_singlethreaded(channel.set_audio_priority(A2dpDirection::Sink)).is_err());

        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let (client_end, mut direction_request_stream) =
            create_request_stream::<bredr::AudioDirectionExtMarker>().unwrap();
        let ext = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            ext_direction: Some(client_end),
            ..bredr::Channel::EMPTY
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
            _x => panic!("Expected ok result from audio direction response"),
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
            _x => panic!("Expected error result from audio direction response"),
        };
    }

    #[test]
    fn test_flush_timeout() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let no_ext = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            flush_timeout: Some(50_000_000), // 50 milliseconds
            ..bredr::Channel::EMPTY
        };
        let channel = Channel::try_from(no_ext).unwrap();

        assert_eq!(Some(zx::Duration::from_millis(50)), channel.flush_timeout());

        // Within 2 milliseconds, doesn't change.
        let res =
            exec.run_singlethreaded(channel.set_flush_timeout(Some(zx::Duration::from_millis(49))));
        assert_eq!(Some(zx::Duration::from_millis(50)), res.expect("shouldn't error"));
        let res =
            exec.run_singlethreaded(channel.set_flush_timeout(Some(zx::Duration::from_millis(51))));
        assert_eq!(Some(zx::Duration::from_millis(50)), res.expect("shouldn't error"));

        assert!(exec
            .run_singlethreaded(channel.set_flush_timeout(Some(zx::Duration::from_millis(200))))
            .is_err());
        assert!(exec.run_singlethreaded(channel.set_flush_timeout(None)).is_err());

        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM).unwrap();
        let (client_end, mut l2cap_request_stream) =
            create_request_stream::<bredr::L2capParametersExtMarker>().unwrap();
        let ext = bredr::Channel {
            socket: Some(remote),
            channel_mode: Some(bredr::ChannelMode::Basic),
            max_tx_sdu_size: Some(1004),
            flush_timeout: None,
            ext_l2cap: Some(client_end),
            ..bredr::Channel::EMPTY
        };

        let channel = Channel::try_from(ext).unwrap();

        {
            let flush_timeout_fut = channel.set_flush_timeout(None);
            pin_mut!(flush_timeout_fut);

            // Requesting no change returns right away with no change.
            match exec.run_until_stalled(&mut flush_timeout_fut) {
                Poll::Ready(Ok(None)) => {}
                x => panic!("Expected no flush timeout to not stall, got {:?}", x),
            }
        }

        let req_duration = zx::Duration::from_millis(42);

        {
            let flush_timeout_fut = channel.set_flush_timeout(Some(req_duration));
            pin_mut!(flush_timeout_fut);

            assert!(exec.run_until_stalled(&mut flush_timeout_fut).is_pending());

            match exec.run_until_stalled(&mut l2cap_request_stream.next()) {
                Poll::Ready(Some(Ok(bredr::L2capParametersExtRequest::RequestParameters {
                    request,
                    responder,
                }))) => {
                    assert_eq!(Some(req_duration.into_nanos()), request.flush_timeout);
                    // Send a different response
                    let params = bredr::ChannelParameters {
                        flush_timeout: Some(50_000_000), // 50ms
                        ..bredr::ChannelParameters::EMPTY
                    };
                    responder.send(params).expect("response to send cleanly");
                }
                x => panic!("Expected a item to be ready on the request stream, got {:?}", x),
            };

            match exec.run_until_stalled(&mut flush_timeout_fut) {
                Poll::Ready(Ok(Some(duration))) => {
                    assert_eq!(zx::Duration::from_millis(50), duration)
                }
                x => panic!("Expected ready result from params response, got {:?}", x),
            };
        }

        // Channel should have recorded the new flush timeout.
        assert_eq!(Some(zx::Duration::from_millis(50)), channel.flush_timeout());
    }
}
