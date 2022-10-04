// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::hash_map::HashMap;

use derivative::Derivative;
use ethernet as eth;
use fidl_fuchsia_hardware_ethernet::Features;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use futures::stream::StreamExt as _;
use net_types::{ethernet::Mac, ip::IpAddr, SpecifiedAddr, UnicastAddr};
use netstack3_core::{
    data_structures::id_map_collection::{Entry, IdMapCollection, IdMapCollectionKey},
    device::{handle_queued_rx_packets, DeviceId},
    Ctx,
};

use crate::bindings::{interfaces_admin, util::NeedsDataNotifier, LockableContext};

pub const LOOPBACK_MAC: Mac = Mac::new([0, 0, 0, 0, 0, 0]);

pub type BindingId = u64;

/// Keeps tabs on devices.
///
/// `Devices` keeps a list of devices that are installed in the netstack with
/// an associated netstack core ID `C` used to reference the device.
///
/// The type parameters `C` and `I` are for the core ID type and the extra
/// information associated with the device, respectively, and default to the
/// types used by `EventLoop` for brevity in the main use case. The type
/// parameters are there to allow testing without dependencies on `core`.
pub struct Devices<C: IdMapCollectionKey = DeviceId, I = DeviceSpecificInfo> {
    devices: IdMapCollection<C, DeviceInfo<C, I>>,
    // invariant: all values in id_map are valid keys in devices.
    id_map: HashMap<BindingId, C>,
    last_id: BindingId,
}

impl<C: IdMapCollectionKey, I> Default for Devices<C, I> {
    fn default() -> Self {
        Self { devices: IdMapCollection::new(), id_map: HashMap::new(), last_id: 0 }
    }
}

impl<C, I> Devices<C, I>
where
    C: IdMapCollectionKey + Clone + std::fmt::Debug,
{
    /// Allocates a new [`BindingId`].
    fn alloc_id(last_id: &mut BindingId) -> BindingId {
        *last_id += 1;
        *last_id
    }

    /// Adds a new device.
    ///
    /// Adds a new device if the informed `core_id` is valid (i.e., not
    /// currently tracked by [`Devices`]). A new [`BindingId`] will be allocated
    /// and a [`DeviceInfo`] struct will be created with the provided `info` and
    /// IDs.
    pub fn add_device<F: FnOnce(BindingId) -> I>(
        &mut self,
        core_id: C,
        info: F,
    ) -> Option<BindingId> {
        let Self { devices, id_map, last_id } = self;
        match devices.entry(core_id) {
            Entry::Occupied(_) => None,
            Entry::Vacant(entry) => {
                let id = Self::alloc_id(last_id);
                let core_id = entry.key().clone();
                assert_matches::assert_matches!(id_map.insert(id, core_id.clone()), None);
                let _: &mut DeviceInfo<_, _> =
                    entry.insert(DeviceInfo { id, core_id: core_id, info: info(id) });
                Some(id)
            }
        }
    }

    /// Removes a device from the internal list.
    ///
    /// Removes a device from the internal [`Devices`] list and returns the
    /// associated [`DeviceInfo`] if `id` is found or `None` otherwise.
    pub fn remove_device(&mut self, id: BindingId) -> Option<DeviceInfo<C, I>> {
        let Self { devices, id_map, last_id: _ } = self;
        id_map.remove(&id).and_then(|core_id| devices.remove(&core_id))
    }

    /// Gets an iterator over all tracked devices.
    #[cfg(test)]
    pub fn iter_devices(&self) -> impl Iterator<Item = &DeviceInfo<C, I>> {
        self.devices.iter()
    }

    /// Retrieve device with [`BindingId`].
    pub fn get_device(&self, id: BindingId) -> Option<&DeviceInfo<C, I>> {
        let Self { devices, id_map, last_id: _ } = self;
        id_map.get(&id).and_then(|device_id| devices.get(&device_id))
    }

    /// Retrieve mutable reference to device with [`BindingId`].
    pub fn get_device_mut(&mut self, id: BindingId) -> Option<&mut DeviceInfo<C, I>> {
        let Self { devices, id_map, last_id: _ } = self;
        id_map.get(&id).and_then(move |core_id| devices.get_mut(&core_id))
    }

    /// Retrieve associated `core_id` for [`BindingId`].
    pub fn get_core_id(&self, id: BindingId) -> Option<C> {
        self.id_map.get(&id).cloned()
    }

    /// Retrieve non-mutable reference to device by associated [`CoreId`] `id`.
    pub fn get_core_device(&self, id: C) -> Option<&DeviceInfo<C, I>> {
        self.devices.get(&id)
    }

    /// Retrieve mutable reference to device by associated [`CoreId`] `id`.
    pub fn get_core_device_mut(&mut self, id: C) -> Option<&mut DeviceInfo<C, I>> {
        self.devices.get_mut(&id)
    }

    /// Retrieve associated `binding_id` for `core_id`.
    pub fn get_binding_id(&self, core_id: C) -> Option<BindingId> {
        self.devices.get(&core_id).map(|d| d.id)
    }
}

impl<C: IdMapCollectionKey> Devices<C, DeviceSpecificInfo> {
    /// Retrieves the device with the given name.
    pub fn get_device_by_name(&self, name: &str) -> Option<&DeviceInfo<C, DeviceSpecificInfo>> {
        self.devices.iter().find(|d| d.info.common_info().name == name)
    }
}

/// Device specific iformation.
#[derive(Debug)]
pub enum DeviceSpecificInfo {
    Ethernet(EthernetInfo),
    Netdevice(NetdeviceInfo),
    Loopback(LoopbackInfo),
}

impl DeviceSpecificInfo {
    pub fn common_info(&self) -> &CommonInfo {
        match self {
            Self::Ethernet(i) => &i.common_info,
            Self::Netdevice(i) => &i.common_info,
            Self::Loopback(i) => &i.common_info,
        }
    }

    pub fn common_info_mut(&mut self) -> &mut CommonInfo {
        match self {
            Self::Ethernet(i) => &mut i.common_info,
            Self::Netdevice(i) => &mut i.common_info,
            Self::Loopback(i) => &mut i.common_info,
        }
    }
}

pub(crate) fn spawn_rx_task<C: LockableContext + Send + Sync + 'static>(
    notifier: &NeedsDataNotifier,
    ns: C,
    device_id: DeviceId,
) {
    let mut watcher = notifier.watcher();

    fuchsia_async::Task::spawn(async move {
        // Loop while we are woken up to handle enqueued RX packets.
        while let Some(()) = watcher.next().await {
            let mut ctx = ns.lock().await;
            let Ctx { sync_ctx, non_sync_ctx } = &mut *ctx;
            handle_queued_rx_packets(sync_ctx, non_sync_ctx, &device_id)
        }
    })
    .detach()
}

/// Information common to all devices.
#[derive(Derivative)]
#[derivative(Debug)]
pub struct CommonInfo {
    pub mtu: u32,
    pub admin_enabled: bool,
    pub events: super::InterfaceEventProducer,
    pub name: String,
    // An attach point to send `fuchsia.net.interfaces.admin/Control` handles to the Interfaces
    // Admin worker.
    #[derivative(Debug = "ignore")]
    pub control_hook: futures::channel::mpsc::Sender<interfaces_admin::OwnedControlHandle>,
    pub(crate) addresses: HashMap<SpecifiedAddr<IpAddr>, AddressInfo>,
}

#[derive(Debug)]
pub(crate) struct AddressInfo {
    // The `AddressStateProvider` FIDL protocol worker.
    pub(crate) address_state_provider: FidlWorkerInfo<fnet_interfaces_admin::AddressRemovalReason>,
    // Sender for [`AddressAssignmentState`] change events published by Core;
    // the receiver is held by the `AddressStateProvider` worker. Note that an
    // [`UnboundedSender`] is used because it exposes a synchronous send API
    // which is required since Core is no-async.
    pub(crate) assignment_state_sender:
        futures::channel::mpsc::UnboundedSender<fnet_interfaces_admin::AddressAssignmentState>,
}

/// Loopback device information.
#[derive(Derivative)]
#[derivative(Debug)]
pub struct LoopbackInfo {
    pub common_info: CommonInfo,
    #[derivative(Debug = "ignore")]
    pub rx_notifier: NeedsDataNotifier,
}

/// Ethernet device information.
#[derive(Debug)]
pub struct EthernetInfo {
    pub common_info: CommonInfo,
    pub client: eth::Client,
    pub mac: UnicastAddr<Mac>,
    pub features: Features,
    pub phy_up: bool,
    pub(crate) interface_control: FidlWorkerInfo<fnet_interfaces_admin::InterfaceRemovedReason>,
}

/// Information associated with FIDL Protocol workers.
#[derive(Debug)]
pub(crate) struct FidlWorkerInfo<R> {
    // The worker `Task`, wrapped in a `Shared` future so that it can be awaited
    // multiple times.
    pub worker: futures::future::Shared<fuchsia_async::Task<()>>,
    // Mechanism to cancel the worker with reason `R`. If `Some`, the worker is
    // active (and holds the `Receiver`). Otherwise, the worker has been
    // canceled.
    pub cancelation_sender: Option<futures::channel::oneshot::Sender<R>>,
}

impl From<EthernetInfo> for DeviceSpecificInfo {
    fn from(i: EthernetInfo) -> Self {
        Self::Ethernet(i)
    }
}

/// Network device information.
#[derive(Debug)]
pub struct NetdeviceInfo {
    pub common_info: CommonInfo,
    pub handler: super::netdevice_worker::PortHandler,
    pub mac: UnicastAddr<Mac>,
    pub phy_up: bool,
}

impl From<NetdeviceInfo> for DeviceSpecificInfo {
    fn from(i: NetdeviceInfo) -> Self {
        Self::Netdevice(i)
    }
}

/// Device information kept by [`Devices`].
#[derive(Debug, PartialEq)]
pub struct DeviceInfo<C = DeviceId, I = DeviceSpecificInfo> {
    id: BindingId,
    core_id: C,
    info: I,
}

impl<C, I> DeviceInfo<C, I>
where
    C: Clone,
{
    pub fn core_id(&self) -> &C {
        &self.core_id
    }

    pub fn id(&self) -> BindingId {
        self.id.clone()
    }

    pub fn info(&self) -> &I {
        &self.info
    }

    pub fn into_info(self) -> I {
        self.info
    }

    pub fn info_mut(&mut self) -> &mut I {
        &mut self.info
    }
}

#[cfg(test)]
mod tests {
    use assert_matches::assert_matches;
    use std::collections::HashSet;

    use super::*;

    type TestDevices = Devices<MockDeviceId, u64>;

    #[derive(Copy, Clone, Eq, PartialEq, Debug)]
    struct MockDeviceId(usize);

    impl IdMapCollectionKey for MockDeviceId {
        const VARIANT_COUNT: usize = 1;

        fn get_variant(&self) -> usize {
            0
        }

        fn get_id(&self) -> usize {
            self.0 as usize
        }
    }

    #[test]
    fn test_add_remove_device() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let core_b = MockDeviceId(2);
        let a = d.add_device(core_a, |id| id + 10).expect("can add device");
        let b = d.add_device(core_b, |id| id + 20).expect("can add device");
        assert_ne!(a, b, "allocated same id");
        assert_eq!(d.add_device(core_a, |id| id + 10), None, "can't add same id again");
        // check that ids are incrementing
        assert_eq!(d.last_id, 2);

        // check that devices are correctly inserted and carry the core id.
        assert_eq!(d.get_device(a).unwrap().core_id, core_a);
        assert_eq!(d.get_device(b).unwrap().core_id, core_b);
        assert_eq!(d.get_core_id(a).unwrap(), core_a);
        assert_eq!(d.get_core_id(b).unwrap(), core_b);
        assert_eq!(d.get_binding_id(core_a).unwrap(), a);
        assert_eq!(d.get_binding_id(core_b).unwrap(), b);

        // check that we can retrieve both devices by the core id:
        assert_matches!(d.get_core_device_mut(core_a), Some(_));
        assert_matches!(d.get_core_device_mut(core_b), Some(_));

        // remove both devices
        let info_a = d.remove_device(a).expect("can remove device");
        let info_b = d.remove_device(b).expect("can remove device");
        assert_eq!(info_a.info, a + 10);
        assert_eq!(info_b.info, b + 20);
        assert_eq!(info_a.core_id, core_a);
        assert_eq!(info_b.core_id, core_b);
        // removing device again will fail
        assert_eq!(d.remove_device(a), None);

        // retrieving the devices now should fail:
        assert_eq!(d.get_device(a), None);
        assert_eq!(d.get_core_id(a), None);
        assert_eq!(d.get_core_device_mut(core_a), None);

        assert!(d.devices.is_empty());
        assert!(d.id_map.is_empty());
    }

    #[test]
    fn test_iter() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let a = d.add_device(core_a, |id| id + 10).unwrap();
        assert_eq!(d.iter_devices().map(|d| d.id).collect::<HashSet<_>>(), HashSet::from([a]));

        let core_b = MockDeviceId(2);
        let b = d.add_device(core_b, |id| id + 20).unwrap();
        assert_eq!(d.iter_devices().map(|d| d.id).collect::<HashSet<_>>(), HashSet::from([a, b]));
    }
}
