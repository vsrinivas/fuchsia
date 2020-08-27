// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(not(target_os = "fuchsia"))]

use {
    anyhow::{bail, ensure, Error},
    fidl::endpoints::{create_proxy, create_proxy_and_stream},
    fidl_fuchsia_overnet::{
        HostOvernetMarker, HostOvernetProxy, HostOvernetRequest, HostOvernetRequestStream,
        MeshControllerMarker, MeshControllerRequest, ServiceConsumerMarker, ServiceConsumerRequest,
        ServicePublisherMarker, ServicePublisherRequest,
    },
    fidl_fuchsia_overnet_protocol::StreamSocketGreeting,
    fuchsia_async::{Task, Timer},
    futures::{lock::Mutex, prelude::*},
    overnet_core::{
        log_errors, new_deframer, new_framer, DeframerWriter, FrameType, FramerReader,
        ListPeersContext, LosslessBinary, Router, RouterOptions, SecurityContext,
    },
    std::{
        sync::atomic::{AtomicU64, Ordering},
        sync::Arc,
        time::Duration,
    },
};

pub use fidl_fuchsia_overnet::{
    MeshControllerProxyInterface, ServiceConsumerProxyInterface, ServicePublisherProxyInterface,
};

pub const ASCENDD_CLIENT_CONNECTION_STRING: &str = "ASCENDD_CLIENT_CONNECTION_STRING";
pub const ASCENDD_SERVER_CONNECTION_STRING: &str = "ASCENDD_SERVER_CONNECTION_STRING";
pub const DEFAULT_ASCENDD_PATH: &str = "/tmp/ascendd";

///////////////////////////////////////////////////////////////////////////////////////////////////
// Overnet <-> API bindings

struct Overnet {
    proxy: HostOvernetProxy,
    _task: Task<()>,
}

fn start_overnet() -> Result<Overnet, Error> {
    let (c, s) = create_proxy_and_stream::<HostOvernetMarker>()?;
    Ok(Overnet {
        proxy: c,
        _task: Task::spawn(log_errors(run_overnet(s), "overnet main loop failed")),
    })
}

async fn read_incoming(
    mut stream: &async_std::os::unix::net::UnixStream,
    mut incoming_writer: DeframerWriter<LosslessBinary>,
) -> Result<(), Error> {
    let mut buf = [0u8; 16384];

    loop {
        let n = stream.read(&mut buf).await?;
        if n == 0 {
            return Ok(());
        }
        incoming_writer.write(&buf[..n]).await?;
    }
}

async fn write_outgoing(
    mut outgoing_reader: FramerReader<LosslessBinary>,
    mut stream: &async_std::os::unix::net::UnixStream,
) -> Result<(), Error> {
    loop {
        let out = outgoing_reader.read().await?;
        stream.write_all(&out).await?;
    }
}

async fn run_ascendd_connection(
    node: Arc<Router>,
    tx_ready: Arc<Mutex<Option<futures::channel::oneshot::Sender<()>>>>,
) -> Result<(), Error> {
    let ascendd_path = std::env::var("ASCENDD").unwrap_or(DEFAULT_ASCENDD_PATH.to_string());
    let mut connection_label = std::env::var("OVERNET_CONNECTION_LABEL").ok();
    if connection_label.is_none() {
        connection_label = std::env::current_exe()
            .ok()
            .map(|p| format!("exe:{} pid:{}", p.display(), std::process::id()));
    }
    if connection_label.is_none() {
        connection_label = Some(format!("pid:{}", std::process::id()));
    }

    log::trace!("Ascendd path: {}", ascendd_path);
    log::trace!("Overnet connection label: {:?}", connection_label);
    let uds = async_std::os::unix::net::UnixStream::connect(ascendd_path.clone()).await?;
    let (mut framer, outgoing_reader) = new_framer(LosslessBinary, 4096);
    let (incoming_writer, mut deframer) = new_deframer(LosslessBinary);

    let _ = futures::future::try_join3(
        read_incoming(&uds, incoming_writer),
        write_outgoing(outgoing_reader, &uds),
        async move {
            // Send first frame
            let mut greeting = StreamSocketGreeting {
                magic_string: Some(ASCENDD_CLIENT_CONNECTION_STRING.to_string()),
                node_id: Some(fidl_fuchsia_overnet_protocol::NodeId { id: node.node_id().0 }),
                connection_label: connection_label.clone(),
                key: None,
            };
            let mut bytes = Vec::new();
            let mut handles = Vec::new();
            fidl::encoding::Encoder::encode(&mut bytes, &mut handles, &mut greeting)?;
            assert_eq!(handles.len(), 0);
            framer.write(FrameType::Overnet, bytes.as_slice()).await?;

            log::info!("Wait for greeting & first frame write");
            let (frame_type, mut frame) = deframer.read().await?;
            ensure!(frame_type == Some(FrameType::Overnet), "Expect Overnet frame as first frame");

            let mut greeting = StreamSocketGreeting::empty();
            // WARNING: Since we are decoding without a transaction header, we have to
            // provide a context manually. This could cause problems in future FIDL wire
            // format migrations, which are driven by header flags.
            let context = fidl::encoding::Context {};
            fidl::encoding::Decoder::decode_with_context(
                &context,
                frame.as_mut(),
                &mut [],
                &mut greeting,
            )?;

            log::info!("Got greeting: {:?}", greeting);
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

            let (link_sender, link_receiver) = node
                .new_link(
                    ascendd_node_id.into(),
                    Box::new(move || {
                        Some(fidl_fuchsia_overnet_protocol::LinkConfig::AscenddClient(
                            fidl_fuchsia_overnet_protocol::AscenddLinkConfig {
                                path: Some(ascendd_path.clone()),
                                connection_label: connection_label.clone(),
                            },
                        ))
                    }),
                )
                .await?;

            if let Some(tx_ready) = tx_ready.lock().await.take() {
                let _ = tx_ready.send(());
            }

            let _: ((), ()) = futures::future::try_join(
                async move {
                    loop {
                        let (frame_type, mut frame) = deframer.read().await?;
                        ensure!(
                            frame_type == Some(FrameType::Overnet),
                            "Should only see Overnet frames"
                        );
                        if let Err(e) = link_receiver.received_packet(frame.as_mut_slice()).await {
                            log::warn!("Error receiving packet: {}", e);
                        }
                    }
                },
                async move {
                    let mut buffer = [0u8; 2048];
                    while let Some(n) = link_sender.next_send(&mut buffer).await? {
                        framer.write(FrameType::Overnet, &buffer[..n]).await?;
                    }
                    Ok::<_, Error>(())
                },
            )
            .await?;
            Ok(())
        },
    )
    .await?;
    Ok(())
}

/// Retry a future until it succeeds or retries run out.
async fn retry_with_backoff<E, F>(
    backoff0: Duration,
    max_backoff: Duration,
    mut f: impl FnMut() -> F,
) where
    F: futures::Future<Output = Result<(), E>>,
    E: std::fmt::Debug,
{
    let mut backoff = backoff0;
    loop {
        match f().await {
            Ok(()) => {
                backoff = backoff0;
            }
            Err(e) => {
                log::warn!("Operation failed: {:?} -- retrying in {:?}", e, backoff);
                Timer::new(backoff).await;
                backoff = std::cmp::min(backoff * 2, max_backoff);
            }
        }
    }
}

async fn handle_consumer_request(
    node: Arc<Router>,
    list_peers_context: Arc<ListPeersContext>,
    r: ServiceConsumerRequest,
) -> Result<(), Error> {
    match r {
        ServiceConsumerRequest::ListPeers { responder } => {
            let mut peers = list_peers_context.list_peers().await?;
            responder.send(&mut peers.iter_mut())?
        }
        ServiceConsumerRequest::ConnectToService {
            node: node_id,
            service_name,
            chan,
            control_handle: _,
        } => node.connect_to_service(node_id.id.into(), &service_name, chan).await?,
    }
    Ok(())
}

async fn handle_publisher_request(
    node: Arc<Router>,
    r: ServicePublisherRequest,
) -> Result<(), Error> {
    let ServicePublisherRequest::PublishService { service_name, provider, control_handle: _ } = r;
    node.register_service(service_name, provider).await
}

async fn handle_controller_request(
    node: Arc<Router>,
    r: MeshControllerRequest,
) -> Result<(), Error> {
    let MeshControllerRequest::AttachSocketLink { socket, options, control_handle: _ } = r;
    if let Err(e) = node.run_socket_link(socket, options).await {
        log::warn!("Socket link failed: {:#?}", e);
    }
    Ok(())
}

static NEXT_LOG_ID: AtomicU64 = AtomicU64::new(0);

fn log_request<
    R: 'static + Send + std::fmt::Debug,
    Fut: Send + Future<Output = Result<(), Error>>,
>(
    f: impl 'static + Send + Clone + Fn(R) -> Fut,
) -> impl Fn(R) -> std::pin::Pin<Box<dyn Send + Future<Output = Result<(), Error>>>> {
    move |r| {
        let f = f.clone();
        async move {
            let log_id = NEXT_LOG_ID.fetch_add(1, Ordering::SeqCst);
            log::trace!("[REQUEST:{}] begin {:?}", log_id, r);
            let f = f(r);
            let r = f.await;
            log::trace!("[REQUEST:{}] end {:?}", log_id, r);
            r
        }
        .boxed()
    }
}

async fn handle_request(node: Arc<Router>, req: HostOvernetRequest) -> Result<(), Error> {
    match req {
        HostOvernetRequest::ConnectServiceConsumer { svc, control_handle: _ } => {
            let list_peers_context = Arc::new(node.new_list_peers_context());
            svc.into_stream()?
                .map_err(Into::<Error>::into)
                .try_for_each_concurrent(
                    None,
                    log_request(move |r| {
                        handle_consumer_request(node.clone(), list_peers_context.clone(), r)
                    }),
                )
                .await?
        }
        HostOvernetRequest::ConnectServicePublisher { svc, control_handle: _ } => {
            svc.into_stream()?
                .map_err(Into::<Error>::into)
                .try_for_each_concurrent(
                    None,
                    log_request(move |r| handle_publisher_request(node.clone(), r)),
                )
                .await?
        }
        HostOvernetRequest::ConnectMeshController { svc, control_handle: _ } => {
            svc.into_stream()?
                .map_err(Into::<Error>::into)
                .try_for_each_concurrent(
                    None,
                    log_request(move |r| handle_controller_request(node.clone(), r)),
                )
                .await?
        }
    }
    Ok(())
}

async fn run_overnet(rx: HostOvernetRequestStream) -> Result<(), Error> {
    let node_id = overnet_core::generate_node_id();
    log::trace!("Hoist node id:  {}", node_id.0);
    let node = Router::new(
        RouterOptions::new()
            .export_diagnostics(fidl_fuchsia_overnet_protocol::Implementation::HoistRustCrate)
            .set_node_id(node_id),
        Box::new(hard_coded_security_context()),
    )?;

    let (tx_ready, rx_ready) = futures::channel::oneshot::channel();

    let _connect = Task::spawn({
        let node = node.clone();
        let tx_ready = Arc::new(Mutex::new(Some(tx_ready)));
        async move {
            retry_with_backoff(Duration::from_millis(100), Duration::from_secs(3), || {
                run_ascendd_connection(node.clone(), tx_ready.clone())
            })
            .await
        }
    });

    rx_ready.await?;

    // Run application loop
    rx.map_err(Into::into)
        .try_for_each_concurrent(None, move |req| {
            let node = node.clone();
            async move {
                if let Err(e) = handle_request(node, req).await {
                    log::warn!("Service handler failed: {:?}", e);
                }
                Ok(())
            }
        })
        .await
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// ProxyInterface implementations

lazy_static::lazy_static! {
    static ref OVERNET: Overnet = start_overnet().unwrap();
}

pub fn connect_as_service_consumer() -> Result<impl ServiceConsumerProxyInterface, Error> {
    let (c, s) = create_proxy::<ServiceConsumerMarker>()?;
    OVERNET.proxy.connect_service_consumer(s)?;
    Ok(c)
}

pub fn connect_as_service_publisher() -> Result<impl ServicePublisherProxyInterface, Error> {
    let (c, s) = create_proxy::<ServicePublisherMarker>()?;
    OVERNET.proxy.connect_service_publisher(s)?;
    Ok(c)
}

pub fn connect_as_mesh_controller() -> Result<impl MeshControllerProxyInterface, Error> {
    let (c, s) = create_proxy::<MeshControllerMarker>()?;
    OVERNET.proxy.connect_mesh_controller(s)?;
    Ok(c)
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Hacks to hardcode a resource file without resources

pub fn hard_coded_security_context() -> impl SecurityContext {
    return overnet_core::MemoryBuffers {
        node_cert: include_bytes!(
            "../../../../../../third_party/rust-mirrors/quiche/examples/cert.crt"
        ),
        node_private_key: include_bytes!(
            "../../../../../../third_party/rust-mirrors/quiche/examples/cert.key"
        ),
        root_cert: include_bytes!(
            "../../../../../../third_party/rust-mirrors/quiche/examples/rootca.crt"
        ),
    }
    .into_security_context()
    .unwrap();
}
