// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use core::sync::atomic::AtomicUsize;
use fidl_fuchsia_wlan_device_service::{self as fidl_svc, DeviceServiceRequest};
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use fuchsia_async as fasync;
use fuchsia_inspect_contrib::{auto_persist, inspect_log};
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, prelude::*};
use log::{error, info};
use std::sync::{atomic::Ordering, Arc};

use crate::device::{self, IfaceMap};
use crate::inspect;
use crate::ServiceCfg;

/// Thread-safe counter for spawned ifaces.
pub struct IfaceCounter(AtomicUsize);

impl IfaceCounter {
    pub fn new() -> Self {
        Self(AtomicUsize::new(0))
    }

    /// Provides the caller with a new unique id.
    pub fn next_iface_id(&self) -> usize {
        self.0.fetch_add(1, Ordering::SeqCst)
    }
}

pub async fn serve_device_requests(
    iface_counter: Arc<IfaceCounter>,
    cfg: ServiceCfg,
    ifaces: Arc<IfaceMap>,
    mut req_stream: fidl_svc::DeviceServiceRequestStream,
    inspect_tree: Arc<inspect::WlanstackTree>,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    persistence_req_sender: auto_persist::PersistenceReqSender,
) -> Result<(), anyhow::Error> {
    while let Some(req) = req_stream.try_next().await.context("error running DeviceService")? {
        // Note that errors from responder.send() are propagated intentionally.
        // If we fail to send a response, the only way to recover is to stop serving the
        // client and close the channel. Otherwise, the client would be left hanging
        // forever.
        match req {
            DeviceServiceRequest::AddIface { req, responder } => {
                let mut add_iface_result = add_iface(
                    req,
                    &cfg,
                    &ifaces,
                    &iface_counter,
                    &inspect_tree,
                    dev_monitor_proxy.clone(),
                    persistence_req_sender.clone(),
                )
                .await;
                responder.send(add_iface_result.status, add_iface_result.iface_id.as_mut())?;
                let serve_sme_fut = add_iface_result.result?;
                fasync::Task::spawn(serve_sme_fut).detach();
            }
        }
    }
    Ok(())
}

struct AddIfaceResult {
    result: Result<BoxFuture<'static, ()>, anyhow::Error>,
    status: i32,
    iface_id: Option<fidl_svc::AddIfaceResponse>,
}

impl AddIfaceResult {
    fn from_error(e: anyhow::Error, status: i32) -> Self {
        AddIfaceResult { result: Err(e), status, iface_id: None }
    }

    fn ok(fut: BoxFuture<'static, ()>, iface_id: u16) -> Self {
        AddIfaceResult {
            result: Ok(fut),
            status: zx::sys::ZX_OK,
            iface_id: Some(fidl_svc::AddIfaceResponse { iface_id }),
        }
    }
}

async fn add_iface(
    req: fidl_svc::AddIfaceRequest,
    cfg: &ServiceCfg,
    ifaces: &Arc<IfaceMap>,
    iface_counter: &Arc<IfaceCounter>,
    inspect_tree: &Arc<inspect::WlanstackTree>,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    persistence_req_sender: auto_persist::PersistenceReqSender,
) -> AddIfaceResult {
    // Utilize the provided MLME channel to construct a future to serve the SME.
    let mlme_channel = match fasync::Channel::from_channel(req.iface.into_channel()) {
        Ok(channel) => channel,
        Err(e) => return AddIfaceResult::from_error(e.into(), zx::sys::ZX_ERR_INTERNAL),
    };
    let mlme_proxy = fidl_mlme::MlmeProxy::new(mlme_channel);

    let id = iface_counter.next_iface_id() as u16;
    let phy_ownership =
        device::PhyOwnership { phy_id: req.phy_id, phy_assigned_id: req.assigned_iface_id };
    info!("iface #{} added ({:?})", id, phy_ownership);

    let inspect_tree = inspect_tree.clone();
    let iface_tree_holder = inspect_tree.create_iface_child(id);

    let device_info = match mlme_proxy.query_device_info().await {
        Ok(device_info) => device_info,
        Err(e) => return AddIfaceResult::from_error(e.into(), zx::sys::ZX_ERR_PEER_CLOSED),
    };

    let mac_sublayer_support = match mlme_proxy.query_mac_sublayer_support().await {
        Ok(support) => support,
        Err(e) => return AddIfaceResult::from_error(e.into(), zx::sys::ZX_ERR_PEER_CLOSED),
    };

    let security_support = match mlme_proxy.query_security_support().await {
        Ok(support) => support,
        Err(e) => return AddIfaceResult::from_error(e.into(), zx::sys::ZX_ERR_PEER_CLOSED),
    };

    let spectrum_management_support = match mlme_proxy.query_spectrum_management_support().await {
        Ok(support) => support,
        Err(e) => return AddIfaceResult::from_error(e.into(), zx::sys::ZX_ERR_PEER_CLOSED),
    };

    let serve_sme_fut = match device::create_and_serve_sme(
        cfg.clone(),
        id,
        phy_ownership,
        mlme_proxy,
        ifaces.clone(),
        inspect_tree.clone(),
        iface_tree_holder,
        device_info,
        mac_sublayer_support,
        security_support,
        spectrum_management_support,
        dev_monitor_proxy,
        persistence_req_sender,
        req.generic_sme,
    ) {
        Ok(fut) => fut,
        Err(e) => return AddIfaceResult::from_error(e, zx::sys::ZX_ERR_INTERNAL),
    };

    // Handle the Result returned by the SME future.  This enables some final cleanup and metrics
    // logging and also makes the spawned task detachable.
    let serve_sme_fut = serve_sme_fut.map(move |result| {
        let msg = match result {
            Ok(()) => {
                let msg = format!("iface {} shutdown gracefully", id);
                info!("{}", msg);
                msg
            }
            Err(e) => {
                let msg = format!("error serving iface {}: {}", id, e);
                error!("{}", msg);
                msg
            }
        };
        inspect_log!(inspect_tree.device_events.lock().get_mut(), msg: msg);
        inspect_tree.notify_iface_removed(id);
    });

    AddIfaceResult::ok(Box::pin(serve_sme_fut), id)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_helper;
    use fidl::endpoints::{create_endpoints, create_proxy};
    use fidl_fuchsia_wlan_common as fidl_common;
    use fidl_fuchsia_wlan_mlme::{self as fidl_mlme};
    use fidl_fuchsia_wlan_sme as fidl_sme;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::task::Poll;
    use pin_utils::pin_mut;
    use wlan_common::assert_variant;

    #[test]
    fn iface_counter() {
        let iface_counter = IfaceCounter::new();
        assert_eq!(0, iface_counter.next_iface_id());
        assert_eq!(1, iface_counter.next_iface_id());
        assert_eq!(2, iface_counter.next_iface_id());
        assert_eq!(3, iface_counter.next_iface_id());
    }

    // Debug is required for assert_variant.
    impl std::fmt::Debug for AddIfaceResult {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.debug_tuple("")
                .field(&self.result.is_ok())
                .field(&self.status)
                .field(&self.iface_id)
                .finish()
        }
    }

    #[test]
    fn test_add_iface() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        // Boilerplate for adding a new interface.
        let iface_map = Arc::new(IfaceMap::new());
        let iface_counter = Arc::new(IfaceCounter::new());
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();
        let (_generic_sme_proxy, generic_sme) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");

        // Construct the request.
        let (mlme_channel, mlme_receiver) =
            create_endpoints().expect("failed to create fake MLME proxy");
        let mut mlme_stream = mlme_receiver.into_stream().expect("failed to create MLME stream");
        let req = fidl_svc::AddIfaceRequest {
            phy_id: 123,
            assigned_iface_id: 456,
            iface: mlme_channel,
            generic_sme,
        };
        let fut = add_iface(
            req,
            &cfg,
            &iface_map,
            &iface_counter,
            &inspect_tree,
            dev_monitor_proxy,
            persistence_req_sender,
        );
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the PHY's information.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QueryDeviceInfo { responder }))) => {
            let mut device_info = fake_device_info();
            responder.send(&mut device_info).expect("failed to send MLME response");
        });

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the MAC sublayer features.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QueryMacSublayerSupport { responder }))) => {
            responder.send(&mut fake_mac_sublayer_support()).expect("failed to send MLME response");
        });

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the security features.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QuerySecuritySupport { responder }))) => {
            responder.send(&mut fake_security_support()).expect("failed to send MLME response");
        });

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the spectrum management features.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QuerySpectrumManagementSupport { responder }))) => {
            responder.send(&mut fake_spectrum_management_support()).expect("failed to send MLME response");
        });

        // The future should complete successfully.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(result) => {
            assert!(result.result.is_ok());
            assert_eq!(result.status, zx::sys::ZX_OK);
            assert_eq!(result.iface_id, Some(fidl_svc::AddIfaceResponse { iface_id: 0 }));
        });
    }

    #[test]
    fn test_add_iface_query_fails() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        // Boilerplate for adding a new interface.
        let iface_map = Arc::new(IfaceMap::new());
        let iface_counter = Arc::new(IfaceCounter::new());
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();
        let (_generic_sme_proxy, generic_sme) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");

        // Construct the request.
        let (mlme_channel, mlme_receiver) =
            create_endpoints().expect("failed to create fake MLME proxy");

        // Drop the receiver so that the initial device info query fails.
        drop(mlme_receiver);

        let req = fidl_svc::AddIfaceRequest {
            phy_id: 123,
            assigned_iface_id: 456,
            iface: mlme_channel,
            generic_sme,
        };
        let fut = add_iface(
            req,
            &cfg,
            &iface_map,
            &iface_counter,
            &inspect_tree,
            dev_monitor_proxy,
            persistence_req_sender,
        );
        pin_mut!(fut);

        // The future should have returned bad status here.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(result) => {
            assert!(result.result.is_err());
            assert_eq!(result.status, zx::sys::ZX_ERR_PEER_CLOSED);
            assert!(result.iface_id.is_none());
        });
    }

    #[test]
    fn test_add_iface_create_sme_fails() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");

        // Boilerplate for adding a new interface.
        let iface_map = Arc::new(IfaceMap::new());
        let iface_counter = Arc::new(IfaceCounter::new());
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let cfg = ServiceCfg { wep_supported: false, wpa1_supported: false };
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();
        let (_generic_sme_proxy, generic_sme) =
            create_proxy::<fidl_sme::GenericSmeMarker>().expect("failed to create MlmeProxy");

        // Construct the request.
        let (mlme_channel, mlme_receiver) =
            create_endpoints().expect("failed to create fake MLME proxy");
        let mut mlme_stream = mlme_receiver.into_stream().expect("failed to create MLME stream");

        let req = fidl_svc::AddIfaceRequest {
            phy_id: 123,
            assigned_iface_id: 456,
            iface: mlme_channel,
            generic_sme,
        };
        let fut = add_iface(
            req,
            &cfg,
            &iface_map,
            &iface_counter,
            &inspect_tree,
            dev_monitor_proxy,
            persistence_req_sender,
        );
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the PHY's information.
        assert_variant!(
        exec.run_until_stalled(&mut mlme_stream.next()),
        Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QueryDeviceInfo { responder }))) => {
            responder.send(&mut fake_device_info()).expect("failed to send MLME response");
        });

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the MAC sublayer features.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QueryMacSublayerSupport { responder }))) => {
            let mut support = fake_mac_sublayer_support();
            // Intentionally provide a set of features that is invalid, to cause a failure.
            // A device that is synthetic but not softmac is currently invalid (but this may change in the future.)
            support.device.is_synthetic = true;
            support.device.mac_implementation_type = fidl_common::MacImplementationType::Fullmac;
            responder.send(&mut support).expect("failed to send MLME response");
        });

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the security features.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QuerySecuritySupport { responder }))) => {
            responder.send(&mut fake_security_support()).expect("failed to send MLME response");
        });

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // The future should have requested the spectrum management features.
        assert_variant!(
            exec.run_until_stalled(&mut mlme_stream.next()),
            Poll::Ready(Some(Ok(fidl_mlme::MlmeRequest::QuerySpectrumManagementSupport { responder }))) => {
            let mut support = fake_spectrum_management_support();
            responder.send(&mut support).expect("failed to send MLME response");
        });

        // The device information should be invalid and the future should report the failure here.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(result) => {
            assert!(result.result.is_err());
            assert_eq!(result.status, zx::sys::ZX_ERR_INTERNAL);
            assert!(result.iface_id.is_none());
        });
    }

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
            sae: fidl_common::SaeFeature {
                driver_handler_supported: false,
                sme_handler_supported: true,
            },
            mfp: fidl_common::MfpFeature { supported: false },
        }
    }

    fn fake_spectrum_management_support() -> fidl_common::SpectrumManagementSupport {
        fidl_common::SpectrumManagementSupport { dfs: fidl_common::DfsFeature { supported: false } }
    }
}
