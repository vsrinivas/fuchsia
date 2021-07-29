// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_wlan_common::DriverFeature,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceInfo},
    fuchsia_cobalt::CobaltSender,
    fuchsia_inspect_contrib::inspect_log,
    futures::{
        channel::mpsc,
        future::{Future, FutureExt, FutureObj},
        select,
        stream::{Stream, StreamExt},
    },
    log::info,
    parking_lot::Mutex,
    std::{collections::HashMap, marker::Unpin, sync::Arc},
    wlan_common::hasher::WlanHasher,
    wlan_inspect,
    wlan_sme::{self, clone_utils},
};

use crate::{
    inspect,
    mlme_query_proxy::MlmeQueryProxy,
    station,
    stats_scheduler::{self, StatsScheduler},
    ServiceCfg,
};

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

pub type ClientSmeServer = mpsc::UnboundedSender<super::station::client::Endpoint>;
pub type ApSmeServer = mpsc::UnboundedSender<super::station::ap::Endpoint>;
pub type MeshSmeServer = mpsc::UnboundedSender<super::station::mesh::Endpoint>;
pub type ShutdownSender = mpsc::Sender<()>;

pub enum SmeServer {
    Client(ClientSmeServer),
    Ap(ApSmeServer),
    Mesh(MeshSmeServer),
}

pub struct IfaceDevice {
    pub phy_ownership: PhyOwnership,
    pub sme_server: SmeServer,
    pub stats_sched: StatsScheduler,
    pub mlme_query: MlmeQueryProxy,
    pub device_info: DeviceInfo,
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
    cobalt_sender: CobaltSender,
    cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    device_info: DeviceInfo,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
) -> Result<impl Future<Output = Result<(), Error>>, Error> {
    let event_stream = mlme_proxy.take_event_stream();
    let (stats_sched, stats_reqs) = stats_scheduler::create_scheduler();
    let (shutdown_sender, shutdown_receiver) = mpsc::channel(1);
    let (sme, sme_fut) = create_sme(
        cfg,
        mlme_proxy.clone(),
        event_stream,
        &device_info,
        stats_reqs,
        cobalt_sender,
        cobalt_1dot1_proxy,
        inspect_tree.clone(),
        iface_tree_holder.clone(),
        inspect_tree.hasher.clone(),
        shutdown_receiver,
    );

    info!("new iface #{} with role '{:?}'", id, device_info.role);
    inspect_log!(inspect_tree.device_events.lock().get_mut(), {
        msg: format!("new iface #{} with role '{:?}'", id, device_info.role)
    });
    if let fidl_mlme::MacRole::Client = device_info.role {
        inspect_tree.mark_active_client_iface(id, ifaces.clone(), iface_tree_holder.clone());
    }
    let mlme_query = MlmeQueryProxy::new(mlme_proxy);
    let is_softmac = device_info.driver_features.contains(&DriverFeature::TempSoftmac);
    // For testing only: All synthetic devices are softmac devices.
    if device_info.driver_features.contains(&DriverFeature::Synth) && !is_softmac {
        return Err(format_err!("Synthetic devices must be SoftMAC"));
    }
    ifaces.insert(
        id,
        IfaceDevice {
            phy_ownership,
            sme_server: sme,
            stats_sched,
            mlme_query,
            device_info,
            shutdown_sender,
        },
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

fn create_sme<S>(
    cfg: ServiceCfg,
    proxy: fidl_mlme::MlmeProxy,
    event_stream: fidl_mlme::MlmeEventStream,
    device_info: &DeviceInfo,
    stats_requests: S,
    cobalt_sender: CobaltSender,
    cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    inspect_tree: Arc<inspect::WlanstackTree>,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    hasher: WlanHasher,
    mut shutdown_receiver: mpsc::Receiver<()>,
) -> (SmeServer, impl Future<Output = Result<(), Error>>)
where
    S: Stream<Item = stats_scheduler::StatsRequest> + Send + Unpin + 'static,
{
    let device_info = clone_utils::clone_device_info(device_info);
    let (server, sme_fut) = match device_info.role {
        fidl_mlme::MacRole::Client => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = station::client::serve(
                cfg.into(),
                proxy,
                device_info,
                event_stream,
                receiver,
                stats_requests,
                cobalt_sender,
                cobalt_1dot1_proxy,
                inspect_tree,
                iface_tree_holder,
                hasher,
            );
            (SmeServer::Client(sender), FutureObj::new(Box::new(fut)))
        }
        fidl_mlme::MacRole::Ap => {
            let (sender, receiver) = mpsc::unbounded();
            let fut =
                station::ap::serve(proxy, device_info, event_stream, receiver, stats_requests);
            (SmeServer::Ap(sender), FutureObj::new(Box::new(fut)))
        }
        fidl_mlme::MacRole::Mesh => {
            let (sender, receiver) = mpsc::unbounded();
            let fut =
                station::mesh::serve(proxy, device_info, event_stream, receiver, stats_requests);
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_helper,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_mlme::MlmeMarker,
        fuchsia_async as fasync,
        fuchsia_cobalt::{self, CobaltSender},
        fuchsia_inspect::assert_data_tree,
        futures::channel::mpsc,
        futures::future::join,
        futures::sink::SinkExt,
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    fn fake_device_info() -> DeviceInfo {
        fidl_mlme::DeviceInfo {
            role: fidl_mlme::MacRole::Client,
            bands: vec![],
            sta_addr: [0xAC; 6],
            driver_features: vec![],
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
        let (sender, _receiver) = mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        let (cobalt_1dot1_proxy, _) =
            create_proxy::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy");
        let (dev_monitor_proxy, _) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");

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
            cobalt_sender,
            cobalt_1dot1_proxy,
            fake_device_info(),
            dev_monitor_proxy,
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
        let (stats_sched, _) = stats_scheduler::create_scheduler();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                stats_sched,
                mlme_query: MlmeQueryProxy::new(mlme_proxy),
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
        let (sender, _receiver) = mpsc::channel(1);
        let cobalt_sender = CobaltSender::new(sender);
        let (cobalt_1dot1_proxy, _) =
            create_proxy::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                .expect("failed to create Cobalt 1.1 proxy");
        let (dev_monitor_proxy, dev_monitor_server) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create DeviceMonitor proxy");
        let mut dev_monitor_stream = dev_monitor_server
            .into_stream()
            .expect("failed to create DeviceMonitor request stream");

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
            cobalt_sender,
            cobalt_1dot1_proxy,
            fake_device_info(),
            dev_monitor_proxy,
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
        let (stats_sched, _) = stats_scheduler::create_scheduler();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                stats_sched,
                mlme_query: MlmeQueryProxy::new(mlme_proxy),
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
        let (stats_sched, _) = stats_scheduler::create_scheduler();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                stats_sched,
                mlme_query: MlmeQueryProxy::new(mlme_proxy),
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
        let (stats_sched, _) = stats_scheduler::create_scheduler();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                stats_sched,
                mlme_query: MlmeQueryProxy::new(mlme_proxy),
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
        let (stats_sched, _) = stats_scheduler::create_scheduler();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                stats_sched,
                mlme_query: MlmeQueryProxy::new(mlme_proxy),
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
        let (stats_sched, _) = stats_scheduler::create_scheduler();
        let (shutdown_sender, _) = mpsc::channel(1);
        iface_map.insert(
            5,
            IfaceDevice {
                phy_ownership: PhyOwnership { phy_id: 0, phy_assigned_id: 0 },
                sme_server: SmeServer::Client(sender),
                stats_sched,
                mlme_query: MlmeQueryProxy::new(mlme_proxy),
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
