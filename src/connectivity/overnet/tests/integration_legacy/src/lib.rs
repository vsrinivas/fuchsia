// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A wrapping of Overnet that is particularly useful for overnet integration tests that don't
//! depend on the surrounding environment (and can thus be run like self contained unit tests)

#![cfg(test)]

mod drop;
mod echo;
mod triangle;

use {
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_overnet::{Peer, ServiceProviderMarker},
    fuchsia_async::Task,
    futures::prelude::*,
    overnet_core::{
        log_errors, Endpoint, LinkReceiver, LinkSender, ListPeersContext, NodeId, NodeIdGenerator,
        Router,
    },
    parking_lot::Mutex,
    std::pin::Pin,
    std::sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
    stream_link::run_stream_link,
    udp_link::{new_quic_link, QuicReceiver},
};

pub use fidl_fuchsia_overnet::MeshControllerProxyInterface;
pub use fidl_fuchsia_overnet::ServiceConsumerProxyInterface;
pub use fidl_fuchsia_overnet::ServicePublisherProxyInterface;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Overnet <-> API bindings

#[derive(Debug)]
enum OvernetCommand {
    ListPeers(futures::channel::oneshot::Sender<Vec<Peer>>),
    RegisterService(String, ClientEnd<ServiceProviderMarker>),
    ConnectToService(NodeId, String, fidl::Channel),
    AttachSocketLink(fidl::Socket),
    NewLink(Box<dyn NewLinkRunner>),
}

trait NewLinkRunner: Send {
    fn run(
        self: Box<Self>,
        tx: LinkSender,
        rx: LinkReceiver,
    ) -> Pin<Box<dyn Send + Future<Output = Result<(), Error>>>>;
}

impl std::fmt::Debug for dyn NewLinkRunner {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        "link_runner".fmt(f)
    }
}

/// Overnet implementation for integration tests
pub struct Overnet {
    tx: Mutex<futures::channel::mpsc::UnboundedSender<OvernetCommand>>,
    node_id: NodeId,
    // Main loop for the Overnet instance - once the object is dropped, the loop can stop.
    _task: Task<()>,
}

impl Overnet {
    /// Create a new instance
    pub fn new(node_id_gen: &mut NodeIdGenerator) -> Result<Arc<Overnet>, Error> {
        let node = node_id_gen.new_router()?;

        let (tx, rx) = futures::channel::mpsc::unbounded();
        tracing::info!(node_id = node.node_id().0, "SPAWN OVERNET");
        let tx = Mutex::new(tx);
        Ok(Arc::new(Overnet {
            tx,
            node_id: node.node_id(),
            _task: Task::spawn(log_errors(run_overnet(node, rx), "Main loop failed")),
        }))
    }

    fn send(&self, cmd: OvernetCommand) -> Result<(), fidl::Error> {
        Ok(self.tx.lock().unbounded_send(cmd).map_err(|_| fidl::Error::Invalid)?)
    }

    /// Produce a proxy that acts as a connection to Overnet under the ServiceConsumer role
    pub fn connect_as_service_consumer(
        self: &Arc<Self>,
    ) -> Result<impl ServiceConsumerProxyInterface, Error> {
        Ok(ServiceConsumer(self.clone()))
    }

    /// Produce a proxy that acts as a connection to Overnet under the ServicePublisher role
    pub fn connect_as_service_publisher(
        self: &Arc<Self>,
    ) -> Result<impl ServicePublisherProxyInterface, Error> {
        Ok(ServicePublisher(self.clone()))
    }

    /// Produce a proxy that acts as a connection to Overnet under the MeshController role
    pub fn connect_as_mesh_controller(
        self: &Arc<Self>,
    ) -> Result<impl MeshControllerProxyInterface, Error> {
        Ok(MeshController(self.clone()))
    }

    pub fn node_id(&self) -> NodeId {
        self.node_id
    }

    fn new_link(&self, runner: impl 'static + NewLinkRunner) -> Result<(), fidl::Error> {
        self.send(OvernetCommand::NewLink(Box::new(runner)))
    }
}

async fn run_overnet_command(
    node: Arc<Router>,
    lpc: Arc<ListPeersContext>,
    cmd: OvernetCommand,
) -> Result<(), Error> {
    match cmd {
        OvernetCommand::ListPeers(sender) => {
            let peers = lpc.list_peers().await?;
            let _ = sender.send(peers);
            Ok(())
        }
        OvernetCommand::RegisterService(service_name, provider) => {
            node.register_service(service_name, provider).await
        }
        OvernetCommand::ConnectToService(node_id, service_name, channel) => {
            node.connect_to_service(node_id, &service_name, channel).await
        }
        OvernetCommand::AttachSocketLink(socket) => {
            let (mut rx, mut tx) = fidl::AsyncSocket::from_socket(socket)?.split();
            run_stream_link(node, None, &mut rx, &mut tx, Default::default(), Box::new(|| None))
                .await
        }
        OvernetCommand::NewLink(runner) => {
            let (tx, rx) = node.new_link(Default::default(), Box::new(|| None));
            runner.run(tx, rx).await
        }
    }
}

static NEXT_CMD_ID: AtomicU64 = AtomicU64::new(0);

async fn run_overnet(
    node: Arc<Router>,
    rx: futures::channel::mpsc::UnboundedReceiver<OvernetCommand>,
) -> Result<(), Error> {
    let node_id = node.node_id();
    tracing::info!(?node_id, "RUN OVERNET");
    let lpc = Arc::new(node.new_list_peers_context());
    // Run application loop
    rx.for_each_concurrent(None, move |cmd| {
        let node = node.clone();
        let lpc = lpc.clone();
        async move {
            let cmd_text = format!("{:?}", cmd);
            let cmd_id = NEXT_CMD_ID.fetch_add(1, Ordering::Relaxed);
            tracing::info!(node_id = node_id.0, cmd = cmd_id, "START: {}", cmd_text);
            if let Err(e) = run_overnet_command(node, lpc, cmd).await {
                tracing::info!(
                    node_id = node_id.0,
                    cmd = cmd_id,
                    "{} with error: {:?}",
                    cmd_text,
                    e
                );
            } else {
                tracing::info!(node_id = node_id.0, cmd = cmd_id, "SUCCEEDED: {}", cmd_text);
            }
        }
    })
    .await;
    tracing::info!(node_id = node_id.0, "DONE OVERNET");
    Ok(())
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyInterface implementations

struct MeshController(Arc<Overnet>);

impl MeshControllerProxyInterface for MeshController {
    fn attach_socket_link(&self, socket: fidl::Socket) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::AttachSocketLink(socket))
    }
}

struct ServicePublisher(Arc<Overnet>);

impl ServicePublisherProxyInterface for ServicePublisher {
    fn publish_service(
        &self,
        service_name: &str,
        provider: ClientEnd<ServiceProviderMarker>,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::RegisterService(service_name.to_string(), provider))
    }
}

struct ServiceConsumer(Arc<Overnet>);

use futures::{
    channel::oneshot,
    future::{err, Either, MapErr, Ready},
};

fn bad_recv(_: oneshot::Canceled) -> fidl::Error {
    fidl::Error::PollAfterCompletion
}

impl ServiceConsumerProxyInterface for ServiceConsumer {
    type ListPeersResponseFut = Either<
        MapErr<oneshot::Receiver<Vec<Peer>>, fn(oneshot::Canceled) -> fidl::Error>,
        Ready<Result<Vec<Peer>, fidl::Error>>,
    >;

    fn list_peers(&self) -> Self::ListPeersResponseFut {
        let (sender, receiver) = futures::channel::oneshot::channel();
        if let Err(e) = self.0.send(OvernetCommand::ListPeers(sender)) {
            Either::Right(err(e))
        } else {
            // Returning an error from the receiver means that the sender disappeared without
            // sending a response, a condition we explicitly disallow.
            Either::Left(receiver.map_err(bad_recv))
        }
    }

    fn connect_to_service(
        &self,
        node: &mut fidl_fuchsia_overnet_protocol::NodeId,
        service_name: &str,
        chan: fidl::Channel,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::ConnectToService(
            node.id.into(),
            service_name.to_string(),
            chan,
        ))
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Connect Overnet nodes to each other

async fn copy_with_mutator(
    mut rx: impl AsyncRead + std::marker::Unpin,
    mut tx: impl AsyncWrite + std::marker::Unpin,
    mut mutator: impl FnMut(Vec<u8>) -> Vec<u8>,
) -> Result<(), Error> {
    let mut buf = [0u8; 1024];
    loop {
        let n = rx.read(&mut buf).await?;
        if n == 0 {
            return Ok(());
        }
        let write = mutator(Vec::from(&buf[0..n]));
        tx.write_all(&write).await?;
    }
}

/// Connect two test overnet instances, with mutating functions between the stream socket connecting
/// them - to test recovery mechanisms.
pub async fn connect_with_mutator(
    a: Arc<Overnet>,
    b: Arc<Overnet>,
    mutator_ab: Box<dyn Send + FnMut(Vec<u8>) -> Vec<u8>>,
    mutator_ba: Box<dyn Send + FnMut(Vec<u8>) -> Vec<u8>>,
) -> Result<(), Error> {
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (a1, a2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (b1, b2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (arx, atx) = fidl::AsyncSocket::from_socket(a2)?.split();
    let (brx, btx) = fidl::AsyncSocket::from_socket(b1)?.split();
    a.attach_socket_link(a1)?;
    b.attach_socket_link(b2)?;
    futures::future::try_join(
        copy_with_mutator(brx, atx, mutator_ab),
        copy_with_mutator(arx, btx, mutator_ba),
    )
    .await
    .map(drop)
}

/// Connect two test overnet instances with a stream socket.
pub fn connect(a: &Arc<Overnet>, b: &Arc<Overnet>) -> Result<(), Error> {
    tracing::info!(a = a.node_id().0, b = b.node_id().0, "Connect nodes");
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (sa, sb) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    a.attach_socket_link(sa)?;
    b.attach_socket_link(sb)?;
    Ok(())
}

struct QuicLinkRunner {
    tx: futures::channel::oneshot::Sender<QuicReceiver>,
    rx: futures::channel::oneshot::Receiver<QuicReceiver>,
    endpoint: Endpoint,
}

impl NewLinkRunner for QuicLinkRunner {
    fn run(
        self: Box<Self>,
        link_sender: LinkSender,
        link_receiver: LinkReceiver,
    ) -> Pin<Box<dyn Send + Future<Output = Result<(), Error>>>> {
        async move {
            let (tx, rx, _) = new_quic_link(link_sender, link_receiver, self.endpoint).await?;
            self.tx
                .send(rx)
                .map_err(|_| anyhow::format_err!("failed to send quic link to other end"))?;
            let rx = self.rx.await?;
            let mut frame = [0u8; 1400];
            while let Some(n) = tx.next_send(&mut frame).await? {
                rx.received_frame(&mut frame[..n]).await;
            }
            Ok(())
        }
        .boxed()
    }
}

// Connect two test overnet instances with a QUIC based link
pub fn connect_with_quic(a: &Arc<Overnet>, b: &Arc<Overnet>) -> Result<(), Error> {
    let (atx, brx) = futures::channel::oneshot::channel();
    let (btx, arx) = futures::channel::oneshot::channel();
    a.new_link(QuicLinkRunner { tx: atx, rx: arx, endpoint: Endpoint::Client })?;
    b.new_link(QuicLinkRunner { tx: btx, rx: brx, endpoint: Endpoint::Server })?;
    Ok(())
}
