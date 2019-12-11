// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A wrapping of Overnet that is particularly useful for overnet integration tests that don't
//! depend on the surrounding environment (and can thus be run like self contained unit tests)

mod echo;

use {
    failure::Error,
    fidl::endpoints::ClientEnd,
    fidl::Handle,
    fidl_fuchsia_overnet::{Peer, ServiceProviderMarker},
    futures::prelude::*,
    overnet_core::{LinkId, Node, NodeId, NodeOptions, NodeRuntime, RouterTime, SendHandle},
    parking_lot::Mutex,
    std::sync::Arc,
};

#[cfg(target_os = "fuchsia")]
use {fuchsia_zircon as zx, zx::AsHandleRef};

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
    AttachSocketLink(fidl::Socket, fidl_fuchsia_overnet::SocketLinkOptions),
}

/// Overnet implementation for integration tests
pub struct Overnet {
    tx: Mutex<futures::channel::mpsc::UnboundedSender<OvernetCommand>>,
}

impl Overnet {
    /// Create a new instance that uses `spawner` to dispatch work
    pub fn new(spawner: TestSpawner) -> Result<Arc<Overnet>, Error> {
        let (tx, rx) = futures::channel::mpsc::unbounded();
        let overnet_spawner = spawner.clone();
        spawner.spawn_local(async move {
            if let Err(e) = run_overnet(overnet_spawner, rx).await {
                log::warn!("Main loop failed: {:?}", e);
            }
        })?;
        let tx = Mutex::new(tx);
        Ok(Arc::new(Overnet { tx }))
    }

    fn send(&self, cmd: OvernetCommand) {
        self.tx.lock().unbounded_send(cmd).expect("Failed to send command to Overnet runner");
    }

    /// Produce a proxy that acts as a connection to Overnet under the ServiceConsumer role
    pub fn connect_as_service_consumer(
        self: Arc<Self>,
    ) -> Result<impl ServiceConsumerProxyInterface, Error> {
        Ok(ServiceConsumer(self.clone()))
    }

    /// Produce a proxy that acts as a connection to Overnet under the ServicePublisher role
    pub fn connect_as_service_publisher(
        self: Arc<Self>,
    ) -> Result<impl ServicePublisherProxyInterface, Error> {
        Ok(ServicePublisher(self.clone()))
    }

    /// Produce a proxy that acts as a connection to Overnet under the MeshController role
    pub fn connect_as_mesh_controller(
        self: Arc<Self>,
    ) -> Result<impl MeshControllerProxyInterface, Error> {
        Ok(MeshController(self.clone()))
    }
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
struct Time(std::time::Instant);

impl RouterTime for Time {
    type Duration = std::time::Duration;

    fn now() -> Self {
        Time(std::time::Instant::now())
    }

    fn after(t: Self, dt: Self::Duration) -> Self {
        Time(t.0 + dt)
    }
}

struct OvernetRuntime {
    spawner: TestSpawner,
}

impl NodeRuntime for OvernetRuntime {
    type Time = Time;
    type LinkId = ();
    const IMPLEMENTATION: fidl_fuchsia_overnet_protocol::Implementation =
        fidl_fuchsia_overnet_protocol::Implementation::HoistRustCrate;

    #[cfg(target_os = "fuchsia")]
    fn handle_type(hdl: &Handle) -> Result<SendHandle, Error> {
        match hdl.basic_info()?.object_type {
            zx::ObjectType::CHANNEL => Ok(SendHandle::Channel),
            _ => failure::bail!("Handle type not proxyable {:?}", hdl.basic_info()?.object_type),
        }
    }

    #[cfg(not(target_os = "fuchsia"))]
    fn handle_type(hdl: &Handle) -> Result<SendHandle, Error> {
        Ok(match hdl.handle_type() {
            fidl::FidlHdlType::Channel => SendHandle::Channel,
            fidl::FidlHdlType::Socket => unimplemented!(),
            fidl::FidlHdlType::Invalid => failure::bail!("Invalid handle"),
        })
    }

    fn spawn_local<F>(&mut self, future: F)
    where
        F: Future<Output = ()> + 'static,
    {
        self.spawner.spawn_local(future).expect("Failed to spawn future");
    }

    fn at(&mut self, t: Time, f: impl FnOnce() + 'static) {
        let (tx, rx) = futures::channel::oneshot::channel();
        std::thread::spawn(move || {
            let now = Time::now();
            if now < t {
                std::thread::sleep(t.0 - now.0);
            }
            let _ = tx.send(());
        });
        self.spawn_local(async move {
            let _ = rx.await;
            f();
        })
    }

    fn router_link_id(&self, _id: Self::LinkId) -> LinkId<overnet_core::PhysLinkId<()>> {
        unreachable!()
    }

    fn send_on_link(&mut self, _id: Self::LinkId, _packet: &mut [u8]) -> Result<(), Error> {
        unreachable!()
    }
}

async fn run_list_peers_inner(
    node: Node<OvernetRuntime>,
    responder: futures::channel::oneshot::Sender<Vec<Peer>>,
) -> Result<(), Error> {
    let peers = node.list_peers().await?;
    if let Err(_) = responder.send(peers) {
        println!("List peers stopped listening");
    }
    Ok(())
}

async fn run_list_peers(
    node: Node<OvernetRuntime>,
    responder: futures::channel::oneshot::Sender<Vec<Peer>>,
) {
    if let Err(e) = run_list_peers_inner(node, responder).await {
        println!("List peers gets error: {:?}", e);
    }
}

async fn run_overnet(
    spawner: TestSpawner,
    mut rx: futures::channel::mpsc::UnboundedReceiver<OvernetCommand>,
) -> Result<(), Error> {
    let node_options = NodeOptions::new();
    #[cfg(not(target_os = "fuchsia"))]
    let node_options = node_options
        .set_quic_server_key_file(hoist::hard_coded_server_key()?)
        .set_quic_server_cert_file(hoist::hard_coded_server_cert()?);
    #[cfg(target_os = "fuchsia")]
    let node_options = node_options
        .set_quic_server_key_file(Box::new("/pkg/data/cert.key".to_string()))
        .set_quic_server_cert_file(Box::new("/pkg/data/cert.crt".to_string()));

    let node = Node::new(OvernetRuntime { spawner: spawner.clone() }, node_options)?;

    // Run application loop
    loop {
        let cmd = rx.next().await;
        let desc = format!("{:?}", cmd);
        let r = match cmd {
            None => return Ok(()),
            Some(OvernetCommand::ListPeers(sender)) => {
                spawner
                    .spawn_local(run_list_peers(node.clone(), sender))
                    .expect("Failed to spawn list peers worker");
                Ok(())
            }
            Some(OvernetCommand::RegisterService(service_name, provider)) => {
                node.register_service(service_name, provider)
            }
            Some(OvernetCommand::ConnectToService(node_id, service_name, channel)) => {
                node.connect_to_service(node_id, &service_name, channel)
            }
            Some(OvernetCommand::AttachSocketLink(socket, options)) => {
                node.attach_socket_link(options.connection_label, socket)
            }
        };
        if let Err(e) = r {
            log::warn!("cmd {} failed: {:?}", desc, e);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyInterface implementations

struct MeshController(Arc<Overnet>);

impl MeshControllerProxyInterface for MeshController {
    fn attach_socket_link(
        &self,
        socket: fidl::Socket,
        options: fidl_fuchsia_overnet::SocketLinkOptions,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::AttachSocketLink(socket, options));
        Ok(())
    }
}

struct ServicePublisher(Arc<Overnet>);

impl ServicePublisherProxyInterface for ServicePublisher {
    fn publish_service(
        &self,
        service_name: &str,
        provider: ClientEnd<ServiceProviderMarker>,
    ) -> Result<(), fidl::Error> {
        self.0.send(OvernetCommand::RegisterService(service_name.to_string(), provider));
        Ok(())
    }
}

struct ServiceConsumer(Arc<Overnet>);

impl ServiceConsumerProxyInterface for ServiceConsumer {
    type ListPeersResponseFut = futures::future::MapErr<
        futures::channel::oneshot::Receiver<Vec<Peer>>,
        fn(futures::channel::oneshot::Canceled) -> fidl::Error,
    >;

    fn list_peers(&self) -> Self::ListPeersResponseFut {
        let (sender, receiver) = futures::channel::oneshot::channel();
        self.0.send(OvernetCommand::ListPeers(sender));
        // Returning an error from the receiver means that the sender disappeared without
        // sending a response, a condition we explicitly disallow.
        receiver.map_err(|_| unreachable!())
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
        ));
        Ok(())
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Connect Overnet nodes to each other

async fn copy_with_mutator(
    mut rx: impl AsyncRead + std::marker::Unpin,
    mut tx: impl AsyncWrite + std::marker::Unpin,
    mut mutator: impl FnMut(Vec<u8>) -> Vec<u8>,
) {
    let mut buf = [0u8; 1024];
    loop {
        match rx.read(&mut buf).await {
            Ok(n) => {
                let write = mutator(Vec::from(&buf[0..n]));
                match tx.write(&write).await {
                    Ok(_) => (),
                    Err(_) => return,
                }
            }
            Err(_) => return,
        }
    }
}

/// Connect two test overnet instances, with mutating functions between the stream socket connecting
/// them - to test recovery mechanisms.
pub fn connect_with_mutator(
    spawner: TestSpawner,
    a: &Arc<Overnet>,
    b: &Arc<Overnet>,
    mutator_ab: Box<dyn FnMut(Vec<u8>) -> Vec<u8>>,
    mutator_ba: Box<dyn FnMut(Vec<u8>) -> Vec<u8>>,
    opt: impl Fn() -> fidl_fuchsia_overnet::SocketLinkOptions,
) -> Result<(), Error> {
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (a1, a2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (b1, b2) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    let (arx, atx) = fidl::AsyncSocket::from_socket(a2)?.split();
    let (brx, btx) = fidl::AsyncSocket::from_socket(b1)?.split();
    a.attach_socket_link(a1, opt())?;
    b.attach_socket_link(b2, opt())?;
    spawner.spawn_local(async move { copy_with_mutator(brx, atx, mutator_ab).await })?;
    spawner.spawn_local(async move { copy_with_mutator(arx, btx, mutator_ba).await })?;
    Ok(())
}

/// Connect two test overnet instances with a stream socket.
pub fn connect(a: &Arc<Overnet>, b: &Arc<Overnet>) -> Result<(), Error> {
    let a = a.clone().connect_as_mesh_controller()?;
    let b = b.clone().connect_as_mesh_controller()?;
    let (sa, sb) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;
    a.attach_socket_link(sa, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;
    b.attach_socket_link(sb, fidl_fuchsia_overnet::SocketLinkOptions::empty())?;
    Ok(())
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Run an asynchronous test body (portably across host & Fuchsia)

// Max test time: one day by default... but can be tuned down at need.
const MAX_TEST_TIME_MS: u32 = 24 * 60 * 60 * 1000;

#[cfg(target_os = "fuchsia")]
pub use fuchsia_runner::*;

#[cfg(not(target_os = "fuchsia"))]
pub use not_fuchsia_runner::*;

use timebomb::timeout_ms;

fn run_async_test_wrapper(
    f: impl 'static + Send + FnOnce() -> Result<(), Error>,
) -> Result<(), Error> {
    hoist::logger::init()?;
    let (s, r) = std::sync::mpsc::channel();
    timeout_ms(move || s.send(f()).unwrap(), MAX_TEST_TIME_MS);
    r.recv()?
}

#[cfg(target_os = "fuchsia")]
mod fuchsia_runner {
    use failure::Error;
    use fuchsia_async as fasync;
    use futures::prelude::*;

    #[derive(Clone)]
    pub struct TestSpawner;
    impl TestSpawner {
        pub fn spawn_local(&self, f: impl Future<Output = ()> + 'static) -> Result<(), Error> {
            fasync::spawn_local(f);
            Ok(())
        }
    }

    pub fn run_async_test<F, Fut>(f: F) -> Result<(), Error>
    where
        F: 'static + Send + FnOnce(TestSpawner) -> Fut,
        Fut: Future<Output = Result<(), Error>> + 'static,
    {
        crate::run_async_test_wrapper(|| {
            fasync::Executor::new()?.run_singlethreaded(f(TestSpawner))
        })
    }
}

#[cfg(not(target_os = "fuchsia"))]
mod not_fuchsia_runner {
    use failure::Error;
    use futures::prelude::*;
    use futures::task::LocalSpawnExt;

    #[derive(Clone)]
    pub struct TestSpawner(futures::executor::LocalSpawner);
    impl TestSpawner {
        pub fn spawn_local(&self, f: impl Future<Output = ()> + 'static) -> Result<(), Error> {
            self.0.spawn_local(f)?;
            Ok(())
        }
    }

    pub fn run_async_test<F, Fut>(f: F) -> Result<(), Error>
    where
        F: 'static + Send + FnOnce(TestSpawner) -> Fut,
        Fut: Future<Output = Result<(), Error>> + 'static,
    {
        crate::run_async_test_wrapper(|| {
            let mut p = futures::executor::LocalPool::new();
            p.run_until(f(TestSpawner(p.spawner())))
        })
    }
}
