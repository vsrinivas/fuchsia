// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Iterator type for neighbor info
#[allow(missing_debug_implementations)]
pub struct NeighborInfoIterator<'a, T: ?Sized> {
    ot_instance: &'a T,
    ot_iter: otNeighborInfoIterator,
}

impl<'a, T: ?Sized + Thread> Iterator for NeighborInfoIterator<'a, T> {
    type Item = NeighborInfo;
    fn next(&mut self) -> Option<Self::Item> {
        self.ot_instance.iter_next_neighbor_info(&mut self.ot_iter)
    }
}

/// Methods from the [OpenThread "Thread General" Module][1].
///
/// [1]: https://openthread.io/reference/group/api-thread-general
pub trait Thread {
    /// Functional equivalent of
    /// [`otsys::otThreadBecomeLeader`](crate::otsys::otThreadBecomeLeader).
    fn become_leader(&self) -> Result;

    /// Functional equivalent of
    /// [`otsys::otThreadBecomeRouter`](crate::otsys::otThreadBecomeRouter).
    fn become_router(&self) -> Result;

    /// Functional equivalent of
    /// [`otsys::otThreadGetChildInfoById`](crate::otsys::otThreadGetChildInfoById).
    #[must_use]
    fn get_child_info_by_id(&self, child_id: u16) -> Result<otChildInfo>;

    /// Functional equivalent of
    /// [`otsys::otThreadGetNetworkKey`](crate::otsys::otThreadGetNetworkKey).
    #[must_use]
    fn get_network_key(&self) -> NetworkKey;

    /// Functional equivalent of
    /// [`otsys::otThreadSetNetworkKey`](crate::otsys::otThreadSetNetworkKey).
    fn set_network_key(&self, key: &NetworkKey) -> Result;

    /// Similar to [`get_network_name_as_slice`], but returns
    /// an [`ot::NetworkName`](crate::ot::NetworkName).
    #[must_use]
    fn get_network_name(&self) -> NetworkName {
        NetworkName::try_from_slice(self.get_network_name_as_slice()).unwrap()
    }

    /// Functional equivalent of
    /// [`otsys::otThreadGetNetworkName`](crate::otsys::otThreadGetNetworkName).
    #[must_use]
    fn get_network_name_as_slice(&self) -> &[u8];

    /// Functional equivalent of
    /// [`otsys::otThreadSetNetworkName`](crate::otsys::otThreadSetNetworkName).
    fn set_network_name(&self, name: &NetworkName) -> Result;

    /// Functional equivalent of
    /// [`otsys::otThreadIsSingleton`](crate::otsys::otThreadIsSingleton).
    #[must_use]
    fn is_singleton(&self) -> bool;

    /// Functional equivalent of
    /// [`otsys::otThreadGetExtendedPanId`](crate::otsys::otThreadGetExtendedPanId).
    #[must_use]
    fn get_extended_pan_id(&self) -> &ExtendedPanId;

    /// Functional equivalent of
    /// [`otsys::otThreadSetExtendedPanId`](crate::otsys::otThreadSetExtendedPanId).
    fn set_extended_pan_id(&self, xpanid: &ExtendedPanId) -> Result;

    /// Functional equivalent of [`otsys::otThreadSetEnabled`](crate::otsys::otThreadSetEnabled).
    fn thread_set_enabled(&self, enabled: bool) -> Result;

    /// Functional equivalent of
    /// [`otsys::otThreadGetDeviceRole`](crate::otsys::otThreadGetDeviceRole).
    #[must_use]
    fn get_device_role(&self) -> DeviceRole;

    /// Functional equivalent of
    /// [`otsys::otThreadGetPartitionId`](crate::otsys::otThreadGetPartitionId).
    fn get_partition_id(&self) -> u32;

    /// Functional equivalent of [`otsys::otThreadGetRloc16`](crate::otsys::otThreadGetRloc16).
    fn get_rloc16(&self) -> u16;

    /// Functional equivalent of [`otsys::otThreadGetLinkMode`](crate::otsys::otThreadGetLinkMode).
    fn get_link_mode(&self) -> ot::LinkModeConfig;

    /// Functional equivalent of [`otsys::otThreadSetLinkMode`](crate::otsys::otThreadSetLinkMode).
    fn set_link_mode(&self, link_mode_config: ot::LinkModeConfig) -> Result;

    /// Gets the full RLOC address.
    fn get_rloc(&self) -> std::net::Ipv6Addr;

    /// Functional equivalent of
    /// [`otsys::otThreadGetMeshLocalEid`](crate::otsys::otThreadGetMeshLocalEid).
    fn get_mesh_local_eid(&self) -> std::net::Ipv6Addr;

    /// Functional equivalent of
    /// [`otsys::otThreadGetLinkLocalIp6Address`](crate::otsys::otThreadGetLinkLocalIp6Address).
    fn get_link_local_addr(&self) -> std::net::Ipv6Addr;

    /// Functional equivalent of
    /// [`otsys::otThreadGetLinkLocalAllThreadNodesMulticastAddress`](crate::otsys::otThreadGetLinkLocalAllThreadNodesMulticastAddress).
    fn get_link_local_all_nodes_multicast_addr(&self) -> std::net::Ipv6Addr;

    /// Functional equivalent of
    /// [`otsys::otThreadGetMeshLocalPrefix`](crate::otsys::otThreadGetMeshLocalPrefix).
    fn get_mesh_local_prefix(&self) -> &MeshLocalPrefix;

    /// Functional equivalent of
    /// [`otsys::otThreadGetNextNeighborInfo`](crate::otsys::otThreadGetNextNeighborInfo).
    // TODO: Determine if the underlying implementation of
    //       this method has undefined behavior when network data
    //       is being mutated while iterating. If it is undefined,
    //       we may need to make it unsafe and provide a safe method
    //       that collects the results.
    fn iter_next_neighbor_info(&self, ot_iter: &mut otNeighborInfoIterator)
        -> Option<NeighborInfo>;

    /// Returns an iterator for iterating over external routes.
    fn iter_neighbor_info(&self) -> NeighborInfoIterator<'_, Self> {
        NeighborInfoIterator {
            ot_instance: self,
            ot_iter: OT_NEIGHBOR_INFO_ITERATOR_INIT.try_into().unwrap(),
        }
    }
}

impl<T: Thread + Boxable> Thread for ot::Box<T> {
    fn become_leader(&self) -> Result<()> {
        self.as_ref().become_leader()
    }
    fn become_router(&self) -> Result<()> {
        self.as_ref().become_router()
    }
    fn get_child_info_by_id(&self, child_id: u16) -> Result<otChildInfo> {
        self.as_ref().get_child_info_by_id(child_id)
    }
    fn get_network_key(&self) -> NetworkKey {
        self.as_ref().get_network_key()
    }

    fn set_network_key(&self, key: &NetworkKey) -> Result {
        self.as_ref().set_network_key(key)
    }
    fn get_network_name_as_slice(&self) -> &[u8] {
        self.as_ref().get_network_name_as_slice()
    }

    fn set_network_name(&self, name: &NetworkName) -> Result {
        self.as_ref().set_network_name(name)
    }

    fn is_singleton(&self) -> bool {
        self.as_ref().is_singleton()
    }

    fn get_extended_pan_id(&self) -> &ExtendedPanId {
        self.as_ref().get_extended_pan_id()
    }

    fn set_extended_pan_id(&self, xpanid: &ExtendedPanId) -> Result {
        self.as_ref().set_extended_pan_id(xpanid)
    }

    fn thread_set_enabled(&self, enabled: bool) -> Result {
        self.as_ref().thread_set_enabled(enabled)
    }

    fn get_device_role(&self) -> DeviceRole {
        self.as_ref().get_device_role()
    }

    fn get_partition_id(&self) -> u32 {
        self.as_ref().get_partition_id()
    }

    fn get_rloc16(&self) -> u16 {
        self.as_ref().get_rloc16()
    }

    fn get_link_mode(&self) -> ot::LinkModeConfig {
        self.as_ref().get_link_mode()
    }

    fn set_link_mode(&self, link_mode_config: ot::LinkModeConfig) -> Result {
        self.as_ref().set_link_mode(link_mode_config)
    }

    fn get_rloc(&self) -> std::net::Ipv6Addr {
        self.as_ref().get_rloc()
    }

    fn get_mesh_local_eid(&self) -> std::net::Ipv6Addr {
        self.as_ref().get_mesh_local_eid()
    }

    fn get_link_local_addr(&self) -> std::net::Ipv6Addr {
        self.as_ref().get_link_local_addr()
    }

    fn get_link_local_all_nodes_multicast_addr(&self) -> std::net::Ipv6Addr {
        self.as_ref().get_link_local_all_nodes_multicast_addr()
    }

    fn get_mesh_local_prefix(&self) -> &MeshLocalPrefix {
        self.as_ref().get_mesh_local_prefix()
    }

    fn iter_next_neighbor_info(
        &self,
        ot_iter: &mut otNeighborInfoIterator,
    ) -> Option<NeighborInfo> {
        self.as_ref().iter_next_neighbor_info(ot_iter)
    }
}

impl Thread for Instance {
    fn become_leader(&self) -> Result<()> {
        Error::from(unsafe { otThreadBecomeLeader(self.as_ot_ptr()) }).into()
    }

    fn become_router(&self) -> Result<()> {
        Error::from(unsafe { otThreadBecomeRouter(self.as_ot_ptr()) }).into()
    }

    fn get_child_info_by_id(&self, child_id: u16) -> Result<otChildInfo> {
        let mut ret: otChildInfo = Default::default();
        Error::from(unsafe { otThreadGetChildInfoById(self.as_ot_ptr(), child_id, &mut ret) })
            .into_result()?;
        Ok(ret)
    }

    fn get_network_key(&self) -> NetworkKey {
        let mut ret = NetworkKey::default();
        unsafe { otThreadGetNetworkKey(self.as_ot_ptr(), ret.as_ot_mut_ptr()) }
        ret
    }

    fn set_network_key(&self, key: &NetworkKey) -> Result {
        Error::from(unsafe { otThreadSetNetworkKey(self.as_ot_ptr(), key.as_ot_ptr()) })
            .into_result()
    }

    fn get_network_name_as_slice(&self) -> &[u8] {
        unsafe {
            let slice = std::slice::from_raw_parts(
                otThreadGetNetworkName(self.as_ot_ptr()) as *const u8,
                OT_NETWORK_NAME_MAX_SIZE as usize,
            );
            let first_zero_index =
                slice.iter().position(|&x| x == 0).unwrap_or(OT_NETWORK_NAME_MAX_SIZE as usize);
            &slice[0..first_zero_index]
        }
    }

    fn set_network_name(&self, name: &NetworkName) -> Result {
        Error::from(unsafe { otThreadSetNetworkName(self.as_ot_ptr(), name.as_c_str()) })
            .into_result()
    }

    fn is_singleton(&self) -> bool {
        unsafe { otThreadIsSingleton(self.as_ot_ptr()) }
    }

    fn get_extended_pan_id(&self) -> &ExtendedPanId {
        unsafe {
            let xpanid = otThreadGetExtendedPanId(self.as_ot_ptr());
            ExtendedPanId::ref_from_ot_ptr(xpanid)
        }
        .unwrap()
    }

    fn set_extended_pan_id(&self, xpanid: &ExtendedPanId) -> Result {
        Error::from(unsafe { otThreadSetExtendedPanId(self.as_ot_ptr(), xpanid.as_ot_ptr()) })
            .into_result()
    }

    fn thread_set_enabled(&self, enabled: bool) -> Result {
        Error::from(unsafe { otThreadSetEnabled(self.as_ot_ptr(), enabled) }).into_result()
    }

    fn get_device_role(&self) -> ot::DeviceRole {
        unsafe { otThreadGetDeviceRole(self.as_ot_ptr()) }.into()
    }

    fn get_partition_id(&self) -> u32 {
        unsafe { otThreadGetPartitionId(self.as_ot_ptr()) }
    }

    fn get_rloc16(&self) -> u16 {
        unsafe { otThreadGetRloc16(self.as_ot_ptr()) }
    }

    fn get_link_mode(&self) -> ot::LinkModeConfig {
        unsafe { otThreadGetLinkMode(self.as_ot_ptr()) }.into()
    }

    fn set_link_mode(&self, link_mode_config: ot::LinkModeConfig) -> Result {
        Error::from(unsafe { otThreadSetLinkMode(self.as_ot_ptr(), link_mode_config.into()) })
            .into_result()
    }

    fn get_rloc(&self) -> std::net::Ipv6Addr {
        std::net::Ipv6Addr::from_ot(unsafe { *otThreadGetRloc(self.as_ot_ptr()) })
    }

    fn get_mesh_local_eid(&self) -> std::net::Ipv6Addr {
        std::net::Ipv6Addr::from_ot(unsafe { *otThreadGetMeshLocalEid(self.as_ot_ptr()) })
    }

    fn get_link_local_addr(&self) -> std::net::Ipv6Addr {
        std::net::Ipv6Addr::from_ot(unsafe { *otThreadGetLinkLocalIp6Address(self.as_ot_ptr()) })
    }

    fn get_link_local_all_nodes_multicast_addr(&self) -> std::net::Ipv6Addr {
        std::net::Ipv6Addr::from_ot(unsafe {
            *otThreadGetLinkLocalAllThreadNodesMulticastAddress(self.as_ot_ptr())
        })
    }

    fn get_mesh_local_prefix(&self) -> &MeshLocalPrefix {
        unsafe { MeshLocalPrefix::ref_from_ot_ptr(otThreadGetMeshLocalPrefix(self.as_ot_ptr())) }
            .unwrap()
    }

    fn iter_next_neighbor_info(
        &self,
        ot_iter: &mut otNeighborInfoIterator,
    ) -> Option<NeighborInfo> {
        unsafe {
            let mut ret = NeighborInfo::default();
            match Error::from(otThreadGetNextNeighborInfo(
                self.as_ot_ptr(),
                ot_iter as *mut otNeighborInfoIterator,
                ret.as_ot_mut_ptr(),
            )) {
                Error::NotFound => None,
                Error::None => Some(ret),
                err => unreachable!("Unexpected error from otThreadGetNextNeighborInfo: {:?}", err),
            }
        }
    }
}
