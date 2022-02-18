// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::DerefMut as _;

use super::{
    devices::{CommonInfo, DeviceSpecificInfo, Devices, EthernetInfo, LoopbackInfo},
    ethernet_worker,
    util::{IntoFidl, TryFromFidlWithContext as _, TryIntoCore as _, TryIntoFidlWithContext as _},
    DeviceStatusNotifier, InterfaceControl as _, Lockable, LockableContext,
    MutableDeviceState as _,
};

use fidl::prelude::*;
use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_stack::{
    self as fidl_net_stack, ForwardingEntry, StackRequest, StackRequestStream,
};
use futures::{TryFutureExt as _, TryStreamExt as _};
use log::{debug, error};
use net_types::{ethernet::Mac, SpecifiedAddr, UnicastAddr};
use netstack3_core::{
    add_ip_addr_subnet, add_route, del_ip_addr, del_route, get_all_routes, initialize_device, Ctx,
    EntryEither,
};

pub(crate) struct StackFidlWorker<C> {
    ctx: C,
}

struct LockedFidlWorker<'a, C: LockableContext> {
    ctx: <C as Lockable<'a, Ctx<C::Dispatcher>>>::Guard,
    worker: &'a StackFidlWorker<C>,
}

impl<C: LockableContext> StackFidlWorker<C> {
    async fn lock_worker(&self) -> LockedFidlWorker<'_, C> {
        let ctx = self.ctx.lock().await;
        LockedFidlWorker { ctx, worker: self }
    }
}

impl<C> StackFidlWorker<C>
where
    C: ethernet_worker::EthernetWorkerContext,
    C: Clone,
{
    pub(crate) async fn serve(ctx: C, stream: StackRequestStream) -> Result<(), fidl::Error> {
        stream
            .try_fold(Self { ctx }, |worker, req| async {
                match req {
                    StackRequest::AddEthernetInterface { topological_path, device, responder } => {
                        responder_send!(
                            responder,
                            &mut worker
                                .lock_worker()
                                .await
                                .fidl_add_ethernet_interface(topological_path, device)
                                .await
                        );
                    }
                    StackRequest::DelEthernetInterface { id, responder } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_del_ethernet_interface(id)
                        );
                    }
                    StackRequest::EnableInterfaceDeprecated { id, responder } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_enable_interface(id)
                        );
                    }
                    StackRequest::DisableInterfaceDeprecated { id, responder } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_disable_interface(id)
                        );
                    }
                    StackRequest::AddInterfaceAddressDeprecated { id, addr, responder } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_add_interface_address(id, addr)
                        );
                    }
                    StackRequest::DelInterfaceAddressDeprecated { id, addr, responder } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_del_interface_address(id, addr)
                        );
                    }
                    StackRequest::GetForwardingTable { responder } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_get_forwarding_table().iter_mut()
                        );
                    }
                    StackRequest::AddForwardingEntry { entry, responder } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_add_forwarding_entry(entry)
                        );
                    }
                    StackRequest::DelForwardingEntry {
                        entry:
                            fidl_net_stack::ForwardingEntry {
                                subnet,
                                device_id: _,
                                next_hop: _,
                                metric: _,
                            },
                        responder,
                    } => {
                        responder_send!(
                            responder,
                            &mut worker.lock_worker().await.fidl_del_forwarding_entry(subnet)
                        );
                    }
                    StackRequest::EnableIpForwarding { responder } => {
                        // TODO(https://fxbug.dev/76987): Support enabling IP forwarding.
                        let () = responder
                            .control_handle()
                            .shutdown_with_epitaph(fuchsia_zircon::Status::NOT_SUPPORTED);
                    }
                    StackRequest::DisableIpForwarding { responder } => {
                        // TODO(https://fxbug.dev/76987): Support disabling IP forwarding.
                        let () = responder
                            .control_handle()
                            .shutdown_with_epitaph(fuchsia_zircon::Status::NOT_SUPPORTED);
                    }
                    StackRequest::GetInterfaceIpForwarding { id: _, ip_version: _, responder } => {
                        // TODO(https://fxbug.dev/76987): Support querying per-NIC forwarding.
                        responder_send!(responder, &mut Err(fidl_net_stack::Error::NotSupported));
                    }
                    StackRequest::SetInterfaceIpForwarding {
                        id: _,
                        ip_version: _,
                        enabled: _,
                        responder,
                    } => {
                        // TODO(https://fxbug.dev/76987): Support configuring per-NIC forwarding.
                        responder_send!(responder, &mut Err(fidl_net_stack::Error::NotSupported));
                    }
                    StackRequest::GetDnsServerWatcher { watcher, control_handle: _ } => {
                        let () = watcher
                            .close_with_epitaph(fuchsia_zircon::Status::NOT_SUPPORTED)
                            .unwrap_or_else(|e| {
                                debug!("failed to close DNS server watcher {:?}", e)
                            });
                    }
                }
                Ok(worker)
            })
            .map_ok(|Self { ctx: _ }| ())
            .await
    }
}

impl<'a, C> LockedFidlWorker<'a, C>
where
    C: ethernet_worker::EthernetWorkerContext,
    C: Clone,
{
    async fn fidl_add_ethernet_interface(
        mut self,
        _topological_path: String,
        device: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    ) -> Result<u64, fidl_net_stack::Error> {
        let (
            client,
            fidl_fuchsia_hardware_ethernet_ext::EthernetInfo {
                mtu,
                features,
                mac: fidl_fuchsia_hardware_ethernet_ext::MacAddress { octets: mac_octets },
            },
        ) = ethernet_worker::setup_ethernet(
            device.into_proxy().map_err(|_| fidl_net_stack::Error::InvalidArgs)?,
        )
        .await
        .map_err(|_| fidl_net_stack::Error::Internal)?;

        let Ctx { state, dispatcher } = self.ctx.deref_mut();
        let client_stream = client.get_stream();

        let online = client
            .get_status()
            .await
            .map(|s| s.contains(ethernet_worker::DeviceStatus::ONLINE))
            .unwrap_or(false);
        let mac_addr =
            UnicastAddr::new(Mac::new(mac_octets)).ok_or(fidl_net_stack::Error::NotSupported)?;
        // We do not support updating the device's mac-address, mtu, and
        // features during it's lifetime, their cached states are hence not
        // updated once initialized.
        let comm_info = EthernetInfo {
            common_info: CommonInfo { mtu, admin_enabled: true },
            client,
            mac: mac_addr,
            features,
            phy_up: online,
        };

        let devices: &mut Devices = dispatcher.as_mut();
        let id = if online {
            let eth_id = state.add_ethernet_device(mac_addr, mtu);
            devices.add_active_device(eth_id, comm_info.into())
        } else {
            Some(devices.add_device(comm_info.into()))
        };
        match id {
            Some(id) => {
                ethernet_worker::EthernetWorker::new(id, self.worker.ctx.clone())
                    .spawn(client_stream);
                // If we have a core_id associated with id, that means the
                // device was added in the active state, so we must initialize
                // it using the new core_id.
                let devices: &Devices = self.ctx.dispatcher.as_ref();
                if let Some(core_id) = devices.get_core_id(id) {
                    initialize_device(&mut self.ctx, core_id);
                }
                Ok(id)
            }
            None => {
                // Send internal error if we can't allocate an id
                Err(fidl_net_stack::Error::Internal)
            }
        }
    }
}

impl<'a, C> LockedFidlWorker<'a, C>
where
    C: LockableContext,
    C::Dispatcher: AsMut<Devices>,
{
    fn fidl_del_ethernet_interface(mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        match self.ctx.dispatcher.as_mut().remove_device(id) {
            Some(_info) => {
                // TODO(rheacock): ensure that the core client deletes all data
                Ok(())
            }
            None => {
                // Invalid device ID
                Err(fidl_net_stack::Error::NotFound)
            }
        }
    }
}

impl<'a, C> LockedFidlWorker<'a, C>
where
    C: LockableContext,
    C::Dispatcher: AsRef<Devices>,
{
    fn fidl_add_interface_address(
        mut self,
        id: u64,
        addr: fidl_net::Subnet,
    ) -> Result<(), fidl_net_stack::Error> {
        let device_info =
            self.ctx.dispatcher.as_ref().get_device(id).ok_or(fidl_net_stack::Error::NotFound)?;
        // TODO(brunodalbo): We should probably allow adding static addresses
        // to interfaces that are not installed, return BadState for now
        let device_id = device_info.core_id().ok_or(fidl_net_stack::Error::BadState)?;

        add_ip_addr_subnet(
            &mut self.ctx,
            device_id,
            addr.try_into_core().map_err(IntoFidl::into_fidl)?,
        )
        .map_err(IntoFidl::into_fidl)
    }

    fn fidl_del_interface_address(
        mut self,
        id: u64,
        addr: fidl_net::Subnet,
    ) -> Result<(), fidl_net_stack::Error> {
        let device_info =
            self.ctx.dispatcher.as_ref().get_device(id).ok_or(fidl_net_stack::Error::NotFound)?;
        // TODO(gongt): Since addresses can't be added to inactive interfaces
        // they can't be deleted either; return BadState for now.
        let device_id = device_info.core_id().ok_or(fidl_net_stack::Error::BadState)?;
        let addr: SpecifiedAddr<_> = addr.addr.try_into_core().map_err(IntoFidl::into_fidl)?;

        del_ip_addr(&mut self.ctx, device_id, addr.into()).map_err(IntoFidl::into_fidl)
    }

    fn fidl_get_forwarding_table(self) -> Vec<fidl_net_stack::ForwardingEntry> {
        get_all_routes(&self.ctx)
            .filter_map(|entry| match entry.try_into_fidl_with_ctx(&self.ctx.dispatcher) {
                Ok(entry) => Some(entry),
                Err(_) => {
                    error!("Failed to map forwarding entry into FIDL");
                    None
                }
            })
            .collect()
    }

    fn fidl_add_forwarding_entry(
        mut self,
        entry: ForwardingEntry,
    ) -> Result<(), fidl_net_stack::Error> {
        let entry = match EntryEither::try_from_fidl_with_ctx(&self.ctx.dispatcher, entry) {
            Ok(entry) => entry,
            Err(e) => return Err(e.into()),
        };
        add_route(&mut self.ctx, entry).map_err(IntoFidl::into_fidl)
    }
}

impl<'a, C> LockedFidlWorker<'a, C>
where
    C: LockableContext,
{
    fn fidl_del_forwarding_entry(
        mut self,
        subnet: fidl_net::Subnet,
    ) -> Result<(), fidl_net_stack::Error> {
        if let Ok(subnet) = subnet.try_into_core() {
            del_route(&mut self.ctx, subnet).map_err(IntoFidl::into_fidl)
        } else {
            Err(fidl_net_stack::Error::InvalidArgs)
        }
    }
}

impl<'a, C> LockedFidlWorker<'a, C>
where
    C: LockableContext,
    C::Dispatcher: DeviceStatusNotifier,
    C::Dispatcher: AsRef<Devices> + AsMut<Devices>,
{
    fn fidl_enable_interface(mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.ctx.update_device_state(id, |dev_info| {
            let admin_enabled: &mut bool = match dev_info.info_mut() {
                DeviceSpecificInfo::Ethernet(EthernetInfo {
                    common_info: CommonInfo { admin_enabled, mtu: _ },
                    client: _,
                    mac: _,
                    features: _,
                    phy_up: _,
                }) => admin_enabled,
                DeviceSpecificInfo::Loopback(LoopbackInfo {
                    common_info: CommonInfo { admin_enabled, mtu: _ },
                }) => admin_enabled,
            };
            *admin_enabled = true;
        });
        self.ctx.enable_interface(id)
    }

    fn fidl_disable_interface(mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.ctx.update_device_state(id, |dev_info| {
            let admin_enabled: &mut bool = match dev_info.info_mut() {
                DeviceSpecificInfo::Ethernet(EthernetInfo {
                    common_info: CommonInfo { admin_enabled, mtu: _ },
                    client: _,
                    mac: _,
                    features: _,
                    phy_up: _,
                }) => admin_enabled,
                DeviceSpecificInfo::Loopback(LoopbackInfo {
                    common_info: CommonInfo { admin_enabled, mtu: _ },
                }) => admin_enabled,
            };
            *admin_enabled = false;
        });
        self.ctx.disable_interface(id)
    }
}
