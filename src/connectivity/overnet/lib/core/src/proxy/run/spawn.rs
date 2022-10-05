// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Factory functions for proxies - one each for sending a handle and receiving a handle.

use super::super::{
    handle::{IntoProxied, ProxyableHandle, ProxyableRW},
    Proxy, ProxyTransferInitiationReceiver,
};
use crate::handle_info::WithRights;
use crate::peer::{FramedStreamReader, FramedStreamWriter, MessageStats, PeerConnRef};
use crate::router::{FoundTransfer, OpenedTransfer, Router};
use anyhow::{format_err, Error};
use fidl_fuchsia_overnet_protocol::{StreamId, StreamRef, TransferInitiator, TransferWaiter};
use std::future::Future;
use std::sync::{Arc, Weak};

pub(crate) async fn send<Hdl: 'static + for<'a> ProxyableRW<'a>>(
    hdl: Hdl,
    initiate_transfer: ProxyTransferInitiationReceiver,
    stream_writer: FramedStreamWriter,
    stream_reader: FramedStreamReader,
    stats: Arc<MessageStats>,
    router: Weak<Router>,
) -> Result<(), Error> {
    super::main::run_main_loop(
        Proxy::new(hdl, router, stats),
        initiate_transfer,
        stream_writer,
        None,
        stream_reader,
    )
    .await
}

// Start receiving from some stream for a channel.
// Returns a handle, and an optional future that runs the proxying activity (or none if no proxying is occurring).
pub(crate) async fn recv<Hdl, CreateType>(
    create_handles: impl FnOnce() -> Result<(CreateType, CreateType), Error>,
    rights: CreateType::Rights,
    initiate_transfer: ProxyTransferInitiationReceiver,
    stream_ref: StreamRef,
    conn: PeerConnRef<'_>,
    stats: Arc<MessageStats>,
    router: Weak<Router>,
) -> Result<(fidl::Handle, Option<impl Send + Future<Output = Result<(), Error>>>), Error>
where
    Hdl: 'static + for<'a> ProxyableRW<'a>,
    CreateType: fidl::HandleBased + IntoProxied<Proxied = Hdl> + std::fmt::Debug + WithRights,
{
    Ok(match stream_ref {
        StreamRef::Creating(StreamId { id: stream_id }) => {
            let (app_chan, overnet_chan) = create_handles()?;
            let app_chan = app_chan.with_rights(rights)?;
            let (stream_writer, stream_reader) = conn.bind_id(stream_id).await?;
            let overnet_chan = overnet_chan.into_proxied()?;
            (
                app_chan.into_handle(),
                Some(super::main::run_main_loop(
                    Proxy::new(overnet_chan, router, stats),
                    initiate_transfer,
                    stream_writer,
                    None,
                    stream_reader,
                )),
            )
        }
        StreamRef::TransferInitiator(TransferInitiator {
            stream_id: StreamId { id: stream_id },
            new_destination_node,
            transfer_key,
        }) => {
            let (app_chan, overnet_chan) = create_handles()?;
            let app_chan = app_chan.with_rights(rights)?;
            let initial_stream_reader: FramedStreamReader =
                conn.bind_uni_id(stream_id).await?.into();
            let opened_transfer = Weak::upgrade(&router)
                .ok_or_else(|| format_err!("No router to handle draining stream ref"))?
                .open_transfer(
                    new_destination_node.into(),
                    transfer_key,
                    overnet_chan.into_handle(),
                )
                .await?;
            match opened_transfer {
                OpenedTransfer::Fused => {
                    let app_chan = app_chan.into_proxied()?;
                    (
                        ProxyableHandle::new(app_chan, router, stats)
                            .drain_stream_to_handle(initial_stream_reader)
                            .await?,
                        None,
                    )
                }
                OpenedTransfer::Remote(stream_writer, stream_reader, overnet_chan) => (
                    app_chan.into_handle(),
                    Some(super::main::run_main_loop(
                        Proxy::new(Hdl::from_fidl_handle(overnet_chan)?, router, stats),
                        initiate_transfer,
                        stream_writer,
                        Some(initial_stream_reader),
                        stream_reader,
                    )),
                ),
            }
        }
        StreamRef::TransferWaiter(TransferWaiter {
            stream_id: StreamId { id: stream_id },
            transfer_key,
        }) => {
            let initial_stream_reader: FramedStreamReader =
                conn.bind_uni_id(stream_id).await?.into();
            let found_transfer = Weak::upgrade(&router)
                .ok_or_else(|| format_err!("No router to handle draining stream ref"))?
                .find_transfer(transfer_key)
                .await?;
            match found_transfer {
                FoundTransfer::Fused(handle) => {
                    let handle = Hdl::from_fidl_handle(handle)?;
                    (
                        ProxyableHandle::new(handle, router, stats)
                            .drain_stream_to_handle(initial_stream_reader)
                            .await?,
                        None,
                    )
                }
                FoundTransfer::Remote(stream_writer, stream_reader) => {
                    let (app_chan, overnet_chan) = create_handles()?;
                    (
                        app_chan.with_rights(rights)?.into_handle(),
                        Some(super::main::run_main_loop(
                            Proxy::new(overnet_chan.into_proxied()?, router, stats),
                            initiate_transfer,
                            stream_writer,
                            Some(initial_stream_reader),
                            stream_reader,
                        )),
                    )
                }
            }
        }
    })
}
