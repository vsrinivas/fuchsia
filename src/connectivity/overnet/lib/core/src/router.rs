// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Manages peer<->peer connections and routing packets over links between nodes.

// General structure:
// A router is a collection of: - streams (each with a StreamId<LinkData>)
//                                These are streams of information flow between processes.
//                              - peers (each with a PeerId)
//                                These are other overnet instances in the overlay network.
//                                There is a client oriented and a server oriented peer per
//                                node.
//                              - links (each with a LinkId)
//                                These are connections between this instance and other
//                                instances in the mesh.
// For each node in the mesh, a routing table tracks which link on which to send data to
// that node (said link may be a third node that will be requested to forward datagrams
// on our behalf).

use crate::{
    diagnostics_service::spawn_diagostic_service_request_handler,
    future_help::{Observable, Observer},
    labels::{Endpoint, NodeId, NodeLinkId},
    link::{Link, LinkStatus},
    link_status_updater::spawn_link_status_updater,
    peer::Peer,
    route_planner::{
        routing_update_channel, spawn_route_planner, RoutingUpdate, RoutingUpdateSender,
    },
    runtime::spawn,
    service_map::{ListablePeer, ServiceMap},
    socket_link::spawn_socket_link,
};
use anyhow::{bail, format_err, Context as _, Error};
use fidl::{endpoints::ClientEnd, Channel};
use fidl_fuchsia_overnet::{ConnectionInfo, ServiceProviderMarker};
use fidl_fuchsia_overnet_protocol::{LinkDiagnosticInfo, PeerConnectionDiagnosticInfo};
use futures::{lock::Mutex, prelude::*};
use rand::Rng;
use std::{
    collections::{BTreeMap, HashMap},
    path::Path,
    rc::{Rc, Weak},
    sync::atomic::{AtomicU64, Ordering},
    time::Duration,
};

/// Configuration object for creating a router.
pub struct RouterOptions {
    node_id: Option<NodeId>,
    pub(crate) quic_server_key_file: Option<Box<dyn AsRef<Path>>>,
    pub(crate) quic_server_cert_file: Option<Box<dyn AsRef<Path>>>,
    diagnostics: Option<fidl_fuchsia_overnet_protocol::Implementation>,
}

impl RouterOptions {
    /// Create with defaults.
    pub fn new() -> Self {
        RouterOptions {
            node_id: None,
            quic_server_key_file: None,
            quic_server_cert_file: None,
            diagnostics: None,
        }
    }

    /// Request a specific node id (if unset, one will be generated).
    pub fn set_node_id(mut self, node_id: NodeId) -> Self {
        self.node_id = Some(node_id);
        self
    }

    /// Set which file to load the server private key from.
    pub fn set_quic_server_key_file(mut self, key_file: Box<dyn AsRef<Path>>) -> Self {
        self.quic_server_key_file = Some(key_file);
        self
    }

    /// Set which file to load the server cert from.
    pub fn set_quic_server_cert_file(mut self, pem_file: Box<dyn AsRef<Path>>) -> Self {
        self.quic_server_cert_file = Some(pem_file);
        self
    }

    /// Enable diagnostics with a selected `implementation`.
    pub fn export_diagnostics(
        mut self,
        implementation: fidl_fuchsia_overnet_protocol::Implementation,
    ) -> Self {
        self.diagnostics = Some(implementation);
        self
    }
}

/// Router maintains global state for one node_id.
/// `LinkData` is a token identifying a link for layers above Router.
/// `Time` is a representation of time for the Router, to assist injecting different platforms
/// schemes.
pub struct Router {
    /// Our node id
    node_id: NodeId,
    /// Factory for local link id's (the next id to be assigned).
    next_node_link_id: AtomicU64,
    /// The servers private key file for this node.
    server_key_file: Box<dyn AsRef<Path>>,
    /// The servers private cert file for this node.
    server_cert_file: Box<dyn AsRef<Path>>,
    /// All peers.
    peers: Mutex<BTreeMap<(NodeId, Endpoint), Rc<Peer>>>,
    links: Mutex<HashMap<NodeLinkId, Weak<Link>>>,
    link_state_changed_observable: Observable<()>,
    link_state_observable: Observable<Vec<LinkStatus>>,
    service_map: ServiceMap,
    routing_update_sender: RoutingUpdateSender,
    list_peers_observer: Mutex<Option<Observer<Vec<ListablePeer>>>>,
}

/// Generate a new random node id
pub fn generate_node_id() -> NodeId {
    rand::thread_rng().gen::<u64>().into()
}

impl Router {
    /// New with some set of options
    pub fn new(options: RouterOptions) -> Result<Rc<Self>, Error> {
        let node_id = options.node_id.unwrap_or_else(generate_node_id);
        let server_cert_file = options
            .quic_server_cert_file
            .ok_or_else(|| anyhow::format_err!("No server cert file"))?;
        if !server_cert_file.as_ref().as_ref().exists() {
            anyhow::bail!(
                "Server cert file does not exist: {}",
                server_cert_file.as_ref().as_ref().display()
            );
        }
        let server_key_file = options
            .quic_server_key_file
            .ok_or_else(|| anyhow::format_err!("No server key file"))?;
        if !server_key_file.as_ref().as_ref().exists() {
            anyhow::bail!(
                "Server key file does not exist: {}",
                server_key_file.as_ref().as_ref().display()
            );
        }
        let (routing_update_sender, routing_update_receiver) = routing_update_channel();
        let service_map = ServiceMap::new(node_id);
        let list_peers_observer = Mutex::new(Some(service_map.new_list_peers_observer()));
        let router = Rc::new(Router {
            node_id,
            next_node_link_id: 1.into(),
            server_cert_file,
            server_key_file,
            routing_update_sender,
            link_state_changed_observable: Observable::new(()),
            link_state_observable: Observable::new(Vec::new()),
            service_map,
            links: Mutex::new(HashMap::new()),
            peers: Mutex::new(BTreeMap::new()),
            list_peers_observer,
        });

        spawn_route_planner(router.clone(), routing_update_receiver);
        spawn_link_status_updater(&router, router.link_state_changed_observable.new_observer());
        // Spawn a future to service diagnostic requests
        if let Some(implementation) = options.diagnostics {
            spawn_diagostic_service_request_handler(router.clone(), implementation);
        }
        Ok(router)
    }

    /// Create a new link to some node, returning a `LinkId` describing it.
    pub async fn new_link(self: &Rc<Self>, peer_node_id: NodeId) -> Result<Rc<Link>, Error> {
        let node_link_id = self.next_node_link_id.fetch_add(1, Ordering::Relaxed).into();
        let link = Link::new(peer_node_id, node_link_id, &self).await?;
        let routing_update_sender = self.routing_update_sender.clone();
        // Spawn a task to move link status to the
        spawn(link.new_description_observer().for_each(move |description| {
            let mut routing_update_sender = routing_update_sender.clone();
            async move {
                if let Err(e) = routing_update_sender
                    .send(RoutingUpdate::UpdateLocalLinkStatus {
                        to_node_id: peer_node_id,
                        link_id: node_link_id,
                        description,
                    })
                    .await
                {
                    log::warn!("Failed publishing local link state: {:?}", e);
                }
            }
        }));
        Ok(link)
    }

    /// Accessor for the node id of this router.
    pub fn node_id(&self) -> NodeId {
        self.node_id
    }

    pub(crate) fn service_map(&self) -> &ServiceMap {
        &self.service_map
    }

    /// Create a new stream to advertised service `service` on remote node id `node`.
    pub async fn connect_to_service(
        self: &Rc<Self>,
        node_id: NodeId,
        service_name: &str,
        chan: Channel,
    ) -> Result<(), Error> {
        let is_local = node_id == self.node_id;
        log::trace!(
            "Request connect_to_service '{}' on {:?}{}",
            service_name,
            node_id,
            if is_local { " [local]" } else { " [remote]" }
        );
        if is_local {
            self.service_map()
                .connect(service_name, chan, ConnectionInfo { peer: Some(node_id.into()) })
                .await
        } else {
            self.client_peer(node_id, &Weak::new())
                .await
                .with_context(|| {
                    format_err!(
                        "Fetching client peer for new stream to {:?} for service {:?}",
                        node_id,
                        service_name,
                    )
                })?
                .new_stream(service_name, chan)
                .await
        }
    }

    /// Implementation of RegisterService fidl method.
    pub async fn register_service(
        &self,
        service_name: String,
        provider: ClientEnd<ServiceProviderMarker>,
    ) -> Result<(), Error> {
        self.service_map().register_service(service_name, Box::new(provider.into_proxy()?)).await;
        Ok(())
    }

    /// Implementation of ListPeers fidl method.
    pub async fn list_peers(
        self: &Rc<Self>,
        sink: Box<dyn FnOnce(Vec<fidl_fuchsia_overnet::Peer>)>,
    ) -> Result<(), Error> {
        let mut obs = self
            .list_peers_observer
            .lock()
            .await
            .take()
            .ok_or_else(|| anyhow::format_err!("Already listing peers"))?;
        let router = self.clone();
        spawn(async move {
            if let Some(r) = obs.next().await {
                *router.list_peers_observer.lock().await = Some(obs);
                sink(r.into_iter().map(|p| p.into()).collect());
            } else {
                *router.list_peers_observer.lock().await = Some(obs);
            }
        });
        Ok(())
    }

    /// Implementation of AttachToSocket fidl method.
    pub fn attach_socket_link(
        self: &Rc<Self>,
        socket: fidl::Socket,
        options: fidl_fuchsia_overnet::SocketLinkOptions,
    ) -> Result<(), Error> {
        let duration_per_byte = if let Some(n) = options.bytes_per_second {
            Some(std::cmp::max(
                Duration::from_micros(10),
                Duration::from_secs(1)
                    .checked_div(n)
                    .ok_or_else(|| anyhow::format_err!("Division failed: 1 second / {}", n))?,
            ))
        } else {
            None
        };
        spawn_socket_link(
            self.clone(),
            self.node_id,
            options.connection_label,
            socket,
            duration_per_byte,
        );
        Ok(())
    }

    /// Diagnostic information for links
    pub(crate) async fn link_diagnostics(&self) -> Vec<LinkDiagnosticInfo> {
        self.links
            .lock()
            .await
            .iter()
            .filter_map(|(_, link)| Weak::upgrade(link).map(|link| link.diagnostic_info()))
            .collect()
    }

    pub(crate) async fn add_link(&self, link: &Rc<Link>) {
        self.links.lock().await.insert(link.id(), Rc::downgrade(link));
        self.schedule_link_status_update();
    }

    pub(crate) fn schedule_link_status_update(&self) {
        self.link_state_changed_observable.push(());
    }

    pub(crate) async fn publish_new_link_status(&self) {
        let mut status = Vec::new();
        let mut dead = Vec::new();
        let mut links = self.links.lock().await;
        for (id, link) in links.iter() {
            if let Some(link) = Weak::upgrade(link) {
                link.make_status().map(|s| status.push(s));
            } else {
                dead.push(*id);
            }
        }
        for id in dead {
            links.remove(&id);
        }
        self.link_state_observable.push(status);
    }

    /// Diagnostic information for peer connections
    pub(crate) async fn peer_diagnostics(&self) -> Vec<PeerConnectionDiagnosticInfo> {
        self.peers.lock().await.iter().map(|(_, peer)| peer.diagnostics(self.node_id)).collect()
    }

    fn server_config(&self) -> Result<quiche::Config, Error> {
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)
            .context("Creating quic configuration for server connection")?;
        let cert_file = self
            .server_cert_file
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| format_err!("Cannot convert path to string"))?;
        let key_file = self
            .server_key_file
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| format_err!("Cannot convert path to string"))?;
        config
            .load_cert_chain_from_pem_file(cert_file)
            .context(format!("Loading server certificate '{}'", cert_file))?;
        config
            .load_priv_key_from_pem_file(key_file)
            .context(format!("Loading server private key '{}'", key_file))?;
        // TODO(ctiller): don't hardcode these
        config
            .set_application_protos(b"\x0bovernet/0.1")
            .context("Setting application protocols")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(0);
        Ok(config)
    }

    fn client_config(&self) -> Result<quiche::Config, Error> {
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
        // TODO(ctiller): don't hardcode these
        config.set_application_protos(b"\x0bovernet/0.1")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(0);
        Ok(config)
    }

    pub(crate) async fn send_routing_update(&self, routing_update: RoutingUpdate) {
        if let Err(e) = self.routing_update_sender.clone().send(routing_update).await {
            log::warn!("Routing update send failed: {:?}", e);
        }
    }

    /// Ensure that a client peer id exists for a given `node_id`
    /// If the client peer needs to be created, `link_id_hint` will be used as an interim hint
    /// as to the best route *to* this node, until a full routing update can be performed.
    pub(crate) async fn ensure_client_peer(
        self: &Rc<Self>,
        node_id: NodeId,
        link_hint: &Weak<Link>,
    ) -> Result<(), Error> {
        if node_id == self.node_id {
            return Ok(());
        }
        self.client_peer(node_id, link_hint).await.map(|_| ())
    }

    /// Map a node id to the client peer id. Since we can always create a client peer, this will
    /// attempt to initiate a connection if there is no current connection.
    /// If the client peer needs to be created, `link_id_hint` will be used as an interim hint
    /// as to the best route *to* this node, until a full routing update can be performed.
    pub(crate) async fn client_peer(
        self: &Rc<Self>,
        node_id: NodeId,
        link_hint: &Weak<Link>,
    ) -> Result<Rc<Peer>, Error> {
        if node_id == self.node_id {
            bail!("Trying to create loopback client peer");
        }
        let key = (node_id, Endpoint::Client);
        let mut peers = self.peers.lock().await;
        if let Some(p) = peers.get(&key) {
            return Ok(p.clone());
        }

        let mut config = self.client_config()?;
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let p = Peer::new_client(
            node_id,
            quiche::connect(None, &scid, &mut config).context("creating quic client connection")?,
            link_hint,
            self.link_state_observable.new_observer(),
            self.service_map.new_local_service_observer(),
        )
        .context("creating client peer")?;
        peers.insert(key, p.clone());
        Ok(p)
    }

    /// Retrieve an existing server peer id for some node id.
    pub(crate) async fn server_peer(
        self: &Rc<Self>,
        node_id: NodeId,
        is_initial_packet: bool,
        link_hint: &Weak<Link>,
    ) -> Result<Option<Rc<Peer>>, Error> {
        if node_id == self.node_id {
            bail!("Trying to create loopback server peer");
        }
        let mut peers = self.peers.lock().await;
        let key = (node_id, Endpoint::Server);
        if let Some(p) = peers.get(&key) {
            return Ok(Some(p.clone()));
        }

        if !is_initial_packet {
            return Ok(None);
        }

        let mut config = self.server_config()?;
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let p = Peer::new_server(
            node_id,
            quiche::accept(&scid, None, &mut config).context("Creating quic server connection")?,
            link_hint,
            &self,
        )
        .context("creating server peer")?;
        assert!(peers.insert(key, p.clone()).is_none());
        Ok(Some(p))
    }

    pub(crate) async fn update_routes(
        self: &Rc<Self>,
        routes: impl Iterator<Item = (NodeId, NodeLinkId)>,
    ) -> Result<(), Error> {
        let links = self.links.lock().await;
        let dummy = Weak::new();
        for (node_id, link_id) in routes {
            let link = links.get(&link_id).unwrap_or(&dummy);
            if let Some(peer) = self.server_peer(node_id, false, &Weak::new()).await? {
                peer.update_link(link.clone()).await;
            }
            self.client_peer(node_id, &link)
                .await
                .with_context(|| format_err!("Adjusting route to link {:?}", link_id))?
                .update_link(link.clone())
                .await;
        }
        Ok(())
    }
}

#[cfg(test)]
pub mod test_util {
    use super::*;
    use std::sync::Once;

    const LOG_LEVEL: log::Level = log::Level::Info;
    const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

    struct Logger;

    fn short_log_level(level: &log::Level) -> &'static str {
        match *level {
            log::Level::Error => "E",
            log::Level::Warn => "W",
            log::Level::Info => "I",
            log::Level::Debug => "D",
            log::Level::Trace => "T",
        }
    }

    impl log::Log for Logger {
        fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
            metadata.level() <= LOG_LEVEL
        }

        fn log(&self, record: &log::Record<'_>) {
            if self.enabled(record.metadata()) {
                println!(
                    "{} {} [{}]: {}",
                    record.target(),
                    record
                        .file()
                        .map(|file| {
                            if let Some(line) = record.line() {
                                format!("{}:{}: ", file, line)
                            } else {
                                format!("{}: ", file)
                            }
                        })
                        .unwrap_or(String::new()),
                    short_log_level(&record.level()),
                    record.args()
                );
            }
        }

        fn flush(&self) {}
    }

    static LOGGER: Logger = Logger;
    static START: Once = Once::new();

    fn init() {
        START.call_once(|| {
            log::set_logger(&LOGGER).unwrap();
            log::set_max_level(MAX_LOG_LEVEL);
        })
    }

    pub fn run<F, Fut>(f: F)
    where
        F: 'static + Send + FnOnce() -> Fut,
        Fut: Future<Output = ()> + 'static,
    {
        const TEST_TIMEOUT_MS: u32 = 60_000;
        init();
        timebomb::timeout_ms(move || crate::runtime::run(f()), TEST_TIMEOUT_MS)
    }

    #[cfg(not(target_os = "fuchsia"))]
    fn temp_file_containing(bytes: &[u8]) -> Box<dyn AsRef<std::path::Path>> {
        let mut path = tempfile::NamedTempFile::new().unwrap();
        use std::io::Write;
        path.write_all(bytes).unwrap();
        Box::new(path)
    }

    pub fn test_router_options() -> RouterOptions {
        let options = RouterOptions::new();
        #[cfg(target_os = "fuchsia")]
        let options = options
            .set_quic_server_cert_file(Box::new("/pkg/data/cert.crt".to_string()))
            .set_quic_server_key_file(Box::new("/pkg/data/cert.key".to_string()));
        #[cfg(not(target_os = "fuchsia"))]
        let options = options
            .set_quic_server_cert_file(temp_file_containing(include_bytes!(
                "../../../../../../third_party/rust-mirrors/quiche/examples/cert.crt"
            )))
            .set_quic_server_key_file(temp_file_containing(include_bytes!(
                "../../../../../../third_party/rust-mirrors/quiche/examples/cert.key"
            )));
        options
    }
}

#[cfg(test)]
mod tests {

    use super::test_util::*;
    use super::*;
    use crate::future_help::log_errors;
    use crate::runtime::{spawn, wait_until};
    use futures::{executor::block_on, select};
    use std::time::{Duration, Instant};

    #[test]
    fn no_op() {
        run(|| async move {
            Router::new(test_router_options()).unwrap();
            assert_eq!(
                Router::new(test_router_options().set_node_id(1.into()),).unwrap().node_id.0,
                1
            );
        })
    }

    struct TwoNode {
        router1: Rc<Router>,
        router2: Rc<Router>,
    }

    fn forward(sender: Rc<Link>, receiver: Rc<Link>) {
        spawn(log_errors(
            async move {
                let mut frame = [0u8; 2048];
                while let Some(n) = sender.next_send(&mut frame).await? {
                    receiver.received_packet(&mut frame[..n]).await?;
                }
                Ok(())
            },
            "forwarder failed",
        ));
    }

    async fn register_test_service(
        router: &Rc<Router>,
        service: &str,
    ) -> futures::channel::oneshot::Receiver<(NodeId, Channel)> {
        use std::sync::Mutex;
        let (send, recv) = futures::channel::oneshot::channel();
        struct TestService(Mutex<Option<futures::channel::oneshot::Sender<(NodeId, Channel)>>>);
        impl fidl_fuchsia_overnet::ServiceProviderProxyInterface for TestService {
            fn connect_to_service(
                &self,
                chan: fidl::Channel,
                connection_info: fidl_fuchsia_overnet::ConnectionInfo,
            ) -> std::result::Result<(), fidl::Error> {
                self.0
                    .lock()
                    .unwrap()
                    .take()
                    .unwrap()
                    .send((connection_info.peer.unwrap().id.into(), chan))
                    .unwrap();
                Ok(())
            }
        }
        router
            .service_map()
            .register_service(service.to_string(), Box::new(TestService(Mutex::new(Some(send)))))
            .await;
        recv
    }

    impl TwoNode {
        async fn new() -> Self {
            let router1 = Router::new(test_router_options()).unwrap();
            let router2 = Router::new(test_router_options()).unwrap();
            let link1 = router1.new_link(router2.node_id).await.unwrap();
            let link2 = router2.new_link(router1.node_id).await.unwrap();
            forward(link1.clone(), link2.clone());
            forward(link2, link1);

            TwoNode { router1, router2 }
        }
    }

    #[test]
    fn no_op_env() {
        run(|| async move {
            TwoNode::new().await;
        })
    }

    #[test]
    fn create_stream() {
        run(|| async move {
            let env = TwoNode::new().await;
            let (_, p) = fidl::Channel::create().unwrap();
            let s = register_test_service(&env.router2, "hello world").await;
            env.router1.connect_to_service(env.router2.node_id, "hello world", p).await.unwrap();
            let (node_id, _) = s.await.unwrap();
            assert_eq!(node_id, env.router1.node_id);
        })
    }

    #[test]
    fn send_datagram_immediately() {
        run(|| async move {
            let env = TwoNode::new().await;
            let (c, p) = fidl::Channel::create().unwrap();
            let s = register_test_service(&env.router2, "hello world").await;
            env.router1.connect_to_service(env.router2.node_id, "hello world", p).await.unwrap();
            let (node_id, s) = s.await.unwrap();
            assert_eq!(node_id, env.router1.node_id);
            let c = fidl::AsyncChannel::from_channel(c).unwrap();
            let s = fidl::AsyncChannel::from_channel(s).unwrap();
            c.write(&[1, 2, 3, 4, 5], &mut Vec::new()).unwrap();
            let mut buf = fidl::MessageBuf::new();
            s.recv_msg(&mut buf).await.unwrap();
            assert_eq!(buf.n_handles(), 0);
            assert_eq!(buf.bytes(), &[1, 2, 3, 4, 5]);
        })
    }

    #[test]
    fn ping_pong() {
        run(|| async move {
            let env = TwoNode::new().await;
            let (c, p) = fidl::Channel::create().unwrap();
            let s = register_test_service(&env.router2, "hello world").await;
            env.router1.connect_to_service(env.router2.node_id, "hello world", p).await.unwrap();
            let (node_id, s) = s.await.unwrap();
            assert_eq!(node_id, env.router1.node_id);

            let c = fidl::AsyncChannel::from_channel(c).unwrap();
            let s = fidl::AsyncChannel::from_channel(s).unwrap();

            println!("send ping");
            c.write(&[1, 2, 3, 4, 5], &mut Vec::new()).unwrap();
            println!("receive ping");
            let mut buf = fidl::MessageBuf::new();
            s.recv_msg(&mut buf).await.unwrap();
            assert_eq!(buf.n_handles(), 0);
            assert_eq!(buf.bytes(), &[1, 2, 3, 4, 5]);

            println!("send pong");
            s.write(&[9, 8, 7, 6, 5, 4, 3, 2, 1], &mut Vec::new()).unwrap();
            println!("receive pong");
            let mut buf = fidl::MessageBuf::new();
            c.recv_msg(&mut buf).await.unwrap();
            assert_eq!(buf.n_handles(), 0);
            assert_eq!(buf.bytes(), &[9, 8, 7, 6, 5, 4, 3, 2, 1]);
        })
    }

    async fn assert_avail(r: &mut futures::channel::mpsc::Receiver<()>, mut n: usize) {
        while n > 0 {
            r.next().await.unwrap();
            n -= 1;
        }
        select! {
            _ = r.next().fuse() => panic!("Unexpected output"),
            _ = wait_until(Instant::now() + Duration::from_secs(1)).fuse() => ()
        }
    }

    #[test]
    fn concurrent_list_peer_calls_will_error() {
        run(|| async move {
            let n = Router::new(test_router_options()).unwrap();
            let (tx, mut rx) = futures::channel::mpsc::channel(1);
            let mut c1 = tx.clone();
            let mut c2 = tx.clone();
            let mut c3 = tx.clone();
            assert_avail(&mut rx, 0).await;
            n.list_peers(Box::new(move |_| block_on(c1.send(())).unwrap())).await.unwrap();
            assert_avail(&mut rx, 1).await;
            n.list_peers(Box::new(move |_| block_on(c2.send(())).unwrap())).await.unwrap();
            assert_avail(&mut rx, 0).await;
            n.list_peers(Box::new(move |_| block_on(c3.send(())).unwrap()))
                .await
                .expect_err("Concurrent list peers should fail");
            assert_avail(&mut rx, 0).await;
        })
    }

    #[test]
    #[cfg(not(target_os = "fuchsia"))]
    fn initial_greeting_packet() {
        use crate::coding::decode_fidl;
        use crate::stream_framer::StreamDeframer;
        use fidl_fuchsia_overnet_protocol::StreamSocketGreeting;
        run(|| async move {
            let n = Router::new(test_router_options()).unwrap();
            let node_id = n.node_id();
            let (c, s) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
            let mut c = fidl::AsyncSocket::from_socket(c).unwrap();
            n.attach_socket_link(
                s,
                fidl_fuchsia_overnet::SocketLinkOptions {
                    connection_label: Some("test".to_string()),
                    bytes_per_second: None,
                },
            )
            .unwrap();

            let mut deframer = StreamDeframer::new();
            let mut buf = [0u8; 1024];
            let mut greeting_bytes = loop {
                let n = c.read(&mut buf).await.unwrap();
                deframer.queue_recv(&buf[..n]);
                if let Some(greeting) = deframer.next_incoming_frame() {
                    break greeting;
                }
            };
            let greeting = decode_fidl::<StreamSocketGreeting>(greeting_bytes.as_mut()).unwrap();
            assert_eq!(greeting.magic_string, Some("OVERNET SOCKET LINK".to_string()));
            assert_eq!(greeting.node_id, Some(node_id.into()));
            assert_eq!(greeting.connection_label, Some("test".to_string()));
        })
    }

    #[test]
    #[cfg(not(target_os = "fuchsia"))]
    fn attach_with_zero_bytes_per_second() {
        run(|| async move {
            let n = Router::new(test_router_options()).unwrap();
            let (c, s) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
            n.attach_socket_link(
                s,
                fidl_fuchsia_overnet::SocketLinkOptions {
                    connection_label: Some("test".to_string()),
                    bytes_per_second: Some(0),
                },
            )
            .expect_err("bytes_per_second == 0 should fail");
            drop(c);
        })
    }
}
