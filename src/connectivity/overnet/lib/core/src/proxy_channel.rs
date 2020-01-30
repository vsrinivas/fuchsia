// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    async_quic::{AsyncQuicStreamReader, AsyncQuicStreamWriter},
    coding::{decode_fidl, encode_fidl},
    framed_stream::{framed, FrameType, FramedStreamReader, FramedStreamWriter, MessageStats},
    future_help::log_errors,
    proxy_socket_datagram::spawn_socket_datagram_proxy,
    proxy_socket_stream::spawn_socket_stream_proxy,
    runtime::spawn,
};
use anyhow::{bail, Error};
use fidl::{AsyncChannel, Channel, Handle, HandleBased, Socket, SocketOpts};
use fidl_fuchsia_overnet_protocol::{
    ChannelHandle, SocketHandle, SocketType, ZirconChannelMessage, ZirconHandle,
};
use std::rc::Rc;

/// When sending a datagram on a channel, contains information needed to establish streams
/// for any handles being sent.
enum SendHandle {
    /// A handle of type channel is being sent.
    Channel,
    Socket(SocketType),
}

pub fn spawn_channel_proxy(
    chan: Channel,
    stream_io: (AsyncQuicStreamWriter, AsyncQuicStreamReader),
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    let chan = Rc::new(AsyncChannel::from_channel(chan)?);
    let (stream_writer, stream_reader) = framed(stream_io);
    spawn(log_errors(
        channel_to_stream(chan.clone(), stream_writer, stats.clone()),
        "channel_to_stream failed",
    ));
    spawn(log_errors(stream_to_channel(stream_reader, chan, stats), "stream_to_channel failed"));
    Ok(())
}

#[cfg(not(target_os = "fuchsia"))]
fn handle_type(hdl: &Handle) -> Result<SendHandle, Error> {
    Ok(match hdl.handle_type() {
        fidl::FidlHdlType::Channel => SendHandle::Channel,
        fidl::FidlHdlType::Socket => SendHandle::Socket(SocketType::Stream),
        fidl::FidlHdlType::Invalid => bail!("Unsupported handle type"),
    })
}

#[cfg(target_os = "fuchsia")]
fn handle_type(handle: &Handle) -> Result<SendHandle, Error> {
    use fuchsia_zircon as zx;
    use zx::AsHandleRef;

    // zx_info_socket_t is able to be safely replaced with a byte representation and is a PoD type.
    struct SocketInfoQuery;
    unsafe impl zx::ObjectQuery for SocketInfoQuery {
        const TOPIC: zx::Topic = zx::Topic::SOCKET;
        type InfoTy = zx::sys::zx_info_socket_t;
    }

    match handle.basic_info()?.object_type {
        zx::ObjectType::CHANNEL => Ok(SendHandle::Channel),
        zx::ObjectType::SOCKET => {
            let mut info = zx::sys::zx_info_socket_t::default();
            let info = zx::object_get_info::<SocketInfoQuery>(
                handle.as_handle_ref(),
                std::slice::from_mut(&mut info),
            )
            .map(|_| zx::SocketInfo::from(info))?;
            let socket_type = match info.options {
                zx::SocketOpts::STREAM => SocketType::Stream,
                zx::SocketOpts::DATAGRAM => SocketType::Datagram,
                _ => bail!("Unhandled socket options"),
            };
            Ok(SendHandle::Socket(socket_type))
        }
        _ => bail!("Handle type not proxyable {:?}", handle.basic_info()?.object_type),
    }
}

fn spawn_socket_proxy(
    socket_type: SocketType,
    sock: Socket,
    stream_io: (AsyncQuicStreamWriter, AsyncQuicStreamReader),
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    match socket_type {
        SocketType::Stream => spawn_socket_stream_proxy(sock, stream_io, stats.clone()),
        SocketType::Datagram => spawn_socket_datagram_proxy(sock, stream_io, stats.clone()),
    }
}

async fn channel_to_stream(
    chan: Rc<AsyncChannel>,
    mut stream: FramedStreamWriter,
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    let mut buf = fidl::MessageBuf::new();
    loop {
        chan.recv_msg(&mut buf).await?;
        let (bytes, handles) = buf.split_mut();
        let mut send_handles = Vec::new();
        for handle in handles {
            let stream_io = stream.underlying_quic_connection().alloc_bidi();
            let stream_id = fidl_fuchsia_overnet_protocol::StreamId { id: stream_io.0.id() };
            match handle_type(&handle)? {
                SendHandle::Channel => {
                    let channel =
                        Channel::from_handle(std::mem::replace(handle, Handle::invalid()));
                    spawn_channel_proxy(channel, stream_io, stats.clone())?;
                    send_handles.push(ZirconHandle::Channel(ChannelHandle { stream_id }));
                }
                SendHandle::Socket(socket_type) => {
                    let socket = Socket::from_handle(std::mem::replace(handle, Handle::invalid()));
                    spawn_socket_proxy(socket_type, socket, stream_io, stats.clone())?;
                    send_handles
                        .push(ZirconHandle::Socket(SocketHandle { stream_id, socket_type }));
                }
            }
        }
        let mut msg = ZirconChannelMessage {
            bytes: std::mem::replace(bytes, Vec::new()),
            handles: send_handles,
        };
        stream.send(FrameType::Data, encode_fidl(&mut msg)?.as_mut_slice(), false, &*stats).await?;
    }
}

async fn stream_to_channel(
    mut stream: FramedStreamReader,
    chan: Rc<AsyncChannel>,
    stats: Rc<MessageStats>,
) -> Result<(), Error> {
    loop {
        let (frame_type, mut msg, fin) = stream.next().await?;
        match frame_type {
            FrameType::Data => {
                let msg = decode_fidl::<ZirconChannelMessage>(&mut msg)?;
                let mut handles = Vec::new();
                for unbound in msg.handles.into_iter() {
                    let bound = match unbound {
                        ZirconHandle::Channel(ChannelHandle { stream_id: stream_index }) => {
                            let (overnet_channel, app_channel) = Channel::create()?;
                            spawn_channel_proxy(
                                overnet_channel,
                                stream.underlying_quic_connection().bind_id(stream_index.id),
                                stats.clone(),
                            )?;
                            app_channel.into_handle()
                        }
                        ZirconHandle::Socket(SocketHandle {
                            stream_id: stream_index,
                            socket_type,
                        }) => {
                            let (overnet_socket, app_socket) = Socket::create(match socket_type {
                                SocketType::Stream => SocketOpts::STREAM,
                                SocketType::Datagram => SocketOpts::DATAGRAM,
                            })?;
                            spawn_socket_proxy(
                                socket_type,
                                overnet_socket,
                                stream.underlying_quic_connection().bind_id(stream_index.id),
                                stats.clone(),
                            )?;
                            app_socket.into_handle()
                        }
                    };
                    handles.push(bound);
                }
                chan.write(&msg.bytes, &mut handles)?;
            }
        }
        if fin {
            return Ok(());
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::async_quic::test_util::new_client_server;
    use crate::router::test_util::run;

    #[test]
    fn simple() {
        run(|| async move {
            let (alice1, bob1) = Channel::create().unwrap();
            let (alice2, bob2) = Channel::create().unwrap();
            let (client, server) = new_client_server().unwrap();
            let cli_stream_io = client.alloc_bidi();
            let svr_stream_io = server.bind_id(cli_stream_io.0.id());
            let cli_stats: Rc<MessageStats> = Rc::new(Default::default());
            let svr_stats: Rc<MessageStats> = Rc::new(Default::default());
            spawn_channel_proxy(bob1, cli_stream_io, cli_stats.clone()).unwrap();
            spawn_channel_proxy(alice2, svr_stream_io, svr_stats.clone()).unwrap();
            let alice1 = AsyncChannel::from_channel(alice1).unwrap();
            let bob2 = AsyncChannel::from_channel(bob2).unwrap();
            alice1.write(&[1, 2, 3], &mut vec![]).unwrap();
            let mut buf = fidl::MessageBuf::new();
            bob2.recv_msg(&mut buf).await.unwrap();
            assert_eq!(buf.bytes(), &[1, 2, 3]);
            assert_eq!(buf.n_handles(), 0);
        })
    }
}
