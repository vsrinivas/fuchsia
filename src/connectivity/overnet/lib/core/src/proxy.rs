// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod main;
pub(crate) mod spawn;
mod xfer;

use crate::framed_stream::{FramedStreamWriter, MessageStats};
use crate::labels::{NodeId, TransferKey};
use crate::proxy_stream::StreamWriter;
use crate::proxyable_handle::{Proxyable, ProxyableHandle};
use crate::router::Router;
use anyhow::{format_err, Error};
use fidl_fuchsia_overnet_protocol::{StreamId, StreamRef, TransferInitiator, TransferWaiter};
use fuchsia_zircon_status as zx_status;
use futures::{future::FusedFuture, prelude::*};
use std::pin::Pin;
use std::rc::{Rc, Weak};
use std::task::{Context, Poll};

#[derive(Debug)]
pub(crate) enum RemoveFromProxyTable {
    InitiateTransfer {
        paired_handle: fidl::Handle,
        drain_stream: FramedStreamWriter,
        stream_ref_sender: StreamRefSender,
    },
    Dropped,
}

impl RemoveFromProxyTable {
    pub(crate) fn is_dropped(&self) -> bool {
        match self {
            RemoveFromProxyTable::Dropped => true,
            _ => false,
        }
    }
}

pub(crate) struct ProxyTransferInitiationReceiver(
    Pin<Box<dyn FusedFuture<Output = Result<RemoveFromProxyTable, Error>>>>,
);

impl Future for ProxyTransferInitiationReceiver {
    type Output = Result<RemoveFromProxyTable, Error>;
    fn poll(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        self.0.as_mut().poll(ctx)
    }
}

impl FusedFuture for ProxyTransferInitiationReceiver {
    fn is_terminated(&self) -> bool {
        self.0.is_terminated()
    }
}

impl std::fmt::Debug for ProxyTransferInitiationReceiver {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        "_".fmt(f)
    }
}

impl ProxyTransferInitiationReceiver {
    pub(crate) fn new(
        f: impl 'static + Future<Output = Result<RemoveFromProxyTable, Error>>,
    ) -> Self {
        Self(Box::pin(f.fuse()))
    }
}

pub(crate) struct StreamRefSender {
    chan: futures::channel::oneshot::Sender<StreamRef>,
}

impl std::fmt::Debug for StreamRefSender {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        "StreamRefSender".fmt(f)
    }
}

impl StreamRefSender {
    pub(crate) fn new() -> (Self, futures::channel::oneshot::Receiver<StreamRef>) {
        let (tx, rx) = futures::channel::oneshot::channel();
        (Self { chan: tx }, rx)
    }

    fn send(self, stream_ref: StreamRef) -> Result<(), Error> {
        Ok(self.chan.send(stream_ref).map_err(|_| format_err!("Failed sending StreamRef"))?)
    }

    pub(crate) fn draining_initiate(
        self,
        stream_id: u64,
        new_destination_node: NodeId,
        transfer_key: TransferKey,
    ) -> Result<(), Error> {
        Ok(self.send(StreamRef::TransferInitiator(TransferInitiator {
            stream_id: StreamId { id: stream_id },
            new_destination_node: new_destination_node.into(),
            transfer_key,
        }))?)
    }

    pub(crate) fn draining_await(
        self,
        stream_id: u64,
        transfer_key: TransferKey,
    ) -> Result<(), Error> {
        Ok(self.send(StreamRef::TransferWaiter(TransferWaiter {
            stream_id: StreamId { id: stream_id },
            transfer_key,
        }))?)
    }
}

#[derive(Debug)]
pub(crate) struct Proxy<Hdl: Proxyable> {
    hdl: Option<ProxyableHandle<Hdl>>,
}

impl<Hdl: Proxyable> Drop for Proxy<Hdl> {
    fn drop(&mut self) {
        if let Some(hdl) = self.hdl.take() {
            if let Some(router) = Weak::upgrade(hdl.router()) {
                router.remove_proxied(hdl.into_fidl_handle().unwrap()).unwrap();
            }
        }
    }
}

impl<Hdl: 'static + Proxyable> Proxy<Hdl> {
    fn new(hdl: Hdl, router: Weak<Router>, stats: Rc<MessageStats>) -> Rc<Self> {
        Rc::new(Self { hdl: Some(ProxyableHandle::new(hdl, router, stats)) })
    }

    fn hdl(&self) -> &ProxyableHandle<Hdl> {
        self.hdl.as_ref().unwrap()
    }

    async fn write_to_handle(&self, msg: &mut Hdl::Message) -> Result<(), zx_status::Status> {
        self.hdl().write(msg).await
    }

    async fn read_from_handle(&self, msg: &mut Hdl::Message) -> Result<(), zx_status::Status> {
        self.hdl().read(msg).await
    }

    async fn drain_handle_to_stream(
        &self,
        stream_writer: &mut StreamWriter<Hdl::Message>,
    ) -> Result<(), Error> {
        self.hdl().drain_to_stream(stream_writer).await
    }
}
