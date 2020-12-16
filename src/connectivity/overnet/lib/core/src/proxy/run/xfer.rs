// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::super::{
    handle::{Message, Proxyable, ProxyableHandle},
    stream::{Frame, StreamReader, StreamWriter, StreamWriterBinder},
    Proxy, ProxyTransferInitiationReceiver, StreamRefSender,
};
use crate::labels::{generate_transfer_key, Endpoint, NodeId, TransferKey};
use crate::peer::{FramedStreamReader, FramedStreamWriter, StreamProperties};
use crate::router::OpenedTransfer;
use anyhow::{bail, format_err, Error};
use fuchsia_zircon_status as zx_status;
use futures::{
    future::Either,
    prelude::*,
    task::{noop_waker_ref, Context, Poll},
};
use std::sync::{Arc, Weak};

// Follow a transfer that was initated elsewhere to the destination.
pub(crate) async fn follow<Hdl: 'static + Proxyable>(
    mut proxy: Proxy<Hdl>,
    initiate_transfer: ProxyTransferInitiationReceiver,
    stream_writer: StreamWriter<Hdl::Message>,
    new_destination_node: NodeId,
    transfer_key: TransferKey,
    stream_reader: StreamReader<Hdl::Message>,
) -> Result<(), Error> {
    let debug_id = stream_writer.debug_id();
    futures::future::try_join(stream_reader.expect_shutdown(Ok(())), async move {
        stream_writer.send_ack_transfer().await?;
        let hdl = proxy.hdl.take().ok_or_else(|| format_err!("Handle already taken"))?;
        let router = Weak::upgrade(&hdl.router()).ok_or_else(|| format_err!("Router gone"))?;
        let stats = hdl.stats().clone();
        let hdl = hdl.into_fidl_handle()?;
        drop(proxy);
        // TODO actual link hint
        log::trace!(
            "[PROXY {:?} {:?}] open_transfer to {:?} with handle {:?}",
            hdl,
            debug_id,
            new_destination_node,
            hdl
        );
        let r = router.open_transfer(new_destination_node.into(), transfer_key, hdl).await?;
        log::trace!("[PROXY {:?}] open_transfer got {:?}", debug_id, r);
        match r {
            OpenedTransfer::Fused => {
                log::trace!("[PROXY {:?}] fused after follow {:?}", debug_id, transfer_key);
                assert!(initiate_transfer.await.unwrap().is_dropped());
                Ok(())
            }
            OpenedTransfer::Remote(new_writer, new_reader, handle) => {
                let handle = Hdl::from_fidl_handle(handle)?;
                log::trace!(
                    "[PROXY {:?}] spawn from {:?}:{:?} for follow {:?}",
                    handle,
                    new_writer.peer_node_id(),
                    new_writer.id(),
                    transfer_key
                );
                make_boxed_main_loop(
                    Proxy::new(handle, Arc::downgrade(&router), stats),
                    initiate_transfer,
                    new_writer.into(),
                    None,
                    new_reader.into(),
                )
                .await?;
                Ok(())
            }
        }
    })
    .await?;
    Ok(())
}

// This needs to be split out to avoid the compiler infinite looping.
fn make_boxed_main_loop<Hdl: 'static + Proxyable>(
    proxy: Arc<Proxy<Hdl>>,
    initiate_transfer: ProxyTransferInitiationReceiver,
    stream_writer: FramedStreamWriter,
    initial_stream_reader: Option<FramedStreamReader>,
    stream_reader: FramedStreamReader,
) -> std::pin::Pin<Box<dyn Send + Future<Output = Result<(), Error>>>> {
    super::main::run_main_loop(
        proxy,
        initiate_transfer,
        stream_writer,
        initial_stream_reader,
        stream_reader,
    )
    .boxed()
}

// Initiate a transfer. The other end of a handle we're already proxying (`pair`) has been sent
// to a new endpoint. A drain stream has been established to flush already received messages to the
// new endpoint.
pub(crate) async fn initiate<Hdl: 'static + Proxyable>(
    proxy: Proxy<Hdl>,
    pair: fidl::Handle,
    mut stream_writer: StreamWriter<Hdl::Message>,
    mut stream_reader: StreamReader<Hdl::Message>,
    drain_stream: FramedStreamWriter,
    stream_ref_sender: StreamRefSender,
) -> Result<(), Error> {
    let transfer_key = generate_transfer_key();

    let drain_stream = drain_stream.bind(&proxy.hdl());
    let drain_stream_id = drain_stream.id();
    let peer_node_id = drain_stream.peer_node_id();
    let debug_id = stream_writer.debug_id();

    futures::future::try_join(
        drain_handle_to_stream(
            ProxyableHandle::new(
                Hdl::from_fidl_handle(pair)?,
                proxy.hdl().router().clone(),
                proxy.hdl().stats().clone(),
            ),
            drain_stream,
        ),
        async move {
            // Before we can send a BeginTransfer message we need to flush out any messages we intended to
            // send.
            let stream_ref_sender = flush_outgoing_messages(
                &proxy,
                transfer_key,
                &mut stream_writer,
                &mut stream_reader,
                drain_stream_id,
                stream_ref_sender,
            )
            .await?;

            // Send the BeginTransfer.
            log::trace!(
                "[PROXY {:?} {:?}] Send begin transfer {:?}",
                proxy.hdl().hdl(),
                debug_id,
                transfer_key
            );
            stream_writer.send_begin_transfer(peer_node_id, transfer_key).await?;

            if let Some(stream_ref_sender) = stream_ref_sender {
                log::trace!("[PROXY {:?} {:?}] Drain original stream", proxy.hdl().hdl(), debug_id);
                // Now we need to read any incoming messages from the original stream and prepare to send
                // them to the drain stream.
                drain_original_stream(
                    &proxy,
                    transfer_key,
                    stream_writer,
                    stream_reader,
                    drain_stream_id,
                    stream_ref_sender,
                )
                .await?;
            } else {
                // This implies we got a BeginTransfer during the channel drain
                // and consequently need to ack it (after our BeginTransfer was sent)
                log::trace!("[PROXY {:?} {:?}] Send ack", proxy.hdl().hdl(), debug_id);
                stream_writer.send_ack_transfer().await?;
                log::trace!("[PROXY {:?} {:?}] Expect ack", proxy.hdl().hdl(), debug_id);
                stream_reader.expect_ack_transfer().await?;
            }

            log::trace!(
                "[PROXY {:?} {:?}] Initiated transfer complete",
                proxy.hdl().hdl(),
                debug_id
            );
            Ok(())
        },
    )
    .await?;
    Ok(())
}

async fn drain_handle_to_stream<Hdl: 'static + Proxyable>(
    hdl: ProxyableHandle<Hdl>,
    mut stream_writer: StreamWriter<Hdl::Message>,
) -> Result<(), Error> {
    let mut message = Default::default();
    let debug_id = stream_writer.debug_id();
    loop {
        let read_result = hdl.read(&mut message).await;
        log::trace!("[PROXY {:?} {:?}] Drain stream gets {:?}", hdl.hdl(), debug_id, read_result);
        match read_result {
            Ok(()) => stream_writer.send_data(&mut message).await?,
            Err(zx_status::Status::PEER_CLOSED) => break,
            Err(x) => return Err(x.into()),
        }
    }
    stream_writer.send_end_transfer().await?;
    log::trace!("[PROXY {:?} {:?}] Sent end transfer", hdl.hdl(), debug_id);
    Ok(())
}

#[derive(Debug)]
enum FlushOutgoingMsg<'a, Msg: Message> {
    FromChannel,
    FromStream(Frame<'a, Msg>),
}

async fn flush_outgoing_messages<Hdl: 'static + Proxyable>(
    proxy: &Proxy<Hdl>,
    original_transfer_key: TransferKey,
    stream_writer: &mut StreamWriter<Hdl::Message>,
    stream_reader: &mut StreamReader<Hdl::Message>,
    drain_stream_id: u64,
    stream_ref_sender: StreamRefSender,
) -> Result<Option<StreamRefSender>, Error> {
    let mut message = Default::default();
    let endpoint = stream_reader.endpoint();
    let debug_id = stream_writer.debug_id();
    let mut ctx = Context::from_waker(noop_waker_ref());
    loop {
        let msg = match futures::future::select(
            proxy.read_from_handle(&mut message),
            stream_reader.next(),
        )
        .poll_unpin(&mut ctx)
        {
            Poll::Pending => return Ok(Some(stream_ref_sender)),
            Poll::Ready(Either::Left((x, _))) => {
                x?;
                FlushOutgoingMsg::FromChannel
            }
            Poll::Ready(Either::Right((msg, _))) => FlushOutgoingMsg::FromStream(msg?),
        };
        log::trace!(
            "[PROXY {:?} {:?}] flush_outgoing_messages gets {:?}",
            proxy.hdl().hdl(),
            debug_id,
            msg
        );
        match msg {
            FlushOutgoingMsg::FromChannel => {
                // Message was read from the channel (it's not empty yet)... so we send it out on
                // the stream.
                stream_writer.send_data(&mut message).await?;
            }
            FlushOutgoingMsg::FromStream(Frame::Data(msg)) => {
                // We received an incoming message - place it on the handle pair for a moment until
                // we can send it to the drain stream.
                proxy.write_to_handle(msg).await?;
            }
            FlushOutgoingMsg::FromStream(Frame::BeginTransfer(
                new_destination_node,
                new_transfer_key,
            )) => {
                // Uh oh! The other end has independently decided to transfer ownership of this
                // handle. We use the quic endpoint to determine behavior (such that each end makes
                // a consistent decision) - clients start a new stream to the target, servers await
                // that stream, and then we just need to drain messages.
                log::trace!(
                    "[PROXY {:?} {:?}] remote is also transferring with key {:?}; this is the {:?}",
                    proxy.hdl().hdl(),
                    debug_id,
                    new_transfer_key,
                    endpoint
                );
                match endpoint {
                    Endpoint::Client => {
                        stream_ref_sender.draining_initiate(
                            drain_stream_id,
                            new_destination_node,
                            new_transfer_key,
                        )?;
                    }
                    Endpoint::Server => {
                        stream_ref_sender.draining_await(drain_stream_id, original_transfer_key)?;
                    }
                }
                proxy.drain_handle_to_stream(stream_writer).await?;
                return Ok(None);
            }
            FlushOutgoingMsg::FromStream(Frame::Hello) => {
                bail!("Hello frame received after stream established")
            }
            FlushOutgoingMsg::FromStream(Frame::AckTransfer) => {
                bail!("AckTransfer received before BeginTransfer sent")
            }
            FlushOutgoingMsg::FromStream(Frame::EndTransfer) => {
                bail!("EndTransfer received on a regular stream")
            }
            FlushOutgoingMsg::FromStream(Frame::Shutdown(r)) => {
                bail!("Stream shutdown during transfer: {:?}", r)
            }
        }
    }
}

async fn drain_original_stream<Hdl: 'static + Proxyable>(
    proxy: &Proxy<Hdl>,
    original_transfer_key: TransferKey,
    stream_writer: StreamWriter<Hdl::Message>,
    mut stream_reader: StreamReader<Hdl::Message>,
    drain_stream_id: u64,
    stream_ref_sender: StreamRefSender,
) -> Result<(), Error> {
    let endpoint = stream_reader.endpoint();
    let debug_id = stream_writer.debug_id();
    loop {
        let r = stream_reader.next().await;
        log::trace!(
            "[PROXY {:?} {:?}] drain_original_stream gets {:?}",
            proxy.hdl().hdl(),
            debug_id,
            r
        );
        match r {
            Ok(Frame::Hello) => {
                bail!("Hello frame received after stream established");
            }
            Ok(Frame::Data(mut message)) => {
                // We received an incoming message - place it on the handle pair for a moment until
                // we can send it to the drain stream.
                proxy.write_to_handle(&mut message).await?;
            }
            Ok(Frame::BeginTransfer(new_destination_node, new_transfer_key)) => {
                // Uh oh! The other end has independently decided to transfer ownership of this
                // handle. We use the quic endpoint to determine behavior (such that each end makes
                // a consistent decision) - clients start a new stream to the target, servers await
                // that stream. We've flushed messages, so we need to do an ack dance too.
                log::trace!(
                    "[PROXY {:?} {:?}] remote is also transferring with key {:?}; this is the {:?}",
                    proxy.hdl().hdl(),
                    debug_id,
                    new_transfer_key,
                    endpoint
                );
                match endpoint {
                    Endpoint::Client => {
                        stream_ref_sender.draining_initiate(
                            drain_stream_id,
                            new_destination_node,
                            new_transfer_key,
                        )?;
                        stream_writer.send_ack_transfer().await?;
                        return stream_reader.expect_ack_transfer().await;
                    }
                    Endpoint::Server => {
                        stream_ref_sender.draining_await(drain_stream_id, original_transfer_key)?;
                        stream_writer.send_ack_transfer().await?;
                        return stream_reader.expect_ack_transfer().await;
                    }
                }
            }
            Ok(Frame::AckTransfer) => {
                stream_writer.send_shutdown(Ok(())).await?;
                return stream_ref_sender.draining_await(drain_stream_id, original_transfer_key);
            }
            Ok(Frame::EndTransfer) => bail!("EndTransfer received on a regular stream"),
            Ok(Frame::Shutdown(r)) => bail!("Stream shutdown during transfer: {:?}", r),
            Err(e) => return Err(e),
        }
    }
}
