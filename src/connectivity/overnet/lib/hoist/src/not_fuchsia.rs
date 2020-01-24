// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(not(target_os = "fuchsia"))]

use {
    anyhow::{bail, format_err, Context as _, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_overnet::{Peer, ServiceProviderMarker},
    fidl_fuchsia_overnet_protocol::StreamSocketGreeting,
    fuchsia_zircon_status as zx_status,
    futures::prelude::*,
    overnet_core::{log_errors, Link, NodeId, Router, RouterOptions, StreamDeframer, StreamFramer},
    parking_lot::Mutex,
    std::{rc::Rc, sync::Arc},
    tokio::{io::AsyncRead, runtime::current_thread},
};

pub use fidl_fuchsia_overnet::MeshControllerProxyInterface;
pub use fidl_fuchsia_overnet::ServiceConsumerProxyInterface;
pub use fidl_fuchsia_overnet::ServicePublisherProxyInterface;

pub const ASCENDD_CLIENT_CONNECTION_STRING: &str = "Lift me";
pub const ASCENDD_SERVER_CONNECTION_STRING: &str = "Yessir";
pub const DEFAULT_ASCENDD_PATH: &str = "/tmp/ascendd";

pub use overnet_core::run;

///////////////////////////////////////////////////////////////////////////////////////////////////
// Overnet <-> API bindings

#[derive(Debug)]
enum OvernetCommand {
    ListPeers(futures::channel::oneshot::Sender<Vec<Peer>>),
    RegisterService(String, ClientEnd<ServiceProviderMarker>),
    ConnectToService(NodeId, String, fidl::Channel),
    AttachSocketLink(fidl::Socket, fidl_fuchsia_overnet::SocketLinkOptions),
}

struct Overnet {
    tx: Mutex<futures::channel::mpsc::UnboundedSender<OvernetCommand>>,
    thread: Option<std::thread::JoinHandle<()>>,
}

impl Drop for Overnet {
    fn drop(&mut self) {
        self.tx.lock().close_channel();
        self.thread.take().unwrap().join().unwrap();
    }
}

impl Overnet {
    fn new() -> Result<Overnet, Error> {
        let (tx, rx) = futures::channel::mpsc::unbounded();
        let rx = Arc::new(Mutex::new(rx));
        let thread = Some(
            std::thread::Builder::new()
                .spawn(move || run_overnet(rx))
                .context("Spawning overnet thread")?,
        );
        let tx = Mutex::new(tx);
        Ok(Overnet { tx, thread })
    }

    fn send(&self, cmd: OvernetCommand) {
        self.tx.lock().unbounded_send(cmd).unwrap();
    }
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
struct Time(std::time::Instant);

async fn read_incoming(
    stream: tokio::io::ReadHalf<tokio::net::UnixStream>,
    mut chan: futures::channel::mpsc::Sender<Vec<u8>>,
) -> Result<(), Error> {
    let mut buf = [0u8; 1024];
    let mut stream = Some(stream);
    let mut deframer = StreamDeframer::new();

    loop {
        let rd = tokio::io::read(stream.take().unwrap(), &mut buf[..]);
        let rd = futures::compat::Compat01As03::new(rd);
        let (returned_stream, _, n) = rd.await?;
        if n == 0 {
            return Ok(());
        }
        stream = Some(returned_stream);
        deframer.queue_recv(&mut buf[..n]);
        while let Some(frame) = deframer.next_incoming_frame() {
            chan.send(frame).await?;
        }
    }
}

async fn write_outgoing(
    tx_bytes: tokio::io::WriteHalf<tokio::net::UnixStream>,
    link_sender: Rc<Link>,
    mut framer: StreamFramer,
) -> Result<(), Error> {
    let mut frame = [0u8; 2048];
    let mut tx_bytes = Some(tx_bytes);
    while let Some(n) = link_sender.next_send(&mut frame).await? {
        framer.queue_send(&mut frame[..n])?;
        let out = framer.take_sends();
        let wr = tokio::io::write_all(tx_bytes.take().unwrap(), out.as_slice());
        let wr = futures::compat::Compat01As03::new(wr).map_err(|e| -> Error { e.into() });
        let (t, _) = wr.await?;
        tx_bytes = Some(t);
    }
    Ok(())
}

/// Retry a future until it succeeds or retries run out.
async fn retry_with_backoff<T, E, F>(
    mut backoff: std::time::Duration,
    mut remaining_retries: u8,
    f: impl Fn() -> F,
) -> Result<T, E>
where
    F: futures::Future<Output = Result<T, E>>,
    E: std::fmt::Debug,
{
    while remaining_retries > 1 {
        match f().await {
            Ok(r) => return Ok(r),
            Err(e) => {
                log::warn!("Operation failed: {:?} -- retrying in {:?}", e, backoff);
                std::thread::sleep(backoff);
                backoff *= 2;
                remaining_retries -= 1;
            }
        }
    }
    f().await
}

async fn run_overnet_prelude() -> Result<Rc<Router>, Error> {
    let node_id = overnet_core::generate_node_id();
    log::trace!("Hoist node id:  {}", node_id.0);

    let ascendd_path = std::env::var("ASCENDD").unwrap_or(DEFAULT_ASCENDD_PATH.to_string());
    let connection_label = std::env::var("OVERNET_CONNECTION_LABEL").ok();

    log::trace!("Ascendd path: {}", ascendd_path);
    log::trace!("Overnet connection label: {:?}", connection_label);
    let uds = tokio::net::UnixStream::connect(ascendd_path.clone());
    let uds = futures::compat::Compat01As03::new(uds);
    let uds = uds.await.context(format!("Opening uds path: {}", ascendd_path))?;
    let (rx_bytes, tx_bytes) = uds.split();
    let (tx_frames, mut rx_frames) = futures::channel::mpsc::channel(8);

    spawn(log_errors(read_incoming(rx_bytes, tx_frames), "Error reading"));

    // Send first frame
    let mut framer = StreamFramer::new();
    let mut greeting = StreamSocketGreeting {
        magic_string: Some(ASCENDD_CLIENT_CONNECTION_STRING.to_string()),
        node_id: Some(fidl_fuchsia_overnet_protocol::NodeId { id: node_id.0 }),
        connection_label,
    };
    let mut bytes = Vec::new();
    let mut handles = Vec::new();
    fidl::encoding::Encoder::encode(&mut bytes, &mut handles, &mut greeting)?;
    assert_eq!(handles.len(), 0);
    framer.queue_send(bytes.as_slice())?;
    let send = framer.take_sends();
    let wr = tokio::io::write_all(tx_bytes, send);
    let wr = futures::compat::Compat01As03::new(wr).map_err(|e| -> Error { e.into() });

    log::trace!("Wait for greeting & first frame write");
    let first_frame = rx_frames
        .next()
        .map(|r| r.ok_or_else(|| format_err!("Stream closed before greeting received")));
    let (mut frame, (tx_bytes, _)) = futures::try_join!(first_frame, wr)?;

    let mut greeting = StreamSocketGreeting::empty();
    // WARNING: Since we are decoding without a transaction header, we have to
    // provide a context manually. This could cause problems in future FIDL wire
    // format migrations, which are driven by header flags.
    let context = fidl::encoding::Context {};
    fidl::encoding::Decoder::decode_with_context(&context, frame.as_mut(), &mut [], &mut greeting)?;

    log::trace!("Got greeting: {:?}", greeting);
    let ascendd_node_id = match greeting {
        StreamSocketGreeting { magic_string: None, .. } => bail!(
            "Required magic string '{}' not present in greeting",
            ASCENDD_SERVER_CONNECTION_STRING
        ),
        StreamSocketGreeting { magic_string: Some(ref x), .. }
            if x != ASCENDD_SERVER_CONNECTION_STRING =>
        {
            bail!(
                "Expected magic string '{}' in greeting, got '{}'",
                ASCENDD_SERVER_CONNECTION_STRING,
                x
            )
        }
        StreamSocketGreeting { node_id: None, .. } => bail!("No node id in greeting"),
        StreamSocketGreeting { node_id: Some(n), .. } => n.id,
    };

    let node = Router::new(
        RouterOptions::new()
            .export_diagnostics(fidl_fuchsia_overnet_protocol::Implementation::HoistRustCrate)
            .set_node_id(node_id)
            .set_quic_server_key_file(hard_coded_server_key()?)
            .set_quic_server_cert_file(hard_coded_server_cert()?),
    )?;
    let link = node.new_link(ascendd_node_id.into()).await?;

    let link_receiver = link.clone();
    spawn(async move {
        while let Some(mut frame) = rx_frames.next().await {
            if let Err(e) = link_receiver.received_packet(frame.as_mut_slice()).await {
                log::warn!("Error receiving packet: {}", e);
            }
        }
    });

    spawn(log_errors(write_outgoing(tx_bytes, link, framer), "Error writing"));

    Ok(node)
}

async fn run_overnet_inner(
    rx: Arc<Mutex<futures::channel::mpsc::UnboundedReceiver<OvernetCommand>>>,
) -> Result<(), Error> {
    let mut rx = rx.lock();
    let node =
        retry_with_backoff(std::time::Duration::from_millis(100), 5, run_overnet_prelude).await?;

    // Run application loop
    loop {
        let cmd = rx.next().await;
        let desc = format!("{:?}", cmd);
        let r = match cmd {
            None => return Ok(()),
            Some(OvernetCommand::ListPeers(sender)) => {
                node.list_peers(Box::new(|peers| {
                    let _ = sender.send(peers);
                }))
                .await
            }
            Some(OvernetCommand::RegisterService(service_name, provider)) => {
                node.register_service(service_name, provider).await
            }
            Some(OvernetCommand::ConnectToService(node_id, service_name, channel)) => {
                node.connect_to_service(node_id, &service_name, channel).await
            }
            Some(OvernetCommand::AttachSocketLink(socket, options)) => {
                node.attach_socket_link(socket, options)
            }
        };
        if let Err(e) = r {
            log::warn!("cmd {} failed: {:?}", desc, e);
        }
    }
}

fn run_overnet(rx: Arc<Mutex<futures::channel::mpsc::UnboundedReceiver<OvernetCommand>>>) {
    current_thread::Runtime::new()
        .unwrap()
        .block_on(
            async move {
                if let Err(e) = run_overnet_inner(rx).await {
                    log::warn!("Main loop failed: {}", e);
                }
            }
            .unit_error()
            .boxed_local()
            .compat(),
        )
        .unwrap();
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
        receiver.map_err(|_| fidl::Error::ClientRead(zx_status::Status::PEER_CLOSED))
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

lazy_static::lazy_static! {
    static ref OVERNET: Arc<Overnet> = Arc::new(Overnet::new().unwrap());
}

pub fn connect_as_service_consumer() -> Result<impl ServiceConsumerProxyInterface, Error> {
    Ok(ServiceConsumer(OVERNET.clone()))
}

pub fn connect_as_service_publisher() -> Result<impl ServicePublisherProxyInterface, Error> {
    Ok(ServicePublisher(OVERNET.clone()))
}

pub fn connect_as_mesh_controller() -> Result<impl MeshControllerProxyInterface, Error> {
    Ok(MeshController(OVERNET.clone()))
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Executor implementation

pub fn spawn<F>(future: F)
where
    F: Future<Output = ()> + 'static,
{
    current_thread::spawn(future.unit_error().boxed_local().compat());
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Hacks to hardcode a resource file without resources

fn temp_file_containing(bytes: &[u8]) -> Result<Box<dyn AsRef<std::path::Path>>, Error> {
    let mut path = tempfile::NamedTempFile::new()?;
    use std::io::Write;
    path.write_all(bytes)?;
    Ok(Box::new(path))
}

pub fn hard_coded_server_cert() -> Result<Box<dyn AsRef<std::path::Path>>, Error> {
    temp_file_containing(include_bytes!(
        "../../../../../../third_party/rust-mirrors/quiche/examples/cert.crt"
    ))
}

pub fn hard_coded_server_key() -> Result<Box<dyn AsRef<std::path::Path>>, Error> {
    temp_file_containing(include_bytes!(
        "../../../../../../third_party/rust-mirrors/quiche/examples/cert.key"
    ))
}
