// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ap;
pub mod client;
pub mod mesh;

use {
    crate::{MlmeRequest, MlmeStream, Station},
    anyhow::format_err,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEventStream, MlmeProxy},
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_inspect_contrib::auto_persist,
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future::FutureObj, prelude::*, select},
    log::{error, warn},
    std::marker::Unpin,
    std::sync::{Arc, Mutex},
    wlan_common::{
        hasher::WlanHasher,
        timer::{self, TimeEntry},
    },
};

pub type ClientSmeServer = mpsc::UnboundedSender<client::Endpoint>;
pub type ApSmeServer = mpsc::UnboundedSender<ap::Endpoint>;
pub type MeshSmeServer = mpsc::UnboundedSender<mesh::Endpoint>;

#[derive(Clone)]
pub enum SmeServer {
    Client(ClientSmeServer),
    Ap(ApSmeServer),
    Mesh(MeshSmeServer),
}

async fn serve_generic_sme(
    generic_sme: fidl::endpoints::ServerEnd<fidl_sme::GenericSmeMarker>,
    mut sme_server: SmeServer,
) -> Result<(), anyhow::Error> {
    let mut generic_sme_stream = match generic_sme.into_stream() {
        Ok(stream) => stream,
        Err(e) => return Err(format_err!("Failed to handle Generic SME stream: {}", e)),
    };
    loop {
        match generic_sme_stream.next().await {
            // Right now we only support one API per-sme, but in the future we plan to support
            // multiple and this fn will be more useful.
            Some(Ok(req)) => {
                let result = match req {
                    fidl_sme::GenericSmeRequest::GetClientSme { responder } => {
                        let (client_end, server_end) =
                            create_endpoints::<fidl_sme::ClientSmeMarker>()
                                .expect("failed to create ClientSme");
                        let mut response = if let SmeServer::Client(server) = &mut sme_server {
                            server
                                .send(server_end)
                                .await
                                .map(|_| client_end)
                                .map_err(|_| zx::Status::PEER_CLOSED.into_raw())
                        } else {
                            Err(zx::Status::NOT_SUPPORTED.into_raw())
                        };
                        responder.send(&mut response)
                    }
                    fidl_sme::GenericSmeRequest::GetApSme { responder } => {
                        let (client_end, server_end) = create_endpoints::<fidl_sme::ApSmeMarker>()
                            .expect("failed to create ApSme");
                        let mut response = if let SmeServer::Ap(server) = &mut sme_server {
                            server
                                .send(server_end)
                                .await
                                .map(|_| client_end)
                                .map_err(|_| zx::Status::PEER_CLOSED.into_raw())
                        } else {
                            Err(zx::Status::NOT_SUPPORTED.into_raw())
                        };
                        responder.send(&mut response)
                    }
                    fidl_sme::GenericSmeRequest::GetSmeTelemetry { responder } => {
                        // TODO(fxbug.dev/66772): Support SME Telemetry API
                        warn!("Requested unsupported SME telemetry API");
                        responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))
                    }
                };
                if let Err(e) = result {
                    error!("Failed to respond to SME handle request: {}", e);
                }
            }
            Some(Err(e)) => {
                return Err(format_err!("Generic SME stream failed: {}", e));
            }
            None => {
                return Err(format_err!("Generic SME stream terminated"));
            }
        }
    }
}

pub fn create_sme(
    cfg: crate::Config,
    mlme_proxy: fidl_mlme::MlmeProxy,
    device_info: &fidl_mlme::DeviceInfo,
    mac_sublayer_support: fidl_common::MacSublayerSupport,
    security_support: fidl_common::SecuritySupport,
    spectrum_management_support: fidl_common::SpectrumManagementSupport,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    hasher: WlanHasher,
    persistence_req_sender: auto_persist::PersistenceReqSender,
    mut shutdown_receiver: mpsc::Receiver<()>,
    generic_sme: fidl::endpoints::ServerEnd<fidl_sme::GenericSmeMarker>,
) -> (SmeServer, impl Future<Output = Result<(), anyhow::Error>>) {
    let device_info = device_info.clone();
    let event_stream = mlme_proxy.take_event_stream();
    let (server, sme_fut) = match device_info.role {
        fidl_common::WlanMacRole::Client => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = client::serve(
                cfg,
                mlme_proxy,
                device_info,
                mac_sublayer_support,
                security_support,
                spectrum_management_support,
                event_stream,
                receiver,
                iface_tree_holder,
                hasher,
                persistence_req_sender,
            );
            (SmeServer::Client(sender), FutureObj::new(Box::new(fut)))
        }
        fidl_common::WlanMacRole::Ap => {
            let (sender, receiver) = mpsc::unbounded();
            let fut =
                ap::serve(mlme_proxy, device_info, mac_sublayer_support, event_stream, receiver);
            (SmeServer::Ap(sender), FutureObj::new(Box::new(fut)))
        }
        fidl_common::WlanMacRole::Mesh => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = mesh::serve(mlme_proxy, device_info, event_stream, receiver);
            (SmeServer::Mesh(sender), FutureObj::new(Box::new(fut)))
        }
    };
    let generic_sme_fut = serve_generic_sme(generic_sme, server.clone());
    let sme_fut_with_shutdown = async move {
        select! {
            sme_fut = sme_fut.fuse() => sme_fut,
            generic_sme_fut = generic_sme_fut.fuse() => generic_sme_fut,
            _ = shutdown_receiver.select_next_some() => Ok(()),
        }
    };
    (server, sme_fut_with_shutdown)
}

// The returned future successfully terminates when MLME closes the channel
async fn serve_mlme_sme<STA, TS>(
    proxy: MlmeProxy,
    mut event_stream: MlmeEventStream,
    station: Arc<Mutex<STA>>,
    mut mlme_stream: MlmeStream,
    time_stream: TS,
) -> Result<(), anyhow::Error>
where
    STA: Station,
    TS: Stream<Item = TimeEntry<<STA as crate::Station>::Event>> + Unpin,
{
    let mut timeout_stream = timer::make_async_timed_event_stream(time_stream).fuse();

    loop {
        select! {
            // Fuse rationale: any `none`s in the MLME stream should result in
            // bailing immediately, so we don't need to track if we've seen a
            // `None` or not and can `fuse` directly in the `select` call.
            mlme_event = event_stream.next().fuse() => match mlme_event {
                Some(Ok(mlme_event)) => station.lock().unwrap().on_mlme_event(mlme_event),
                Some(Err(ref e)) if e.is_closed() => return Ok(()),
                None => return Ok(()),
                Some(Err(e)) => return Err(format_err!("Error reading an event from MLME channel: {}", e)),
            },
            mlme_req = mlme_stream.next().fuse() => match mlme_req {
                Some(req) => match forward_mlme_request(req, &proxy) {
                    Ok(()) => {},
                    Err(ref e) if e.is_closed() => return Ok(()),
                    Err(e) => return Err(format_err!("Error forwarding a request from SME to MLME: {}", e)),
                },
                None => return Err(format_err!("Stream of requests from SME to MLME has ended unexpectedly")),
            },
            timeout = timeout_stream.next() => match timeout {
                Some(timed_event) => station.lock().unwrap().on_timeout(timed_event),
                None => return Err(format_err!("SME timer stream has ended unexpectedly")),
            },
        }
    }
}

fn forward_mlme_request(req: MlmeRequest, proxy: &MlmeProxy) -> Result<(), fidl::Error> {
    match req {
        MlmeRequest::Scan(mut req) => proxy.start_scan(&mut req),
        MlmeRequest::Join(mut req) => proxy.join_req(&mut req),
        MlmeRequest::Authenticate(mut req) => proxy.authenticate_req(&mut req),
        MlmeRequest::AuthResponse(mut resp) => proxy.authenticate_resp(&mut resp),
        MlmeRequest::Associate(mut req) => proxy.associate_req(&mut req),
        MlmeRequest::AssocResponse(mut resp) => proxy.associate_resp(&mut resp),
        MlmeRequest::Connect(mut req) => proxy.connect_req(&mut req),
        MlmeRequest::Reconnect(mut req) => proxy.reconnect_req(&mut req),
        MlmeRequest::Deauthenticate(mut req) => proxy.deauthenticate_req(&mut req),
        MlmeRequest::Eapol(mut req) => proxy.eapol_req(&mut req),
        MlmeRequest::SetKeys(mut req) => proxy.set_keys_req(&mut req),
        MlmeRequest::SetCtrlPort(mut req) => proxy.set_controlled_port(&mut req),
        MlmeRequest::Start(mut req) => proxy.start_req(&mut req),
        MlmeRequest::Stop(mut req) => proxy.stop_req(&mut req),
        MlmeRequest::SendMpOpenAction(mut req) => proxy.send_mp_open_action(&mut req),
        MlmeRequest::SendMpConfirmAction(mut req) => proxy.send_mp_confirm_action(&mut req),
        MlmeRequest::MeshPeeringEstablished(mut req) => proxy.mesh_peering_established(&mut req),
        MlmeRequest::SaeHandshakeResp(mut resp) => proxy.sae_handshake_resp(&mut resp),
        MlmeRequest::SaeFrameTx(mut frame) => proxy.sae_frame_tx(&mut frame),
        MlmeRequest::WmmStatusReq => proxy.wmm_status_req(),
        MlmeRequest::FinalizeAssociation(mut cap) => proxy.finalize_association_req(&mut cap),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils,
        fidl::endpoints::create_proxy,
        fidl_mlme::MlmeMarker,
        fuchsia_async as fasync,
        fuchsia_inspect::Inspector,
        futures::task::Poll,
        pin_utils::pin_mut,
        std::{pin::Pin, sync::Arc},
        wlan_common::{
            assert_variant,
            test_utils::fake_features::{
                fake_mac_sublayer_support, fake_security_support,
                fake_spectrum_management_support_empty,
            },
        },
        wlan_inspect::IfaceTreeHolder,
    };

    const PLACEHOLDER_HASH_KEY: [u8; 8] = [88, 77, 66, 55, 44, 33, 22, 11];

    #[test]
    fn sme_shutdown() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mlme_proxy, _mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let inspector = Inspector::new();
        let iface_tree_holder = IfaceTreeHolder::new(inspector.root().create_child("sme"));
        let (persistence_req_sender, _persistence_stream) =
            test_utils::create_inspect_persistence_channel();
        let (mut shutdown_sender, shutdown_receiver) = mpsc::channel(1);
        let (_generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");
        let (_sme_server, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_proxy,
            &test_utils::fake_device_info([0; 6]),
            fake_mac_sublayer_support(),
            fake_security_support(),
            fake_spectrum_management_support_empty(),
            Arc::new(iface_tree_holder),
            WlanHasher::new(PLACEHOLDER_HASH_KEY),
            persistence_req_sender,
            shutdown_receiver,
            generic_sme_server,
        );
        pin_mut!(serve_fut);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Retrieve SME instance and close SME
        shutdown_sender.try_send(()).expect("expect sending shutdown command to succeed");

        // Verify SME future is finished
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn sme_close_endpoints() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mlme_proxy, _mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let inspector = Inspector::new();
        let iface_tree_holder = IfaceTreeHolder::new(inspector.root().create_child("sme"));
        let (persistence_req_sender, _persistence_stream) =
            test_utils::create_inspect_persistence_channel();
        let (_shutdown_sender, shutdown_receiver) = mpsc::channel(1);
        let (generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");
        let (mut sme_server, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_proxy,
            &test_utils::fake_device_info([0; 6]),
            fake_mac_sublayer_support(),
            fake_security_support(),
            fake_spectrum_management_support_empty(),
            Arc::new(iface_tree_holder),
            WlanHasher::new(PLACEHOLDER_HASH_KEY),
            persistence_req_sender,
            shutdown_receiver,
            generic_sme_server,
        );
        pin_mut!(serve_fut);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Retrieve SME instance and close SME
        let sme = assert_variant!(
            sme_server,
            SmeServer::Client(ref mut sme) => sme,
            "expected Client SME to be spawned"
        );
        let close_fut = sme.close();
        pin_mut!(close_fut);
        assert_variant!(exec.run_until_stalled(&mut close_fut), Poll::Ready(_));

        // Also close secondary SME endpoint in the Generic SME.
        drop(generic_sme_proxy);

        // Verify SME future is finished
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Ready(Err(_)));
    }

    struct GenericSmeTestHelper {
        proxy: fidl_sme::GenericSmeProxy,
        // These values must stay in scope or the SME will terminate, but they
        // are not relevant to Generic SME tests.
        _inspector: Inspector,
        _shutdown_sender: mpsc::Sender<()>,
        _persistence_stream: mpsc::Receiver<String>,
        _mlme_server: fidl::endpoints::ServerEnd<fidl_mlme::MlmeMarker>,
        // Executor goes last to avoid test shutdown failures.
        exec: fasync::TestExecutor,
    }

    fn start_generic_sme_test(
        role: fidl_common::WlanMacRole,
    ) -> (GenericSmeTestHelper, Pin<Box<impl Future<Output = Result<(), anyhow::Error>>>>) {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let inspector = Inspector::new();
        let (mlme_proxy, mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let iface_tree_holder = IfaceTreeHolder::new(inspector.root().create_child("sme"));
        let (persistence_req_sender, persistence_stream) =
            test_utils::create_inspect_persistence_channel();
        let (shutdown_sender, shutdown_receiver) = mpsc::channel(1);
        let (generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");
        let device_info = fidl_mlme::DeviceInfo { role, ..test_utils::fake_device_info([0; 6]) };
        let (_sme_server, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_proxy,
            &device_info,
            fake_mac_sublayer_support(),
            fake_security_support(),
            fake_spectrum_management_support_empty(),
            Arc::new(iface_tree_holder),
            WlanHasher::new(PLACEHOLDER_HASH_KEY),
            persistence_req_sender,
            shutdown_receiver,
            generic_sme_server,
        );
        let mut serve_fut = Box::pin(serve_fut);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        (
            GenericSmeTestHelper {
                proxy: generic_sme_proxy,
                _inspector: inspector,
                _shutdown_sender: shutdown_sender,
                _persistence_stream: persistence_stream,
                _mlme_server: mlme_server,
                exec,
            },
            serve_fut,
        )
    }

    #[test]
    fn generic_sme_get_client() {
        let (mut helper, mut serve_fut) = start_generic_sme_test(fidl_common::WlanMacRole::Client);

        let mut client_sme_fut = helper.proxy.get_client_sme();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let client_sme = assert_variant!(helper.exec.run_until_stalled(&mut client_sme_fut), Poll::Ready(Ok(Ok(sme))) => sme);
        let client_proxy = client_sme.into_proxy().unwrap();

        let mut status_fut = client_proxy.status();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            helper.exec.run_until_stalled(&mut status_fut),
            Poll::Ready(Ok(fidl_sme::ClientStatusResponse::Idle(_)))
        );
    }

    #[test]
    fn generic_sme_get_ap_from_client_fails() {
        let (mut helper, mut serve_fut) = start_generic_sme_test(fidl_common::WlanMacRole::Client);

        let mut client_sme_fut = helper.proxy.get_ap_sme();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            helper.exec.run_until_stalled(&mut client_sme_fut),
            Poll::Ready(Ok(Err(_)))
        );
    }

    #[test]
    fn generic_sme_get_ap() {
        let (mut helper, mut serve_fut) = start_generic_sme_test(fidl_common::WlanMacRole::Ap);

        let mut client_sme_fut = helper.proxy.get_ap_sme();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let ap_sme = assert_variant!(helper.exec.run_until_stalled(&mut client_sme_fut), Poll::Ready(Ok(Ok(sme))) => sme);
        let ap_proxy = ap_sme.into_proxy().unwrap();

        let mut status_fut = ap_proxy.status();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            helper.exec.run_until_stalled(&mut status_fut),
            Poll::Ready(Ok(fidl_sme::ApStatusResponse { .. }))
        );
    }

    #[test]
    fn generic_sme_get_client_from_ap_fails() {
        let (mut helper, mut serve_fut) = start_generic_sme_test(fidl_common::WlanMacRole::Ap);

        let mut client_sme_fut = helper.proxy.get_client_sme();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            helper.exec.run_until_stalled(&mut client_sme_fut),
            Poll::Ready(Ok(Err(_)))
        );
    }
}
