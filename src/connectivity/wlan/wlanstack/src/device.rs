// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::ControlHandle,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_inspect_contrib::{auto_persist, inspect_log},
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future::Future, select, FutureExt, StreamExt},
    log::info,
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
    wlan_common::sink::UnboundedSink,
    wlan_inspect,
    wlan_sme::{self, serve::create_sme},
};

use crate::{inspect, ServiceCfg};

/// Iface's PHY information.
#[derive(Debug, PartialEq)]
pub struct PhyOwnership {
    // Iface's global PHY ID.
    pub phy_id: u16,
    // Local ID assigned by this iface's PHY.
    pub phy_assigned_id: u16,
}

#[derive(Debug)]
pub struct NewIface {
    // Global, unique iface ID.
    pub id: u16,
    // Information about this iface's PHY.
    pub phy_ownership: PhyOwnership,
    // A proxy to communicate with the iface's underlying MLME.
    pub mlme_proxy: fidl_mlme::MlmeProxy,
}

pub struct IfaceDevice {
    pub phy_ownership: PhyOwnership,
    pub mlme_proxy: fidl_mlme::MlmeProxy,
    pub device_info: fidl_mlme::DeviceInfo,
}

pub struct IfaceMap {
    inner: Mutex<Arc<HashMap<u16, Arc<IfaceDevice>>>>,
}

impl IfaceMap {
    pub fn new() -> Self {
        IfaceMap { inner: Mutex::new(Arc::new(HashMap::new())) }
    }

    pub fn insert(&self, iface_id: u16, device: IfaceDevice) {
        let mut inner = self.inner.lock();
        let hash_map = Arc::make_mut(&mut inner);
        hash_map.insert(iface_id, Arc::new(device));
    }

    pub fn remove(&self, iface_id: &u16) {
        let mut inner = self.inner.lock();
        let hash_map = Arc::make_mut(&mut inner);
        hash_map.remove(iface_id);
    }

    #[cfg(test)]
    pub fn get(&self, iface_id: &u16) -> Option<Arc<IfaceDevice>> {
        self.inner.lock().get(iface_id).map(|v| v.clone())
    }
}

pub fn create_and_serve_sme(
    cfg: ServiceCfg,
    id: u16,
    phy_ownership: PhyOwnership,
    mlme_proxy: fidl_mlme::MlmeProxy,
    ifaces: Arc<IfaceMap>,
    inspect_tree: Arc<inspect::WlanstackTree>,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    device_info: fidl_mlme::DeviceInfo,
    mac_sublayer_support: fidl_common::MacSublayerSupport,
    security_support: fidl_common::SecuritySupport,
    spectrum_management_support: fidl_common::SpectrumManagementSupport,
    persistence_req_sender: auto_persist::PersistenceReqSender,
    generic_sme: fidl::endpoints::ServerEnd<fidl_sme::GenericSmeMarker>,
) -> Result<impl Future<Output = Result<(), Error>>, Error> {
    let (mlme_event_sender, mlme_event_receiver) = mpsc::unbounded();
    let (generic_sme_stream, generic_sme_control_handle) =
        generic_sme.into_stream_and_control_handle()?;
    let (mlme_req_stream, sme_fut) = create_sme(
        cfg.into(),
        mlme_event_receiver,
        &device_info,
        mac_sublayer_support,
        security_support,
        spectrum_management_support,
        iface_tree_holder.clone(),
        inspect_tree.hasher.clone(),
        persistence_req_sender,
        generic_sme_stream,
    );
    let forward_mlme_msgs_fut = forward_mlme_msgs(
        mlme_proxy.clone(),
        mlme_req_stream,
        UnboundedSink::new(mlme_event_sender),
    );

    info!("new iface #{} with role '{:?}'", id, device_info.role);
    inspect_log!(inspect_tree.device_events.lock().get_mut(), {
        msg: format!("new iface #{} with role '{:?}'", id, device_info.role)
    });
    if let fidl_common::WlanMacRole::Client = device_info.role {
        inspect_tree.mark_active_client_iface(id);
    }

    // For testing only: currently, all synthetic devices are softmac devices.
    // This may change in the future.
    if mac_sublayer_support.device.is_synthetic
        && mac_sublayer_support.device.mac_implementation_type
            != fidl_common::MacImplementationType::Softmac
    {
        return Err(format_err!("Synthetic devices must be SoftMAC"));
    }
    ifaces.insert(id, IfaceDevice { phy_ownership, mlme_proxy, device_info });

    Ok(async move {
        let result = select! {
            result = sme_fut.fuse() => result.map_err(|e| format_err!("error while serving SME: {}", e)),
            result = forward_mlme_msgs_fut.fuse() => result,
        };
        inspect_log!(
            inspect_tree.device_events.lock().get_mut(),
            msg: format!("iface removed: #{}", id)
        );
        inspect_tree.unmark_active_client_iface(id);
        ifaces.remove(&id);

        let epitaph = match result {
            Ok(()) => zx::Status::OK,
            Err(_) => zx::Status::CONNECTION_ABORTED,
        };
        // If this shutdown is due to the generic SME closing, this is a no-op.
        generic_sme_control_handle.shutdown_with_epitaph(epitaph);

        result
    })
}

async fn forward_mlme_msgs(
    mlme_proxy: fidl_mlme::MlmeProxy,
    mut mlme_req_stream: wlan_sme::MlmeStream,
    mlme_event_sink: UnboundedSink<fidl_mlme::MlmeEvent>,
) -> Result<(), anyhow::Error> {
    let mut mlme_event_stream = mlme_proxy.take_event_stream();
    loop {
        select! {
            mlme_event = mlme_event_stream.next().fuse() => match mlme_event {
                Some(Ok(event)) => mlme_event_sink.send(event),
                Some(Err(ref e)) if e.is_closed() => return Ok(()),
                None => return Ok(()),
                Some(Err(e)) => return Err(format_err!("Error reading an event from MLME channel: {}", e)),
            },
            mlme_req = mlme_req_stream.next().fuse() => match mlme_req {
                Some(req) => match forward_mlme_request(req, &mlme_proxy).await {
                    Ok(()) => {},
                    Err(ref e) if e.is_closed() => return Ok(()),
                    Err(e) => return Err(format_err!("Error forwarding a request from SME to MLME: {}", e)),
                },
                None => return Err(format_err!("Stream of requests from SME to MLME has ended unexpectedly")),
            },
        }
    }
}

async fn forward_mlme_request(
    req: wlan_sme::MlmeRequest,
    proxy: &fidl_mlme::MlmeProxy,
) -> Result<(), fidl::Error> {
    use wlan_sme::MlmeRequest;
    match req {
        MlmeRequest::Scan(mut req) => proxy.start_scan(&mut req),
        MlmeRequest::AuthResponse(mut resp) => proxy.authenticate_resp(&mut resp),
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
        MlmeRequest::GetIfaceCounterStats(responder) => {
            proxy.get_iface_counter_stats().await.map(|resp| responder.respond(resp))
        }
        MlmeRequest::GetIfaceHistogramStats(responder) => {
            proxy.get_iface_histogram_stats().await.map(|resp| responder.respond(resp))
        }
        MlmeRequest::SaeHandshakeResp(mut resp) => proxy.sae_handshake_resp(&mut resp),
        MlmeRequest::SaeFrameTx(mut frame) => proxy.sae_frame_tx(&mut frame),
        MlmeRequest::WmmStatusReq => proxy.wmm_status_req(),
        MlmeRequest::FinalizeAssociation(mut cap) => proxy.finalize_association_req(&mut cap),
        MlmeRequest::QueryDeviceInfo(responder) => {
            proxy.query_device_info().await.map(|resp| responder.respond(resp))
        }
        MlmeRequest::QueryDiscoverySupport(responder) => {
            proxy.query_discovery_support().await.map(|resp| responder.respond(resp))
        }
        MlmeRequest::QueryMacSublayerSupport(responder) => {
            proxy.query_mac_sublayer_support().await.map(|resp| responder.respond(resp))
        }
        MlmeRequest::QuerySecuritySupport(responder) => {
            proxy.query_security_support().await.map(|resp| responder.respond(resp))
        }
        MlmeRequest::QuerySpectrumManagementSupport(responder) => {
            proxy.query_spectrum_management_support().await.map(|resp| responder.respond(resp))
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_helper,
        fidl::endpoints::create_proxy,
        fidl_mlme::MlmeMarker,
        fuchsia_async as fasync,
        fuchsia_inspect::assert_data_tree,
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::{
            assert_variant, test_utils::fake_features::fake_spectrum_management_support_empty,
        },
    };

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_common::WlanMacRole::Client,
            bands: vec![],
            sta_addr: [0xAC; 6],
            softmac_hardware_capability: 0,
            qos_capable: false,
        }
    }

    fn fake_mac_sublayer_support() -> fidl_common::MacSublayerSupport {
        fidl_common::MacSublayerSupport {
            rate_selection_offload: fidl_common::RateSelectionOffloadExtension { supported: false },
            data_plane: fidl_common::DataPlaneExtension {
                data_plane_type: fidl_common::DataPlaneType::EthernetDevice,
            },
            device: fidl_common::DeviceExtension {
                is_synthetic: false,
                mac_implementation_type: fidl_common::MacImplementationType::Fullmac,
                tx_status_report_supported: false,
            },
        }
    }

    fn fake_security_support() -> fidl_common::SecuritySupport {
        fidl_common::SecuritySupport {
            mfp: fidl_common::MfpFeature { supported: false },
            sae: fidl_common::SaeFeature {
                driver_handler_supported: false,
                sme_handler_supported: false,
            },
        }
    }

    #[test]
    fn query_serve_with_sme_channel() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mlme_proxy, _mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let iface_tree_holder = inspect_tree.create_iface_child(1);
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();
        let (generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");

        // Assert that the IfaceMap is initially empty.
        assert!(iface_map.get(&5).is_none());

        let serve_fut = create_and_serve_sme(
            ServiceCfg::default(),
            5,
            PhyOwnership { phy_id: 1, phy_assigned_id: 2 },
            mlme_proxy,
            iface_map.clone(),
            inspect_tree.clone(),
            iface_tree_holder,
            fake_device_info(),
            fake_mac_sublayer_support(),
            fake_security_support(),
            fake_spectrum_management_support_empty(),
            persistence_req_sender,
            generic_sme_server,
        )
        .expect("failed to create SME");

        // Assert that the IfaceMap now has an entry for the new iface.
        assert!(iface_map.get(&5).is_some());

        pin_mut!(serve_fut);

        // Progress to cause SME creation and serving.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Drop connections to the SME.
        drop(generic_sme_proxy);

        // Progress SME serving to completion and verify iface was deleted
        let fut_result = exec.run_until_stalled(&mut serve_fut);
        assert_variant!(fut_result, Poll::Ready(_), "expected SME serving to be terminated");
        assert!(iface_map.get(&5).is_none());

        assert_data_tree!(inspect_tree.inspector, root: contains {
            device_events: {
                "0": contains { msg: "new iface #5 with role 'Client'" },
                "1": contains { msg: "iface removed: #5" },
            },
        });
    }

    #[test]
    fn sme_shutdown_by_generic_sme() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mlme_proxy, _mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let iface_tree_holder = inspect_tree.create_iface_child(1);
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();
        let (generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");

        // Assert that the IfaceMap is initially empty.
        assert!(iface_map.get(&5).is_none());

        let serve_fut = create_and_serve_sme(
            ServiceCfg::default(),
            5,
            PhyOwnership { phy_id: 1, phy_assigned_id: 2 },
            mlme_proxy,
            iface_map.clone(),
            inspect_tree.clone(),
            iface_tree_holder,
            fake_device_info(),
            fake_mac_sublayer_support(),
            fake_security_support(),
            fake_spectrum_management_support_empty(),
            persistence_req_sender,
            generic_sme_server,
        )
        .expect("failed to create SME");

        // Assert that the IfaceMap now has an entry for the new iface.
        assert!(iface_map.get(&5).is_some());

        pin_mut!(serve_fut);

        // Progress to cause SME creation and serving.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // SME closes when generic SME is closed.
        drop(generic_sme_proxy);

        // The SME future should complete successfully.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn sme_shutdown_by_mlme_proxy() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mlme_proxy, mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let iface_tree_holder = inspect_tree.create_iface_child(1);
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();
        let (generic_sme_proxy, generic_sme_server) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");

        // Assert that the IfaceMap is initially empty.
        assert!(iface_map.get(&5).is_none());

        let serve_fut = create_and_serve_sme(
            ServiceCfg::default(),
            5,
            PhyOwnership { phy_id: 1, phy_assigned_id: 2 },
            mlme_proxy,
            iface_map.clone(),
            inspect_tree.clone(),
            iface_tree_holder,
            fake_device_info(),
            fake_mac_sublayer_support(),
            fake_security_support(),
            fake_spectrum_management_support_empty(),
            persistence_req_sender,
            generic_sme_server,
        )
        .expect("failed to create SME");

        // Assert that the IfaceMap now has an entry for the new iface.
        assert!(iface_map.get(&5).is_some());

        pin_mut!(serve_fut);

        // Progress to cause SME creation and serving.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        drop(mlme_server);

        // The SME future should complete successfully.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Ready(Ok(())));

        // Verify that the generic SME stream completes with an epitaph.
        let mut generic_sme_stream = generic_sme_proxy.take_event_stream();
        assert_variant!(
            exec.run_until_stalled(&mut generic_sme_stream.next()),
            Poll::Ready(Some(Err(fidl::Error::ClientChannelClosed { status, .. }))) => {
                assert_eq!(status, zx::Status::OK);
            }
        );
    }

    #[test]
    fn test_new_iface_map() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let iface_map = IfaceMap::new();
        let hashmap = iface_map.inner.lock();
        assert!(hashmap.is_empty());
    }

    #[test]
    fn test_iface_map_insert_remove() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let iface_map = IfaceMap::new();
        let (mlme_proxy, _) = create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                mlme_proxy,
                device_info: fake_device_info(),
            },
        );

        {
            let hashmap = iface_map.inner.lock();
            assert!(!hashmap.is_empty());
        }

        iface_map.remove(&5);

        {
            let hashmap = iface_map.inner.lock();
            assert!(hashmap.is_empty());
        }
    }

    #[test]
    fn test_iface_map_remove_nonexistent_iface() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let iface_map = IfaceMap::new();
        let (mlme_proxy, _) = create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                mlme_proxy,
                device_info: fake_device_info(),
            },
        );

        {
            let hashmap = iface_map.inner.lock();
            assert!(!hashmap.is_empty());
        }

        iface_map.remove(&0);

        {
            let hashmap = iface_map.inner.lock();
            assert!(!hashmap.is_empty());
        }
    }

    #[test]
    fn test_iface_map_insert_get() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let iface_map = IfaceMap::new();
        let (mlme_proxy, _) = create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                mlme_proxy,
                device_info: fake_device_info(),
            },
        );

        let map_entry = iface_map.get(&5).expect("iface is missing from map");
        assert_eq!(map_entry.phy_ownership, PhyOwnership { phy_id: 0, phy_assigned_id: 0 });
        assert_eq!(map_entry.device_info, fake_device_info());
    }

    #[test]
    fn test_iface_map_insert_get_nonexistent_iface() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");

        let iface_map = IfaceMap::new();
        let (mlme_proxy, _) = create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                mlme_proxy,
                device_info: fake_device_info(),
            },
        );

        assert!(iface_map.get(&0).is_none());
    }
}
