// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ap;
pub mod client;
pub mod mesh;

use {
    crate::{MlmeRequest, MlmeStream, Station},
    anyhow::format_err,
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEventStream, MlmeProxy},
    fuchsia_inspect_contrib::auto_persist,
    futures::{channel::mpsc, future::FutureObj, prelude::*, select},
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

pub enum SmeServer {
    Client(ClientSmeServer),
    Ap(ApSmeServer),
    Mesh(MeshSmeServer),
}

pub fn create_sme(
    cfg: crate::Config,
    mlme_proxy: fidl_mlme::MlmeProxy,
    device_info: &fidl_mlme::DeviceInfo,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    hasher: WlanHasher,
    persistence_req_sender: auto_persist::PersistenceReqSender,
    mut shutdown_receiver: mpsc::Receiver<()>,
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
            let fut = ap::serve(mlme_proxy, device_info, event_stream, receiver);
            (SmeServer::Ap(sender), FutureObj::new(Box::new(fut)))
        }
        fidl_common::WlanMacRole::Mesh => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = mesh::serve(mlme_proxy, device_info, event_stream, receiver);
            (SmeServer::Mesh(sender), FutureObj::new(Box::new(fut)))
        }
    };
    let sme_fut_with_shutdown = async move {
        select! {
            sme_fut = sme_fut.fuse() => sme_fut,
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
        super::*, crate::test_utils, fidl::endpoints::create_proxy, fidl_mlme::MlmeMarker,
        fuchsia_async as fasync, fuchsia_inspect::Inspector, futures::task::Poll,
        pin_utils::pin_mut, std::sync::Arc, wlan_common::assert_variant,
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
        let (_sme_server, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_proxy,
            &test_utils::fake_device_info([0; 6]),
            Arc::new(iface_tree_holder),
            WlanHasher::new(PLACEHOLDER_HASH_KEY),
            persistence_req_sender,
            shutdown_receiver,
        );
        pin_mut!(serve_fut);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Retrieve SME instance and close SME
        shutdown_sender.try_send(()).expect("expect sending shutdown command to succeed");

        // Verify SME future is finished
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn sme_close_endpoint() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mlme_proxy, _mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let inspector = Inspector::new();
        let iface_tree_holder = IfaceTreeHolder::new(inspector.root().create_child("sme"));
        let (persistence_req_sender, _persistence_stream) =
            test_utils::create_inspect_persistence_channel();
        let (_shutdown_sender, shutdown_receiver) = mpsc::channel(1);
        let (mut sme_server, serve_fut) = create_sme(
            crate::Config::default(),
            mlme_proxy,
            &test_utils::fake_device_info([0; 6]),
            Arc::new(iface_tree_holder),
            WlanHasher::new(PLACEHOLDER_HASH_KEY),
            persistence_req_sender,
            shutdown_receiver,
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

        // Verify SME future is finished
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Ready(Err(_)));
    }
}
