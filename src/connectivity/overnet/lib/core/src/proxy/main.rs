// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Main loops (and associated spawn functions) for proxying... handles moving data from one point
//! to another, and calling into crate::proxy::xfer once a handle transfer is required.

use super::{
    xfer, Proxy, ProxyTransferInitiationReceiver, Proxyable, RemoveFromProxyTable, StreamRefSender,
};
use crate::async_quic::StreamProperties;
use crate::framed_stream::{FramedStreamReader, FramedStreamWriter};
use crate::future_help::log_errors;
use crate::labels::{NodeId, TransferKey};
use crate::proxy_stream::{
    Frame, StreamReader, StreamReaderBinder, StreamWriter, StreamWriterBinder,
};
use crate::runtime::spawn;
use anyhow::{bail, format_err, Context as _, Error};
use fuchsia_zircon_status as zx_status;
use futures::{prelude::*, select};
use std::rc::Rc;

// We run two tasks to proxy a handle - one to handle handle->stream, the other to handle
// stream->handle. When we want to perform a transfer operation we end up wanting to think about
// just one task, so we provide a join operation here.
#[derive(Debug)]
enum FinishProxyLoopAction<Hdl: Proxyable> {
    InitiateTransfer {
        paired_handle: fidl::Handle,
        drain_stream: FramedStreamWriter,
        stream_ref_sender: StreamRefSender,
        stream_reader: StreamReader<Hdl::Message>,
    },
    FollowTransfer {
        initiate_transfer: ProxyTransferInitiationReceiver,
        new_destination_node: NodeId,
        transfer_key: TransferKey,
        stream_reader: StreamReader<Hdl::Message>,
    },
    Shutdown {
        result: Result<(), zx_status::Status>,
        stream_reader: StreamReader<Hdl::Message>,
    },
}

struct FinishProxyLoopSender<Hdl: Proxyable> {
    chan: futures::channel::oneshot::Sender<FinishProxyLoopAction<Hdl>>,
}
type FinishProxyLoopReceiver<Hdl> = futures::channel::oneshot::Receiver<FinishProxyLoopAction<Hdl>>;

impl<Hdl: 'static + Proxyable> FinishProxyLoopSender<Hdl> {
    fn and_then(self, then: FinishProxyLoopAction<Hdl>) -> Result<(), Error> {
        Ok(self.chan.send(then).map_err(|_| format_err!("Join channel broken"))?)
    }

    // This join is to initiate a new transfer.
    fn and_then_initiate(
        self,
        paired_handle: fidl::Handle,
        drain_stream: FramedStreamWriter,
        stream_ref_sender: StreamRefSender,
        stream_reader: StreamReader<Hdl::Message>,
    ) -> Result<(), Error> {
        self.and_then(FinishProxyLoopAction::InitiateTransfer {
            paired_handle,
            drain_stream,
            stream_ref_sender,
            stream_reader,
        })
    }

    // This join is to follow a transfer initiated by the remote end.
    fn and_then_follow(
        self,
        initiate_transfer: ProxyTransferInitiationReceiver,
        new_destination_node: NodeId,
        transfer_key: TransferKey,
        stream_reader: StreamReader<Hdl::Message>,
    ) -> Result<(), Error> {
        self.and_then(FinishProxyLoopAction::FollowTransfer {
            initiate_transfer,
            new_destination_node,
            transfer_key,
            stream_reader,
        })
    }

    fn and_then_shutdown(
        self,
        result: Result<(), zx_status::Status>,
        stream_reader: StreamReader<Hdl::Message>,
    ) -> Result<(), Error> {
        self.and_then(FinishProxyLoopAction::Shutdown { result, stream_reader })
    }
}

fn new_task_joiner<Hdl: Proxyable>() -> (FinishProxyLoopSender<Hdl>, FinishProxyLoopReceiver<Hdl>) {
    let (tx, rx) = futures::channel::oneshot::channel();
    (FinishProxyLoopSender { chan: tx }, rx)
}

// Spawn a proxy (two tasks, one for each direction of proxying).
pub(crate) fn spawn_main_loop<Hdl: 'static + Proxyable>(
    proxy: Rc<Proxy<Hdl>>,
    initiate_transfer: ProxyTransferInitiationReceiver,
    stream_writer: FramedStreamWriter,
    initial_stream_reader: Option<FramedStreamReader>,
    stream_reader: FramedStreamReader,
) -> Result<(), Error> {
    let (tx_join, rx_join) = new_task_joiner();
    let hdl = proxy.hdl();
    let mut stream_writer = stream_writer.bind(hdl);
    let initial_stream_reader = initial_stream_reader.map(|s| s.bind(hdl));
    let mut stream_reader = stream_reader.bind(hdl);
    spawn(log_errors(
        async move {
            futures::future::try_join(
                async {
                    if !stream_reader.is_initiator() {
                        stream_reader.expect_hello().await?;
                    } else {
                        stream_writer.send_hello().await?;
                    }
                    Ok::<(), Error>(())
                },
                async {
                    if let Some(initial_stream_reader) = initial_stream_reader {
                        drain(proxy.clone(), initial_stream_reader).await?;
                    }
                    Ok(())
                },
            )
            .await?;
            futures::future::try_join(
                stream_to_handle(proxy.clone(), initiate_transfer, stream_reader, tx_join),
                handle_to_stream(proxy, stream_writer, rx_join),
            )
            .await?;
            Ok(())
        },
        "Proxy loop failed",
    ));
    Ok(())
}

async fn handle_to_stream<Hdl: 'static + Proxyable>(
    proxy: Rc<Proxy<Hdl>>,
    mut stream: StreamWriter<Hdl::Message>,
    finish_proxy_loop: FinishProxyLoopReceiver<Hdl>,
) -> Result<(), Error> {
    let mut message = Default::default();
    let mut finish_proxy_loop = finish_proxy_loop.fuse();
    let finish_proxy_loop_action = loop {
        let r = select! {
            x = proxy.read_from_handle(&mut message).fuse() => x,
            x = finish_proxy_loop => break x
        };
        match r {
            Ok(()) => stream.send_data(&mut message).await?,
            Err(zx_status::Status::PEER_CLOSED) => {
                stream.send_shutdown(Ok(())).await?;
                return Ok(());
            }
            Err(x) => {
                stream.send_shutdown(Err(x)).await?;
                return Err(x.into());
            }
        }
    };
    log::trace!("finish_proxy_loop_action: stream={} {:?}", stream.id(), finish_proxy_loop_action);
    let proxy = Rc::try_unwrap(proxy).map_err(|_| format_err!("Proxy should be isolated"))?;
    match finish_proxy_loop_action {
        Ok(FinishProxyLoopAction::InitiateTransfer {
            paired_handle,
            drain_stream,
            stream_ref_sender,
            stream_reader,
        }) => {
            xfer::initiate(
                proxy,
                paired_handle,
                stream,
                stream_reader,
                drain_stream,
                stream_ref_sender,
            )
            .await
        }
        Ok(FinishProxyLoopAction::FollowTransfer {
            initiate_transfer,
            new_destination_node,
            transfer_key,
            stream_reader,
        }) => {
            xfer::follow(
                proxy,
                initiate_transfer,
                stream,
                new_destination_node,
                transfer_key,
                stream_reader,
            )
            .await
        }
        Ok(FinishProxyLoopAction::Shutdown { result, stream_reader }) => {
            join_shutdown(proxy, stream, stream_reader, result).await
        }
        Err(futures::channel::oneshot::Canceled) => unreachable!(),
    }
}

async fn join_shutdown<Hdl: 'static + Proxyable>(
    proxy: Proxy<Hdl>,
    stream_writer: StreamWriter<Hdl::Message>,
    stream_reader: StreamReader<Hdl::Message>,
    result: Result<(), zx_status::Status>,
) -> Result<(), Error> {
    stream_writer.send_shutdown(result).await?;
    let _ = stream_reader.expect_shutdown(Ok(())).await;
    drop(proxy);
    Ok(())
}

async fn drain<Hdl: 'static + Proxyable>(
    proxy: Rc<Proxy<Hdl>>,
    mut drain_stream: StreamReader<Hdl::Message>,
) -> Result<(), Error> {
    loop {
        match drain_stream.next().await? {
            Frame::Data(message) => proxy.write_to_handle(message).await?,
            Frame::EndTransfer => break,
            Frame::BeginTransfer(_, _) => bail!("BeginTransfer on drain stream"),
            Frame::AckTransfer => bail!("AckTransfer on drain stream"),
            Frame::Hello => bail!("Hello frame disallowed on drain streams"),
            Frame::Shutdown(r) => bail!("Stream shutdown during drain: {:?}", r),
        }
    }
    Ok(())
}

async fn stream_to_handle<Hdl: 'static + Proxyable>(
    proxy: Rc<Proxy<Hdl>>,
    mut initiate_transfer: ProxyTransferInitiationReceiver,
    mut stream: StreamReader<Hdl::Message>,
    finish_proxy_loop: FinishProxyLoopSender<Hdl>,
) -> Result<(), Error> {
    let removed_from_proxy_table = loop {
        let frame = select! {
            x = stream.next().fuse() => x,
            x = initiate_transfer => break x,
        };
        match frame {
            Ok(Frame::Data(message)) => {
                if let Err(e) = proxy.write_to_handle(message).await {
                    let _ = finish_proxy_loop.and_then_shutdown(Err(e), stream);
                    match e {
                        zx_status::Status::PEER_CLOSED => return Ok(()),
                        _ => return Err(e.into()),
                    }
                }
            }
            Ok(Frame::BeginTransfer(new_destination_node, transfer_key)) => {
                if let Err(e) = finish_proxy_loop.and_then_follow(
                    initiate_transfer,
                    new_destination_node,
                    transfer_key,
                    stream,
                ) {
                    panic!("{:?}", e);
                }
                return Ok(());
            }
            Ok(Frame::EndTransfer) => bail!("Received EndTransfer on a regular stream"),
            Ok(Frame::AckTransfer) => bail!("Received AckTransfer before sending a BeginTransfer"),
            Ok(Frame::Hello) => bail!("Hello frame received after stream established"),
            Ok(Frame::Shutdown(result)) => {
                let _ = finish_proxy_loop.and_then_shutdown(result, stream);
                return result.context("Remote shutdown");
            }
            Err(e) => panic!("{:?}", e),
        }
    };
    match removed_from_proxy_table {
        Err(e) => panic!(e),
        Ok(RemoveFromProxyTable::Dropped) => unreachable!(),
        Ok(RemoveFromProxyTable::InitiateTransfer {
            paired_handle,
            drain_stream,
            stream_ref_sender,
        }) => Ok(finish_proxy_loop.and_then_initiate(
            paired_handle,
            drain_stream,
            stream_ref_sender,
            stream,
        )?),
    }
}
