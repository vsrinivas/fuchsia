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
    crate::test_util::NodeIdGenerator,
    crate::{
        log_errors, new_quic_link, Endpoint, LinkReceiver, LinkSender, ListPeersContext, NodeId,
        QuicReceiver, Router,
    },
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_overnet::{Peer, ServiceProviderMarker},
    fuchsia_async::Task,
    futures::prelude::*,
    parking_lot::Mutex,
    std::collections::VecDeque,
    std::pin::Pin,
    std::sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
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
    AttachSocketLink(fidl::Socket, fidl_fuchsia_overnet_protocol::SocketLinkOptions),
    NewLink(NodeId, Box<dyn NewLinkRunner>),
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
        log::info!("{:?} SPAWN OVERNET", node.node_id());
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

    fn new_link(
        &self,
        peer_node_id: NodeId,
        runner: impl 'static + NewLinkRunner,
    ) -> Result<(), fidl::Error> {
        self.send(OvernetCommand::NewLink(peer_node_id, Box::new(runner)))
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
        OvernetCommand::AttachSocketLink(socket, options) => {
            node.run_socket_link(socket, options).await
        }
        OvernetCommand::NewLink(peer_node_id, runner) => {
            let (tx, rx) = node.new_link(peer_node_id, Box::new(|| None)).await?;
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
    log::info!("{:?} RUN OVERNET", node_id);
    let lpc = Arc::new(node.new_list_peers_context());
    // Run application loop
    rx.for_each_concurrent(None, move |cmd| {
        let node = node.clone();
        let lpc = lpc.clone();
        async move {
            let cmd_text = format!("{:?}", cmd);
            let cmd_id = NEXT_CMD_ID.fetch_add(1, Ordering::Relaxed);
            log::info!("{:?} CMD[{}] START: {}", node_id, cmd_id, cmd_text);
            if let Err(e) = run_overnet_command(node, lpc, cmd).await {
                log::info!(
                    "{:?} CMD[{}] FAILED: {} with error: {:?}",
                    node_id,
                    cmd_id,
                    cmd_text,
                    e
                );
            } else {
                log::info!("{:?} CMD[{}] SUCCEEDED: {}", node_id, cmd_id, cmd_text);
            }
        }
    })
    .await;
    log::info!("{:?} DONE OVERNET", node_id);
    Ok(())
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyInterface implementations

struct MeshController(Arc<Overnet>);

impl MeshControllerProxyInterface for MeshController {
    fn attach_socket_link(
        &self,
        socket: fidl::Socket,
        options: fidl_fuchsia_overnet_protocol::SocketLinkOptions,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::AttachSocketLink(socket, options))
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
        tx.write(&write).await?;
    }
}

/// Connect two test overnet instances, with mutating functions between the stream socket connecting
/// them - to test recovery mechanisms.
pub async fn connect_with_mutator(
    a: Arc<Overnet>,
    b: Arc<Overnet>,
    mutator_ab: Box<dyn Send + FnMut(Vec<u8>) -> Vec<u8>>,
    mutator_ba: Box<dyn Send + FnMut(Vec<u8>) -> Vec<u8>>,
    opt: Box<dyn Send + Fn() -> fidl_fuchsia_overnet_protocol::SocketLinkOptions>,
) -> Result<(), Error> {
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (a1, a2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (b1, b2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (arx, atx) = fidl::AsyncSocket::from_socket(a2)?.split();
    let (brx, btx) = fidl::AsyncSocket::from_socket(b1)?.split();
    a.attach_socket_link(a1, opt())?;
    b.attach_socket_link(b2, opt())?;
    futures::future::try_join(
        copy_with_mutator(brx, atx, mutator_ab),
        copy_with_mutator(arx, btx, mutator_ba),
    )
    .await
    .map(drop)
}

/// Connect two test overnet instances with a stream socket.
pub fn connect(a: &Arc<Overnet>, b: &Arc<Overnet>) -> Result<(), Error> {
    log::info!("Connect {:?} and {:?}", a.node_id(), b.node_id());
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (sa, sb) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    a.attach_socket_link(sa, fidl_fuchsia_overnet_protocol::SocketLinkOptions::empty())?;
    b.attach_socket_link(sb, fidl_fuchsia_overnet_protocol::SocketLinkOptions::empty())?;
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
            let (tx, rx) = new_quic_link(link_sender, link_receiver, self.endpoint).await?;
            self.tx
                .send(rx)
                .map_err(|_| anyhow::format_err!("failed to send quic link to other end"))?;
            let rx = self.rx.await?;
            let mut frame = [0u8; 1400];
            while let Some(n) = tx.next_send(&mut frame).await? {
                rx.received_packet(&mut frame[..n]).await?;
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
    a.new_link(b.node_id(), QuicLinkRunner { tx: atx, rx: arx, endpoint: Endpoint::Client })?;
    b.new_link(a.node_id(), QuicLinkRunner { tx: btx, rx: brx, endpoint: Endpoint::Server })?;
    Ok(())
}

pub async fn connect_with_interspersed_log_messages(
    a: Arc<Overnet>,
    b: Arc<Overnet>,
) -> Result<(), Error> {
    let mut log_messages = VecDeque::from(vec![
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff100a1900 (arm64_perfmon) at level 0x90000, flags 0x1\n",
"[00001.245] 00000:00000> Trying to start cpu 1 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 2 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 3 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 4 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 5 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 6 returned: 0\n",
"[00001.245] 00000:00000> Trying to start cpu 7 returned: 0\n",
"[00001.245] 00000:00000> initializing target\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10092fc0 (platform_dev_init) at level 0xa0000, flags 0x1\n",
"[00001.245] 00000:00000> UART: started IRQ driven TX\n",
"[00001.245] 00000:00000> moving to last init level\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10030204 (debuglog) at level 0xb0000, flags 0x1\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10098934 (kcounters) at level 0xb0000, flags 0x1\n",
"[00001.245] 00000:00000> INIT: cpu 0, calling hook 0xffffffff10104270 (ktrace) at level 0xbffff, flags 0x1\n",
"[00001.246] 00000:00000> OOM: memory availability state 1\n",
"[00001.300] 00000:00000> INIT: cpu 3, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.300] 00000:00000> ARM cpu 3: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000003 aff 0:0:0:3\n",
"[00001.300] 00000:00000> entering scheduler on cpu 3\n",
"[00001.314] 00000:00000> INIT: cpu 2, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.315] 00000:00000> ARM cpu 2: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000002 aff 0:0:0:2\n",
"[00001.315] 00000:00000> entering scheduler on cpu 2\n",
"[00001.330] 00000:00000> INIT: cpu 7, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.330] 00000:00000> ARM cpu 7: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000007 aff 0:0:0:7\n",
"[00001.330] 00000:00000> entering scheduler on cpu 7\n",
"[00001.345] 00000:00000> INIT: cpu 5, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.345] 00000:00000> ARM cpu 5: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000005 aff 0:0:0:5\n",
"[00001.345] 00000:00000> entering scheduler on cpu 5\n",
"[00001.363] 00000:00000> INIT: cpu 4, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.363] 00000:00000> ARM cpu 4: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000004 aff 0:0:0:4\n",
"[00001.363] 00000:00000> entering scheduler on cpu 4\n",
"[00001.378] 00000:00000> INIT: cpu 1, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.378] 00000:00000> ARM cpu 1: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000001 aff 0:0:0:1\n",
"[00001.378] 00000:00000> entering scheduler on cpu 1\n",
"[00001.393] 00000:00000> INIT: cpu 6, calling hook 0xffffffff10097ac8 (arm_generic_timer_init_secondary_cpu) at level 0x7ffff, flags 0x2\n",
"[00001.393] 00000:00000> ARM cpu 6: midr 0x431f0af1 'Cavium CN99XX r1p1' mpidr 0x80000006 aff 0:0:0:6\n",
"[00001.393] 00000:00000> entering scheduler on cpu 6\n",
"[00001.721] 00000:00000> ktrace: buffer at 0xffff008713666000 (33554432 bytes)\n",
"[00001.721] 00000:00000> INIT: cpu 0, calling hook 0xffffffff100047f8 (kernel_shell) at level 0xc0000, flags 0x1\n",
    ]);
    let mut last_was_nl = false;
    let mutator = move |mut bytes: Vec<u8>| {
        let mutate = last_was_nl;
        if bytes.len() > 0 {
            last_was_nl = bytes[bytes.len() - 1] == 10;
        }
        if mutate {
            assert_ne!(bytes.len(), 0);
            let prefix = log_messages.pop_front().unwrap();
            let mut output = prefix.to_string().into_bytes();
            output.append(&mut bytes);
            log_messages.push_back(prefix);
            output
        } else {
            bytes
        }
    };
    connect_with_mutator(
        a.clone(),
        b.clone(),
        Box::new(|v| v),
        Box::new(mutator),
        Box::new(|| fidl_fuchsia_overnet_protocol::SocketLinkOptions {
            bytes_per_second: Some(1000),
            ..fidl_fuchsia_overnet_protocol::SocketLinkOptions::empty()
        }),
    )
    .await
}
