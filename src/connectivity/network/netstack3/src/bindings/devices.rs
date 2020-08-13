// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use ethernet as eth;
use fidl_fuchsia_hardware_ethernet::Features;
use fidl_fuchsia_hardware_ethernet::MacAddress;
use netstack3_core::{DeviceId, IdMapCollection, IdMapCollectionKey};

pub type BindingId = u64;

/// Keeps tabs on devices.
///
/// `Devices` keeps a list of devices that can be either *active* or *inactive*.
/// An *active* device has an associated [`CoreId`] and an *inactive* one
/// doesn't.
///
/// The type parameters `C` and `I` are for the core ID type and the extra
/// information associated with the device, respectively, and default to the
/// types used by `EventLoop` for brevity in the main use case. The type
/// parameters are there to allow testing without dependencies on `core`.
// NOTE: Devices uses separate hash maps internally for active and inactive
//  devices to guarantee that the fast path - sending and receiving frames - can
//  be each be achieved with a single hash map lookup to get the necessary
//  information. When sending frames, we lookup with CoreId on active_devices
//  to retrieve the driver client, and when receiving frames we just get the
//  CoreId from id_map.
//  For users of this mod, this split in hash maps should be completely opaque.
//  The main use cases are operated as follows:
//  - Core asks us to send a frame on a device, indexed by the core ID:
//    => A single lookup in active_devices will retrieve the device info, which
//       contains the device client.
//  - We receive a frame from a device, indexed by the binding ID:
//    => A single lookup in id_map will retrieve the core ID, which is enough
//       information to send the frame into core.
//  - Core asks us to send a message about a device to an application client:
//    => A single lookup in active_devices will retrieve the device info, which
//       contains the binding ID and all other information that an application
//       client may need.
//  - An application client wants to operate on an active device (e.g. add an IP
//    address):
//    => Two lookups are necessary, one in id_map to retrieve the core ID
//       followed by one in active_devices.
//  - An application client asks us to bring a device up or down, or a device
//    goes online or offline (in both cases addressed by binding ID):
//    => For the down case, entries are removed from both active_devices and
//       id_map and the device info is moved into inactive_devices.
//    => For the up case, the inactive_devices entry is removed and one entry is
//       created in each of active_devices and id_map.
pub struct Devices<C: IdMapCollectionKey = DeviceId, I = CommonInfo> {
    active_devices: IdMapCollection<C, DeviceInfo<C, I>>,
    // invariant: all values in id_map are valid keys in active_devices.
    id_map: HashMap<BindingId, C>,
    inactive_devices: HashMap<BindingId, DeviceInfo<C, I>>,
    last_id: BindingId,
}

impl<C: IdMapCollectionKey, I> Default for Devices<C, I> {
    fn default() -> Self {
        Self {
            active_devices: IdMapCollection::new(),
            id_map: HashMap::new(),
            inactive_devices: HashMap::new(),
            last_id: 0,
        }
    }
}

/// Errors that may be returned by switching a device state.
///
/// See [`Devices::activate_device`] and [`Devices::deactivate_device`].
#[derive(Debug, Eq, PartialEq)]
pub enum ToggleError {
    /// No change to device's active or inactive state.
    NoChange,
    /// Informed device identifier not found.
    NotFound,
}

impl<C, I> Devices<C, I>
where
    C: IdMapCollectionKey + Clone,
{
    /// Allocates a new [`BindingId`].
    fn alloc_id(&mut self) -> BindingId {
        self.last_id = self.last_id + 1;
        self.last_id
    }

    /// Adds a new active device.
    ///
    /// Adds a new active device if the informed `core_id` is valid (i.e., not
    /// currently tracked by [`Devices`]). A new [`BindingId`] will be allocated
    /// and a [`DeviceInfo`] struct will be created with the provided `info` and
    /// IDs.
    pub fn add_active_device(&mut self, core_id: C, info: I) -> Option<BindingId> {
        if self.active_devices.get(&core_id).is_some() {
            return None;
        }
        let id = self.alloc_id();

        self.active_devices
            .insert(&core_id, DeviceInfo { id, core_id: Some(core_id.clone()), info });
        self.id_map.insert(id, core_id);

        Some(id)
    }

    /// Adds a new device in the inactive state.
    ///
    /// Adds a new device with `info`. A new [`BindingId`] will be allocated
    /// and a [`DeviceInfo`] struct will be created with the provided `info` and
    /// the generated [`BindingId`].
    ///
    /// The new device will *not* have a `core_id` allocated, that can be done
    /// by calling [`Devices::activate_device`] with the newly created
    /// [`BindingId`].
    pub fn add_device(&mut self, info: I) -> BindingId {
        let id = self.alloc_id();
        self.inactive_devices.insert(id, DeviceInfo { id, core_id: None, info });
        id
    }

    /// Activates a device with `id`, using the closure to associate a `core_id`
    /// with it.
    ///
    /// Activates a device with `id` if all the conditions are true:
    /// - `id` exists.
    /// - `id` is not already attached to a `core_id`.
    ///
    /// On success, returns a ref to the updated [`DeviceInfo`] containing the
    /// provided `core_id`.
    ///
    /// # Panics
    ///
    /// Panics if the returned core ID `C` exists and is tracked by this
    /// `Devices` collection.
    pub fn activate_device<F: FnOnce(&DeviceInfo<C, I>) -> C>(
        &mut self,
        id: BindingId,
        generate_core_id: F,
    ) -> Result<&DeviceInfo<C, I>, ToggleError> {
        if self.id_map.contains_key(&id) {
            return Err(ToggleError::NoChange);
        }

        match self.inactive_devices.remove(&id) {
            None => Err(ToggleError::NotFound),
            Some(mut info) => {
                let core_id = generate_core_id(&info);
                assert!(self.active_devices.get(&core_id).is_none());

                assert!(info.core_id.is_none());
                info.core_id = Some(core_id.clone());

                self.id_map.insert(id, core_id.clone());
                self.active_devices.insert(&core_id, info);
                // we can unwrap here because we just inserted the device
                // above.
                Ok(self.active_devices.get(&core_id).unwrap())
            }
        }
    }

    /// Deactivates a device with `id`, disassociating its `core_id`.
    ///
    /// Deactivates a device with `id` if all the conditions are true:
    /// - `id` exists.
    /// - `id` has an associated `core_id`.
    ///
    /// On success, returnes a ref to the updated [`DeviceInfo`] and the
    /// previously associated `core_id`.
    pub fn deactivate_device(
        &mut self,
        id: BindingId,
    ) -> Result<(C, &mut DeviceInfo<C, I>), ToggleError> {
        if self.inactive_devices.contains_key(&id) {
            return Err(ToggleError::NoChange);
        }

        match self.id_map.remove(&id) {
            None => Err(ToggleError::NotFound),
            Some(core_id) => {
                // we can unwrap here because of the invariant between
                // id_map and active_devices.
                let mut dev_id = self.active_devices.remove(&core_id).unwrap();
                dev_id.core_id = None;

                Ok((core_id, self.inactive_devices.entry(id).or_insert(dev_id)))
            }
        }
    }

    /// Removes a device from the internal list.
    ///
    /// Removes a device from the internal [`Devices`] list and returns the
    /// associated [`DeviceInfo`] if `id` is found or `None` otherwise.
    pub fn remove_device(&mut self, id: BindingId) -> Option<DeviceInfo<C, I>> {
        match self.id_map.remove(&id) {
            Some(core) => self.active_devices.remove(&core),
            None => self.inactive_devices.remove(&id),
        }
    }

    /// Gets an iterator over all tracked devices.
    pub fn iter_devices(&self) -> impl Iterator<Item = &DeviceInfo<C, I>> {
        self.active_devices.iter().chain(self.inactive_devices.values())
    }

    /// Retrieve device with [`BindingId`].
    pub fn get_device(&self, id: BindingId) -> Option<&DeviceInfo<C, I>> {
        self.id_map
            .get(&id)
            .and_then(|device_id| self.active_devices.get(&device_id))
            .or_else(|| self.inactive_devices.get(&id))
    }

    /// Retrieve mutable reference to device with [`BindingId`].
    pub fn get_device_mut(&mut self, id: BindingId) -> Option<&mut DeviceInfo<C, I>> {
        if let Some(device_id) = self.id_map.get(&id) {
            self.active_devices.get_mut(&device_id)
        } else {
            self.inactive_devices.get_mut(&id)
        }
    }

    /// Retrieve associated `core_id` for [`BindingId`].
    pub fn get_core_id(&self, id: BindingId) -> Option<C> {
        self.id_map.get(&id).cloned()
    }

    /// Retrieve mutable reference to device by associated [`CoreId`] `id`.
    pub fn get_core_device_mut(&mut self, id: C) -> Option<&mut DeviceInfo<C, I>> {
        self.active_devices.get_mut(&id)
    }

    /// Retrieve associated `binding_id` for `core_id`.
    pub fn get_binding_id(&self, core_id: C) -> Option<BindingId> {
        self.active_devices.get(&core_id).map(|d| d.id)
    }
}

/// Device information kept in [`DeviceInfo`].
pub struct CommonInfo {
    path: String,
    client: eth::Client,
    mac: MacAddress,
    mtu: u32,
    features: Features,
    admin_enabled: bool,
    phy_up: bool,
}

impl CommonInfo {
    pub fn new(
        path: String,
        client: eth::Client,
        mac: MacAddress,
        mtu: u32,
        features: Features,
        admin_enabled: bool,
        phy_up: bool,
    ) -> Self {
        Self { path, client, mac, mtu, features, admin_enabled, phy_up }
    }
}

/// Device information kept by [`Devices`].
#[derive(Debug)]
pub struct DeviceInfo<C = DeviceId, I = CommonInfo> {
    id: BindingId,
    core_id: Option<C>,
    info: I,
}

impl<C, I> DeviceInfo<C, I>
where
    C: Clone,
{
    pub fn core_id(&self) -> Option<C> {
        self.core_id.clone()
    }

    pub fn id(&self) -> BindingId {
        self.id
    }

    #[cfg(test)]
    pub fn is_active(&self) -> bool {
        self.core_id.is_some()
    }
}

impl<C> DeviceInfo<C, CommonInfo> {
    pub fn path(&self) -> &String {
        &self.info.path
    }

    pub fn client(&self) -> &eth::Client {
        &self.info.client
    }

    pub fn client_mut(&mut self) -> &mut eth::Client {
        &mut self.info.client
    }

    pub fn mac(&self) -> MacAddress {
        self.info.mac
    }

    pub fn mtu(&self) -> u32 {
        self.info.mtu
    }

    pub fn features(&self) -> Features {
        self.info.features
    }

    pub fn admin_enabled(&self) -> bool {
        self.info.admin_enabled
    }

    pub fn set_admin_enabled(&mut self, setting: bool) {
        self.info.admin_enabled = setting;
    }

    pub fn phy_up(&self) -> bool {
        self.info.phy_up
    }

    pub fn set_phy_up(&mut self, setting: bool) {
        self.info.phy_up = setting;
    }
}

#[cfg(test)]
mod tests {
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
    fn test_add_remove_active_device() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let core_b = MockDeviceId(2);
        let a = d.add_active_device(core_a, 10).expect("can add device");
        let b = d.add_active_device(core_b, 20).expect("can add device");
        assert_ne!(a, b, "allocated same id");
        assert!(d.add_active_device(core_a, 10).is_none(), "can't add same id again");
        // check that ids are incrementing
        assert_eq!(d.last_id, 2);

        // check that devices are correctly inserted and carry the core id.
        assert_eq!(d.get_device(a).unwrap().core_id.unwrap(), core_a);
        assert_eq!(d.get_device(b).unwrap().core_id.unwrap(), core_b);
        assert_eq!(d.get_core_id(a).unwrap(), core_a);
        assert_eq!(d.get_core_id(b).unwrap(), core_b);
        assert_eq!(d.get_binding_id(core_a).unwrap(), a);
        assert_eq!(d.get_binding_id(core_b).unwrap(), b);

        // check that we can retrieve both devices by the core id:
        assert!(d.get_core_device_mut(core_a).is_some());
        assert!(d.get_core_device_mut(core_b).is_some());

        // remove both devices
        let info_a = d.remove_device(a).expect("can remove device");
        let info_b = d.remove_device(b).expect("can remove device");
        assert_eq!(info_a.info, 10);
        assert_eq!(info_b.info, 20);
        assert_eq!(info_a.core_id.unwrap(), core_a);
        assert_eq!(info_b.core_id.unwrap(), core_b);
        // removing device again will fail
        assert!(d.remove_device(a).is_none());

        // retrieving the devices now should fail:
        assert!(d.get_device(a).is_none());
        assert!(d.get_core_id(a).is_none());
        assert!(d.get_core_device_mut(core_a).is_none());

        assert!(d.active_devices.is_empty());
        assert!(d.inactive_devices.is_empty());
        assert!(d.id_map.is_empty());
    }

    #[test]
    fn test_add_remove_inactive_device() {
        let mut d = TestDevices::default();
        let a = d.add_device(10);
        let b = d.add_device(20);
        assert_ne!(a, b, "allocated same id");

        // check that ids are incrementing
        assert_eq!(d.last_id, 2);

        // check that devices are correctly inserted and don't
        // carry a core id.
        assert!(d.get_device(a).unwrap().core_id.is_none());
        assert!(d.get_device(b).unwrap().core_id.is_none());
        assert!(d.get_core_id(a).is_none());
        assert!(d.get_core_id(b).is_none());

        // remove both devices
        let info_a = d.remove_device(a).expect("can remove device");
        let info_b = d.remove_device(b).expect("can remove device");
        assert_eq!(info_a.info, 10);
        assert_eq!(info_b.info, 20);
        assert!(info_a.core_id.is_none());
        assert!(info_b.core_id.is_none());

        // removing device again will fail
        assert!(d.remove_device(a).is_none());

        // retrieving the device now should fail:
        assert!(d.get_device(a).is_none());
        assert!(d.get_core_id(a).is_none());

        assert!(d.active_devices.is_empty());
        assert!(d.inactive_devices.is_empty());
        assert!(d.id_map.is_empty());
    }

    #[test]
    fn test_activate_device() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let core_b = MockDeviceId(2);
        let a = d.add_device(10);
        let b = d.add_active_device(core_b, 20).unwrap();
        assert_eq!(d.activate_device(1000, |_| core_a).unwrap_err(), ToggleError::NotFound);
        assert_eq!(d.activate_device(b, |_| core_b).unwrap_err(), ToggleError::NoChange);

        let info = d.activate_device(a, |_| core_a).expect("can activate device");
        assert_eq!(info.info, 10);
        assert_eq!(info.core_id.unwrap(), core_a);

        // both a and b should be active now:
        assert!(d.inactive_devices.is_empty());
    }

    #[test]
    #[should_panic]
    fn test_activate_duplicate_core_id() {
        let mut devices = TestDevices::default();
        let core_id = MockDeviceId(1);
        // Add an active device with core_id
        devices.add_active_device(core_id, 20).unwrap();

        // Trying to activate another device with the same core_id should panic
        let second_device = devices.add_device(10);
        let _result = devices.activate_device(second_device, |_| core_id);
    }

    #[test]
    fn test_deactivate_device() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let a = d.add_active_device(core_a, 10).unwrap();
        let b = d.add_device(20);
        assert_eq!(d.deactivate_device(b).unwrap_err(), ToggleError::NoChange);
        assert_eq!(d.deactivate_device(1000).unwrap_err(), ToggleError::NotFound);

        let (core, info) = d.deactivate_device(a).unwrap();
        assert_eq!(core, core_a);
        assert_eq!(info.info, 10);
        assert!(info.core_id.is_none());

        // both a and b should be inactive now:
        assert!(d.active_devices.is_empty());
        assert!(d.id_map.is_empty());
    }

    #[test]
    fn test_iter() {
        let mut d = TestDevices::default();
        let core_a = MockDeviceId(1);
        let a = d.add_active_device(core_a, 10).unwrap();
        let b = d.add_device(20);

        // check that we can iterate over active and inactive devices seamlessly
        assert_eq!(d.iter_devices().count(), 2);
        let mut touch_a = false;
        let mut touch_b = false;
        for dev in d.iter_devices() {
            if dev.id == a {
                assert!(!touch_a);
                touch_a = true;
            } else if dev.id == b {
                assert!(!touch_b);
                touch_b = true;
            } else {
                panic!("Unexpected id");
            }
        }
        assert!(touch_a && touch_b);
    }
}
