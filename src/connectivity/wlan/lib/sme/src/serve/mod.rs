// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ap;
pub mod client;
pub mod mesh;

use {
    crate::{MlmeEventStream, MlmeStream, Station},
    anyhow::format_err,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
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
    mut telemetry_server: Option<
        mpsc::UnboundedSender<fidl::endpoints::ServerEnd<fidl_sme::TelemetryMarker>>,
    >,
) -> Result<(), anyhow::Error> {
    let mut generic_sme_stream = match generic_sme.into_stream() {
        Ok(stream) => stream,
        Err(e) => return Err(format_err!("Failed to handle Generic SME stream: {}", e)),
    };
    loop {
        match generic_sme_stream.next().await {
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
                        let mut response = if let Some(telemetry_server) = telemetry_server.as_mut()
                        {
                            let (client_end, server_end) =
                                create_endpoints::<fidl_sme::TelemetryMarker>()
                                    .expect("failed to create Telemetry");
                            telemetry_server
                                .send(server_end)
                                .await
                                .map(|_| client_end)
                                .map_err(|_| zx::Status::PEER_CLOSED.into_raw())
                        } else {
                            warn!("Requested unsupported SME telemetry API");
                            Err(zx::Status::NOT_SUPPORTED.into_raw())
                        };
                        responder.send(&mut response)
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
    mlme_event_stream: MlmeEventStream,
    device_info: &fidl_mlme::DeviceInfo,
    mac_sublayer_support: fidl_common::MacSublayerSupport,
    security_support: fidl_common::SecuritySupport,
    spectrum_management_support: fidl_common::SpectrumManagementSupport,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    hasher: WlanHasher,
    persistence_req_sender: auto_persist::PersistenceReqSender,
    mut shutdown_receiver: mpsc::Receiver<()>,
    generic_sme: fidl::endpoints::ServerEnd<fidl_sme::GenericSmeMarker>,
) -> (SmeServer, MlmeStream, impl Future<Output = Result<(), anyhow::Error>>) {
    let device_info = device_info.clone();
    let (server, mlme_req_stream, telemetry_sender, sme_fut) = match device_info.role {
        fidl_common::WlanMacRole::Client => {
            let (telemetry_endpoint_sender, telemetry_endpoint_receiver) = mpsc::unbounded();
            let (sender, receiver) = mpsc::unbounded();
            let (mlme_req_stream, fut) = client::serve(
                cfg,
                device_info,
                mac_sublayer_support,
                security_support,
                spectrum_management_support,
                mlme_event_stream,
                receiver,
                telemetry_endpoint_receiver,
                iface_tree_holder,
                hasher,
                persistence_req_sender,
            );
            (
                SmeServer::Client(sender),
                mlme_req_stream,
                Some(telemetry_endpoint_sender),
                FutureObj::new(Box::new(fut)),
            )
        }
        fidl_common::WlanMacRole::Ap => {
            let (sender, receiver) = mpsc::unbounded();
            let (mlme_req_stream, fut) =
                ap::serve(device_info, mac_sublayer_support, mlme_event_stream, receiver);
            (SmeServer::Ap(sender), mlme_req_stream, None, FutureObj::new(Box::new(fut)))
        }
        fidl_common::WlanMacRole::Mesh => {
            let (sender, receiver) = mpsc::unbounded();
            let (mlme_req_stream, fut) = mesh::serve(device_info, mlme_event_stream, receiver);
            (SmeServer::Mesh(sender), mlme_req_stream, None, FutureObj::new(Box::new(fut)))
        }
    };
    let generic_sme_fut = serve_generic_sme(generic_sme, server.clone(), telemetry_sender);
    let sme_fut_with_shutdown = async move {
        select! {
            sme_fut = sme_fut.fuse() => sme_fut,
            generic_sme_fut = generic_sme_fut.fuse() => generic_sme_fut,
            _ = shutdown_receiver.select_next_some() => Ok(()),
        }
    };
    (server, mlme_req_stream, sme_fut_with_shutdown)
}

// The returned future successfully terminates when MLME closes the channel
async fn serve_mlme_sme<STA, TS>(
    mut event_stream: MlmeEventStream,
    station: Arc<Mutex<STA>>,
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
                Some(mlme_event) => station.lock().unwrap().on_mlme_event(mlme_event),
                None => return Ok(()),
            },
            timeout = timeout_stream.next() => match timeout {
                Some(timed_event) => station.lock().unwrap().on_timeout(timed_event),
                None => return Err(format_err!("SME timer stream has ended unexpectedly")),
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils,
        fidl::endpoints::create_proxy,
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
        let (_mlme_event_sender, mlme_event_stream) = mpsc::unbounded();
        let inspector = Inspector::new();
        let iface_tree_holder = IfaceTreeHolder::new(inspector.root().create_child("sme"));
        let (persistence_req_sender, _persistence_stream) =
            test_utils::create_inspect_persistence_channel();
        let (mut shutdown_sender, shutdown_receiver) = mpsc::channel(1);
        let (_generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");
        let (_sme_server, _mlme_req_stream, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_event_stream,
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
        let (_mlme_event_sender, mlme_event_stream) = mpsc::unbounded();
        let inspector = Inspector::new();
        let iface_tree_holder = IfaceTreeHolder::new(inspector.root().create_child("sme"));
        let (persistence_req_sender, _persistence_stream) =
            test_utils::create_inspect_persistence_channel();
        let (_shutdown_sender, shutdown_receiver) = mpsc::channel(1);
        let (generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");
        let (mut sme_server, _mlme_req_stream, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_event_stream,
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
        mlme_req_stream: MlmeStream,

        // These values must stay in scope or the SME will terminate, but they
        // are not relevant to Generic SME tests.
        _inspector: Inspector,
        _shutdown_sender: mpsc::Sender<()>,
        _persistence_stream: mpsc::Receiver<String>,
        _mlme_event_sender: mpsc::UnboundedSender<crate::MlmeEvent>,
        // Executor goes last to avoid test shutdown failures.
        exec: fasync::TestExecutor,
    }

    fn start_generic_sme_test(
        role: fidl_common::WlanMacRole,
    ) -> (GenericSmeTestHelper, Pin<Box<impl Future<Output = Result<(), anyhow::Error>>>>) {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let inspector = Inspector::new();
        let (mlme_event_sender, mlme_event_stream) = mpsc::unbounded();
        let iface_tree_holder = IfaceTreeHolder::new(inspector.root().create_child("sme"));
        let (persistence_req_sender, persistence_stream) =
            test_utils::create_inspect_persistence_channel();
        let (shutdown_sender, shutdown_receiver) = mpsc::channel(1);
        let (generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");
        let device_info = fidl_mlme::DeviceInfo { role, ..test_utils::fake_device_info([0; 6]) };
        let (_sme_server, mlme_req_stream, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_event_stream,
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
                mlme_req_stream,
                _inspector: inspector,
                _shutdown_sender: shutdown_sender,
                _persistence_stream: persistence_stream,
                _mlme_event_sender: mlme_event_sender,
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

    #[test]
    fn generic_sme_get_histogram_stats_for_client() {
        let (mut helper, mut serve_fut) = start_generic_sme_test(fidl_common::WlanMacRole::Client);

        let mut telemetry_fut = helper.proxy.get_sme_telemetry();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let telemetry = assert_variant!(helper.exec.run_until_stalled(&mut telemetry_fut), Poll::Ready(Ok(Ok(telemetry))) => telemetry);
        let telemetry_proxy = telemetry.into_proxy().unwrap();

        // Forward request to MLME.
        let mut histogram_fut = telemetry_proxy.get_histogram_stats();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Mock response from MLME. Use a fake error code to make the response easily verifiable.
        let histogram_req = assert_variant!(helper.exec.run_until_stalled(&mut helper.mlme_req_stream.next()), Poll::Ready(Some(req)) => req);
        let histogram_responder = assert_variant!(histogram_req, crate::MlmeRequest::GetIfaceHistogramStats(responder) => responder);
        histogram_responder.respond(fidl_mlme::GetIfaceHistogramStatsResponse::ErrorStatus(1337));

        // Verify that the response made it to us without alteration.
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let histogram_result = assert_variant!(helper.exec.run_until_stalled(&mut histogram_fut), Poll::Ready(Ok(histogram_result)) => histogram_result);
        assert_eq!(histogram_result, Err(1337));
    }

    #[test]
    fn generic_sme_get_counter_stats_for_client() {
        let (mut helper, mut serve_fut) = start_generic_sme_test(fidl_common::WlanMacRole::Client);

        let mut telemetry_fut = helper.proxy.get_sme_telemetry();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let telemetry = assert_variant!(helper.exec.run_until_stalled(&mut telemetry_fut), Poll::Ready(Ok(Ok(telemetry))) => telemetry);
        let telemetry_proxy = telemetry.into_proxy().unwrap();

        // Forward request to MLME.
        let mut counter_fut = telemetry_proxy.get_counter_stats();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Mock response from MLME. Use a fake error code to make the response easily verifiable.
        let counter_req = assert_variant!(helper.exec.run_until_stalled(&mut helper.mlme_req_stream.next()), Poll::Ready(Some(req)) => req);
        let counter_responder = assert_variant!(counter_req, crate::MlmeRequest::GetIfaceCounterStats(responder) => responder);
        counter_responder.respond(fidl_mlme::GetIfaceCounterStatsResponse::ErrorStatus(1337));

        // Verify that the response made it to us without alteration.
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let counter_result = assert_variant!(helper.exec.run_until_stalled(&mut counter_fut), Poll::Ready(Ok(counter_result)) => counter_result);
        assert_eq!(counter_result, Err(1337));
    }

    #[test]
    fn generic_sme_get_telemetry_for_ap_fails() {
        let (mut helper, mut serve_fut) = start_generic_sme_test(fidl_common::WlanMacRole::Ap);

        let mut telemetry_fut = helper.proxy.get_sme_telemetry();
        assert_variant!(helper.exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(helper.exec.run_until_stalled(&mut telemetry_fut), Poll::Ready(Ok(Err(_))));
    }
}
