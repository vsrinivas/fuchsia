// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_wlan_device as fidl_wlan_dev, fidl_fuchsia_wlan_sme as fidl_wlan_sme,
    fuchsia_inspect_contrib::inspect_log,
    futures::{
        future::FutureExt,
        select,
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
    },
    log::{error, info},
    pin_utils::pin_mut,
    std::{convert::Infallible, path::Path, sync::Arc},
};

use crate::{device_watch, inspect, watchable_map::WatchableMap};

/// Iface's PHY information.
#[derive(Debug, PartialEq, Clone)]
pub struct PhyOwnership {
    // Iface's global PHY ID.
    pub phy_id: u16,
    // Local ID assigned by this iface's PHY.
    pub phy_assigned_id: u16,
}

#[derive(Debug, Clone)]
pub struct NewIface {
    // Global, unique iface ID.
    pub id: u16,
    // Information about this iface's PHY.
    pub phy_ownership: PhyOwnership,
    // The handle for connecting channels to this iface's SME.
    pub generic_sme: fidl_wlan_sme::GenericSmeProxy,
}

pub struct PhyDevice {
    pub proxy: fidl_wlan_dev::PhyProxy,
    pub device: wlan_dev::Device,
}

pub struct IfaceDevice {
    pub phy_ownership: PhyOwnership,
    pub generic_sme: fidl_wlan_sme::GenericSmeProxy,
}

pub type PhyMap = WatchableMap<u16, PhyDevice>;
pub type IfaceMap = WatchableMap<u16, IfaceDevice>;

/// Handles newly-discovered PHYs.
///
/// `serve_phys` watches for new PHY devices in the appropriate `DeviceEnv` (ie: real or
/// `IsolatedDevMgr`).
///
/// When new PHYs are discovered, the `device_watch` module produces a `NewPhyDevice`.  This struct
/// contains a PHY proxy and a `wlan_dev::Device`.  This `Device` lifetime is then managed by
/// `serve_phy`.
pub async fn serve_phys<P: AsRef<Path>, E: wlan_dev::DeviceEnv>(
    phys: Arc<PhyMap>,
    inspect_tree: Arc<inspect::WlanMonitorTree>,
    device_path: P,
) -> Result<Infallible, Error> {
    let new_phys = device_watch::watch_phy_devices::<_, E>(device_path)?;
    pin_mut!(new_phys);
    let mut active_phys = FuturesUnordered::new();
    loop {
        select! {
            // OK to fuse directly in the `select!` since we bail immediately
            // when a `None` is encountered.
            new_phy = new_phys.next().fuse() => match new_phy {
                None => return Err(format_err!("new phy stream unexpectedly finished")),
                Some(Err(e)) => return Err(format_err!("new phy stream returned an error: {}", e)),
                Some(Ok(new_phy)) => {
                    let fut = serve_phy(&phys, new_phy, inspect_tree.clone());
                    active_phys.push(fut);
                }
            },
            () = active_phys.select_next_some() => {},
        }
    }
}

/// Handles the lifetime of discovered PHY devices.
///
/// `serve_phy` takes newly discovered PHY devices and inserts them into a `WatchableMap`.
///
/// `serve_phy` then waits for the PHY device to be removed from the system.  When the PHY is
/// removed, `serve_phy` removes the device from the `WatchableMap`.
///
/// The `WatchableMap` produces events when elements are added to or removed from it.  These events
/// are consumed by another future that manages the `DeviceWatcher` protocol and notifies API
/// clients of PHY addition or removal.
async fn serve_phy(
    phys: &PhyMap,
    new_phy: device_watch::NewPhyDevice,
    inspect_tree: Arc<inspect::WlanMonitorTree>,
) {
    let msg = format!("new phy #{}: {}", new_phy.id, new_phy.device.path().to_string_lossy());
    info!("{}", msg);
    inspect_log!(inspect_tree.device_events.lock(), msg: msg);
    let id = new_phy.id;

    // Take the event stream from the PHY proxy so that it can be monitored.  An event produced by
    // this stream indicates that the PHY has been removed from the system.
    let event_stream = new_phy.proxy.take_event_stream();

    // Insert the newly discovered device into the `WatchableMap`.  This will trigger the watchable
    // map to produce an event so that the `DeviceWatcher` service can produce an update for API
    // consumers.
    phys.insert(id, PhyDevice { proxy: new_phy.proxy, device: new_phy.device });

    // The event stream's production of an event indicates that the PHY has been removed from the
    // system.  Remove the PHY from the `WatchableMap`.  This will result in the `WatchableMap`
    // producing a removal event which will trigger the `DeviceWatcher` service to send a
    // notification to API consumers.
    let r = event_stream.map_ok(|_| ()).try_collect::<()>().await;
    phys.remove(&id);
    if let Err(e) = r {
        let msg = format!("error reading from FIDL channel of phy #{}: {}", id, e);
        error!("{}", msg);
        inspect_log!(inspect_tree.device_events.lock(), msg: msg);
    }
    info!("phy removed: #{}", id);
    inspect_log!(inspect_tree.device_events.lock(), msg: format!("phy removed: #{}", id));
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{inspect, watchable_map},
        fidl::endpoints::create_proxy,
        fuchsia_async as fasync,
        fuchsia_inspect::Inspector,
        fuchsia_zircon::{self as zx, prelude::*},
        futures::task::Poll,
        std::{fs::File, path::Path},
        tempfile,
        wlan_common::{assert_variant, test_utils::ExpectWithin},
    };

    // This struct is an implementation of the DeviceEnv trait that returns errors in all cases to
    // enable testing the case where watching for devices fails.
    struct FaultyDeviceEnv;

    impl wlan_dev::DeviceEnv for FaultyDeviceEnv {
        fn device_from_path<P: AsRef<Path>>(_path: P) -> Result<wlan_dev::Device, zx::Status> {
            Err(zx::Status::NOT_SUPPORTED)
        }
    }

    #[test]
    fn test_serve_phys_exits_when_watching_devices_fails() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (phys, _phy_events) = PhyMap::new();
        let phys = Arc::new(phys);
        let inspector = Inspector::new_with_size(inspect::VMO_SIZE_BYTES);
        let inspect_tree = Arc::new(inspect::WlanMonitorTree::new(inspector));

        // Serve PHYs from the bogus device environment that returns errors for all operations.
        // This will ensure that attempting to watch devices fails immediately.
        let fut = serve_phys::<_, FaultyDeviceEnv>(phys.clone(), inspect_tree, "/bogus/path");
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    #[test]
    fn test_serve_phy_adds_and_removes_phy() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (phys, mut phy_events) = PhyMap::new();
        let phys = Arc::new(phys);
        let inspector = Inspector::new_with_size(inspect::VMO_SIZE_BYTES);
        let inspect_tree = Arc::new(inspect::WlanMonitorTree::new(inspector));

        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let test_path = temp_dir.path().join("test_device");
        let file = File::create(test_path.clone()).expect("failed to open file");
        let device = wlan_dev::Device { node: file, path: test_path };
        let (phy_proxy, phy_server) =
            create_proxy::<fidl_wlan_dev::PhyMarker>().expect("failed to create PHY proxy");

        let new_phy = device_watch::NewPhyDevice { id: 0, proxy: phy_proxy, device };

        let fut = serve_phy(&phys, new_phy, inspect_tree);
        pin_mut!(fut);

        // Run the PHY service to pick up the new PHY.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        match exec.run_until_stalled(
            &mut phy_events.next().expect_within(5.seconds(), "phy_watcher did not respond"),
        ) {
            Poll::Ready(Some(event)) => match event {
                watchable_map::MapEvent::KeyInserted(key) => {
                    assert_eq!(key, 0)
                }
                _ => panic!("unexpected watcher event"),
            },
            Poll::Ready(None) => panic!("watcher events ended unexpectedly"),
            Poll::Pending => panic!("no pending watcher events"),
        }
        assert!(phys.get(&0).is_some());

        // Now drop the other end of the PHY and observe that the PHY is removed from the map.
        drop(phy_server);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        match exec.run_until_stalled(
            &mut phy_events.next().expect_within(5.seconds(), "phy_watcher did not respond"),
        ) {
            Poll::Ready(Some(event)) => match event {
                watchable_map::MapEvent::KeyRemoved(key) => {
                    assert_eq!(key, 0)
                }
                _ => panic!("unexpected watcher event"),
            },
            Poll::Ready(None) => panic!("watcher events ended unexpectedly"),
            Poll::Pending => panic!("no pending watcher events"),
        }
        assert!(phys.get(&0).is_none());
    }
}
