// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_inspect_contrib::{auto_persist, inspect_log},
    futures::{channel::mpsc, future::Future},
    log::info,
    parking_lot::Mutex,
    std::{collections::HashMap, sync::Arc},
    wlan_inspect,
    wlan_sme::serve::{create_sme, SmeServer},
};

use crate::{inspect, ServiceCfg};

pub type ShutdownSender = mpsc::Sender<()>;

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
    pub sme_server: SmeServer,
    pub mlme_proxy: fidl_mlme::MlmeProxy,
    pub device_info: fidl_mlme::DeviceInfo,
    pub shutdown_sender: ShutdownSender,
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

    pub fn get_snapshot(&self) -> Arc<HashMap<u16, Arc<IfaceDevice>>> {
        self.inner.lock().clone()
    }

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
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    persistence_req_sender: auto_persist::PersistenceReqSender,
) -> Result<impl Future<Output = Result<(), Error>>, Error> {
    let (shutdown_sender, shutdown_receiver) = mpsc::channel(1);
    let (sme, sme_fut) = create_sme(
        cfg.into(),
        mlme_proxy.clone(),
        &device_info,
        iface_tree_holder.clone(),
        inspect_tree.hasher.clone(),
        persistence_req_sender,
        shutdown_receiver,
    );

    info!("new iface #{} with role '{:?}'", id, device_info.role);
    inspect_log!(inspect_tree.device_events.lock().get_mut(), {
        msg: format!("new iface #{} with role '{:?}'", id, device_info.role)
    });
    if let fidl_common::WlanMacRole::Client = device_info.role {
        inspect_tree.mark_active_client_iface(id);
    }
    let is_softmac = device_info.driver_features.contains(&fidl_common::DriverFeature::TempSoftmac);
    // For testing only: All synthetic devices are softmac devices.
    if device_info.driver_features.contains(&fidl_common::DriverFeature::Synth) && !is_softmac {
        return Err(format_err!("Synthetic devices must be SoftMAC"));
    }
    ifaces.insert(
        id,
        IfaceDevice { phy_ownership, sme_server: sme, mlme_proxy, device_info, shutdown_sender },
    );

    Ok(async move {
        let result = sme_fut.await.map_err(|e| format_err!("error while serving SME: {}", e));
        inspect_log!(
            inspect_tree.device_events.lock().get_mut(),
            msg: format!("iface removed: #{}", id)
        );
        inspect_tree.unmark_active_client_iface(id);
        ifaces.remove(&id);

        // Upon any error associated with the iface in wlanstack, the iface should be destroyed
        // since it can no longer being managed by wlanstack. This includes either the associated
        // MLME or SME FIDL channels being closed.
        let mut req = fidl_fuchsia_wlan_device_service::DestroyIfaceRequest { iface_id: id };
        if let Err(e) = dev_monitor_proxy.destroy_iface(&mut req).await {
            info!("unable to inform DeviceMonitor of interface removal: {:?}", e);
        }

        result
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::test_helper, fidl::endpoints::create_proxy, fidl_mlme::MlmeMarker,
        fuchsia_async as fasync, fuchsia_inspect::assert_data_tree, futures::channel::mpsc,
        futures::future::join, futures::sink::SinkExt, futures::task::Poll, futures::StreamExt,
        pin_utils::pin_mut, wlan_common::assert_variant,
    };

    fn fake_device_info() -> fidl_mlme::DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_common::WlanMacRole::Client,
            bands: vec![],
            sta_addr: [0xAC; 6],
            driver_features: vec![],
            softmac_hardware_capability: 0,
            qos_capable: false,
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
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();

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
            dev_monitor_proxy,
            persistence_req_sender,
        )
        .expect("failed to create SME");

        // Assert that the IfaceMap now has an entry for the new iface.
        assert!(iface_map.get(&5).is_some());

        pin_mut!(serve_fut);

        // Progress to cause SME creation and serving.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Retrieve SME instance and close SME (iface must be acquired).
        let mut iface = iface_map.get(&5).expect("expected iface");
        iface_map.remove(&5);
        let mut_iface = Arc::get_mut(&mut iface).expect("error yielding iface");
        let sme = assert_variant!(
            mut_iface.sme_server,
            SmeServer::Client(ref mut sme) => sme,
            "expected Client SME to be spawned"
        );
        let close_fut = sme.close();
        pin_mut!(close_fut);
        let fut_result = exec.run_until_stalled(&mut close_fut);
        assert_variant!(fut_result, Poll::Ready(_), "expected closing SME to succeed");

        // Insert iface back into map.
        let (mlme_proxy, _) = create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let (sender, _) = mpsc::unbounded();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                mlme_proxy,
                device_info: fake_device_info(),
                shutdown_sender,
            },
        );
        iface_map.get(&5).expect("expected iface");

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
    fn sme_shutdown_signal() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mlme_proxy, _mlme_server) =
            create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let iface_map = IfaceMap::new();
        let iface_map = Arc::new(iface_map);
        let (inspect_tree, _persistence_stream) = test_helper::fake_inspect_tree();
        let iface_tree_holder = inspect_tree.create_iface_child(1);
        let (dev_monitor_proxy, dev_monitor_server) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let mut dev_monitor_stream = dev_monitor_server
            .into_stream()
            .expect("failed to create DeviceMonitor request stream");
        let (persistence_req_sender, _persistence_stream) =
            test_helper::create_inspect_persistence_channel();

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
            dev_monitor_proxy,
            persistence_req_sender,
        )
        .expect("failed to create SME");

        // Assert that the IfaceMap now has an entry for the new iface.
        assert!(iface_map.get(&5).is_some());

        pin_mut!(serve_fut);

        // Progress to cause SME creation and serving.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // SME closes on shutdown.
        let mut shutdown_sender = iface_map.get(&5).unwrap().shutdown_sender.clone();
        let shutdown_signal = shutdown_sender.send(());
        let mut shutdown_fut = join(serve_fut, shutdown_signal);
        assert_variant!(exec.run_until_stalled(&mut shutdown_fut), Poll::Pending);

        // Verify that a notification is sent to DeviceMonitor.
        assert_variant!(
            exec.run_until_stalled(&mut dev_monitor_stream.next()),
            Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::DestroyIface {
                req,
                responder,
            }))) => {
            assert_eq!(req, fidl_fuchsia_wlan_device_service::DestroyIfaceRequest { iface_id: 5 });
            responder.send(0).expect("failed to send result to SME fut.");
        });

        // The SME future should complete successfully.
        assert_variant!(exec.run_until_stalled(&mut shutdown_fut), Poll::Ready((Ok(()), Ok(()))));
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
        let (sender, _) = mpsc::unbounded();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                mlme_proxy,
                device_info: fake_device_info(),
                shutdown_sender,
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
        let (sender, _) = mpsc::unbounded();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                mlme_proxy,
                device_info: fake_device_info(),
                shutdown_sender,
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
        let (sender, _) = mpsc::unbounded();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                mlme_proxy,
                device_info: fake_device_info(),
                shutdown_sender,
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
        let (sender, _) = mpsc::unbounded();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                mlme_proxy,
                device_info: fake_device_info(),
                shutdown_sender,
            },
        );

        assert!(iface_map.get(&0).is_none());
    }

    #[test]
    fn test_get_snapshot() {
        let _exec = fasync::TestExecutor::new().expect("failed to create an executor");

        // The initial snapshot should be empty.
        let iface_map = IfaceMap::new();
        let hashmap = iface_map.get_snapshot();
        assert!(hashmap.is_empty());

        // Add an entry to the map and request another snapshot.  This one should contain a single
        // interface record.
        let (mlme_proxy, _) = create_proxy::<MlmeMarker>().expect("failed to create MlmeProxy");
        let (sender, _) = mpsc::unbounded();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                mlme_proxy,
                device_info: fake_device_info(),
                shutdown_sender,
            },
        );

        let hashmap = iface_map.get_snapshot();
        assert!(!hashmap.is_empty());
        let keys: Vec<u16> = hashmap.keys().cloned().collect();
        assert_eq!(keys.len(), 1);
        assert_eq!(keys[0], 5);

        // Remove the interface and again expect the snapshot to be empty.
        iface_map.remove(&5);
        let hashmap = iface_map.get_snapshot();
        assert!(hashmap.is_empty());
    }
}
