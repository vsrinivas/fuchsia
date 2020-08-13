// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    context::MultiInnerValue,
    devices::{CommonInfo, Devices},
    ethernet_worker,
    util::{IntoFidl, TryFromFidlWithContext, TryIntoCore, TryIntoFidl, TryIntoFidlWithContext},
    LockedStackContext, StackContext, StackDispatcher,
};

use fidl_fuchsia_net as fidl_net;
use fidl_fuchsia_net_stack::{
    self as fidl_net_stack, AdministrativeStatus, ForwardingEntry, InterfaceInfo,
    InterfaceProperties, PhysicalStatus, StackRequest, StackRequestStream,
};
use fuchsia_async as fasync;
use futures::{TryFutureExt, TryStreamExt};
use log::{debug, error};
use net_types::{ethernet::Mac, SpecifiedAddr};
use netstack3_core::{
    add_ip_addr_subnet, add_route, del_device_route, del_ip_addr, get_all_ip_addr_subnets,
    get_all_routes, initialize_device, EntryEither,
};

pub(crate) struct StackFidlWorker<C: StackContext> {
    ctx: C,
}

struct LockedFidlWorker<'a, C: StackContext> {
    ctx: LockedStackContext<'a, C>,
    worker: &'a StackFidlWorker<C>,
}

impl<C: StackContext> StackFidlWorker<C> {
    pub(crate) fn spawn(ctx: C, mut stream: StackRequestStream) {
        fasync::Task::spawn(
            async move {
                let worker = StackFidlWorker { ctx };
                while let Some(e) = stream.try_next().await? {
                    worker.handle_request(e).await;
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                debug!("Stack Fidl worker finished with error {}", e)
            }),
        )
        .detach();
    }

    async fn lock_worker(&self) -> LockedFidlWorker<'_, C> {
        let ctx = self.ctx.lock().await;
        LockedFidlWorker { ctx, worker: self }
    }

    async fn handle_request(&self, req: StackRequest) {
        match req {
            StackRequest::AddInterface { responder, .. } => {
                responder_send!(responder, &mut Err(fidl_net_stack::Error::NotSupported));
            }
            StackRequest::AddEthernetInterface { topological_path, device, responder } => {
                responder_send!(
                    responder,
                    &mut self
                        .lock_worker()
                        .await
                        .fidl_add_ethernet_interface(topological_path, device)
                        .await
                );
            }
            StackRequest::DelEthernetInterface { id, responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_del_ethernet_interface(id)
                );
            }
            StackRequest::ListInterfaces { responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_list_interfaces().iter_mut()
                );
            }
            StackRequest::GetInterfaceInfo { id, responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_get_interface_info(id)
                );
            }
            StackRequest::EnableInterface { id, responder } => {
                responder_send!(responder, &mut self.lock_worker().await.fidl_enable_interface(id));
            }
            StackRequest::DisableInterface { id, responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_disable_interface(id)
                );
            }
            StackRequest::AddInterfaceAddress { id, addr, responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_add_interface_address(id, addr)
                );
            }
            StackRequest::DelInterfaceAddress { id, addr, responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_del_interface_address(id, addr)
                );
            }
            StackRequest::GetForwardingTable { responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_get_forwarding_table().iter_mut()
                );
            }
            StackRequest::AddForwardingEntry { entry, responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_add_forwarding_entry(entry)
                );
            }
            StackRequest::DelForwardingEntry { subnet, responder } => {
                responder_send!(
                    responder,
                    &mut self.lock_worker().await.fidl_del_forwarding_entry(subnet)
                );
            }
            StackRequest::EnablePacketFilter { id: _, responder: _ } => {
                // TODO(toshik)
            }
            StackRequest::DisablePacketFilter { id: _, responder: _ } => {
                // TODO(toshik)
            }
            StackRequest::EnableIpForwarding { responder: _ } => {
                // TODO(toshik)
            }
            StackRequest::DisableIpForwarding { responder: _ } => {
                // TODO(toshik)
            }
            StackRequest::GetDnsServerWatcher { watcher, control_handle: _ } => {
                let () = watcher
                    .close_with_epitaph(fuchsia_zircon::Status::NOT_SUPPORTED)
                    .unwrap_or_else(|e| debug!("failed to close DNS server watcher {:?}", e));
            }
        }
    }
}

impl<'a, C: StackContext> LockedFidlWorker<'a, C> {
    async fn fidl_add_ethernet_interface(
        mut self,
        topological_path: String,
        device: fidl::endpoints::ClientEnd<fidl_fuchsia_hardware_ethernet::DeviceMarker>,
    ) -> Result<u64, fidl_net_stack::Error> {
        let (client, info) = ethernet_worker::setup_ethernet(
            device.into_proxy().map_err(|_| fidl_net_stack::Error::InvalidArgs)?,
        )
        .await
        .map_err(|_| fidl_net_stack::Error::Internal)?;

        let (state, disp) = self.ctx.state_and_dispatcher();
        let client_stream = client.get_stream();

        let online = client
            .get_status()
            .await
            .map(|s| s.contains(ethernet_worker::DeviceStatus::Online))
            .unwrap_or(false);
        // We do not support updating the device's mac-address,
        // mtu, and features during it's lifetime, their cached
        // states are hence not updated once initialized.
        let comm_info = CommonInfo::new(
            topological_path,
            client,
            info.mac.into(),
            info.mtu,
            info.features,
            true,
            online,
        );

        let id = if online {
            let eth_id = state.add_ethernet_device(Mac::new(info.mac.octets), info.mtu);
            disp.get_inner_mut::<Devices>().add_active_device(eth_id, comm_info)
        } else {
            Some(disp.get_inner_mut::<Devices>().add_device(comm_info))
        };
        match id {
            Some(id) => {
                ethernet_worker::EthernetWorker::new(id, self.worker.ctx.clone())
                    .spawn(client_stream);
                // If we have a core_id associated with id, that means
                // the device was added in the active state, so we must
                // initialize it using the new core_id.
                if let Some(core_id) = self.ctx.dispatcher().get_inner::<Devices>().get_core_id(id)
                {
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

    fn fidl_del_ethernet_interface(mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        match self.ctx.dispatcher_mut().get_inner_mut::<Devices>().remove_device(id) {
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

    fn fidl_list_interfaces(self) -> Vec<fidl_net_stack::InterfaceInfo> {
        let mut devices = Vec::new();
        for device in self.ctx.dispatcher().get_inner::<Devices>().iter_devices() {
            let mut addresses = Vec::new();
            if let Some(core_id) = device.core_id() {
                for addr in get_all_ip_addr_subnets(&self.ctx, core_id) {
                    match addr.try_into_fidl() {
                        Ok(addr) => addresses.push(addr),
                        Err(e) => {
                            error!("failed to map interface address/subnet into FIDL: {:?}", e)
                        }
                    }
                }
            };
            devices.push(InterfaceInfo {
                id: device.id(),
                properties: InterfaceProperties {
                    name: "[TBD]".to_owned(), // TODO(porce): Follow up to populate the name
                    topopath: device.path().clone(),
                    filepath: "[TBD]".to_owned(), // TODO(porce): Follow up to populate
                    mac: Some(Box::new(device.mac())),
                    mtu: device.mtu(),
                    features: device.features(),
                    administrative_status: if device.admin_enabled() {
                        AdministrativeStatus::Enabled
                    } else {
                        AdministrativeStatus::Disabled
                    },
                    physical_status: if device.phy_up() {
                        PhysicalStatus::Up
                    } else {
                        PhysicalStatus::Down
                    },
                    addresses, // TODO(gongt) Handle tentative IPv6 addresses
                },
            });
        }
        devices
    }

    fn fidl_get_interface_info(
        self,
        id: u64,
    ) -> Result<fidl_net_stack::InterfaceInfo, fidl_net_stack::Error> {
        let device =
            self.ctx.dispatcher().get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;
        let mut addresses = Vec::new();
        if let Some(core_id) = device.core_id() {
            for addr in get_all_ip_addr_subnets(&self.ctx, core_id) {
                match addr.try_into_fidl() {
                    Ok(addr) => addresses.push(addr),
                    Err(e) => error!("failed to map interface address/subnet into FIDL: {:?}", e),
                }
            }
        };
        return Ok(InterfaceInfo {
            id: device.id(),
            properties: InterfaceProperties {
                name: "[TBD]".to_owned(), // TODO(porce): Follow up to populate the name
                topopath: device.path().clone(),
                filepath: "[TBD]".to_owned(), // TODO(porce): Follow up to populate
                mac: Some(Box::new(device.mac())),
                mtu: device.mtu(),
                features: device.features(),
                administrative_status: if device.admin_enabled() {
                    AdministrativeStatus::Enabled
                } else {
                    AdministrativeStatus::Disabled
                },
                physical_status: if device.phy_up() {
                    PhysicalStatus::Up
                } else {
                    PhysicalStatus::Down
                },
                addresses, // TODO(gongt) Handle tentative IPv6 addresses
            },
        });
    }

    fn fidl_add_interface_address(
        mut self,
        id: u64,
        addr: fidl_net::Subnet,
    ) -> Result<(), fidl_net_stack::Error> {
        let device_info =
            self.ctx.dispatcher().get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;
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
            self.ctx.dispatcher().get_device_info(id).ok_or(fidl_net_stack::Error::NotFound)?;
        // TODO(gongt): Since addresses can't be added to inactive interfaces
        // they can't be deleted either; return BadState for now.
        let device_id = device_info.core_id().ok_or(fidl_net_stack::Error::BadState)?;
        let addr: SpecifiedAddr<_> = addr.addr.try_into_core().map_err(IntoFidl::into_fidl)?;

        del_ip_addr(&mut self.ctx, device_id, addr.into()).map_err(IntoFidl::into_fidl)
    }

    fn fidl_get_forwarding_table(self) -> Vec<fidl_net_stack::ForwardingEntry> {
        get_all_routes(&self.ctx)
            .filter_map(|entry| match entry.try_into_fidl_with_ctx(self.ctx.dispatcher()) {
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
        let entry = match EntryEither::try_from_fidl_with_ctx(self.ctx.dispatcher(), entry) {
            Ok(entry) => entry,
            Err(e) => return Err(e.into()),
        };
        add_route(&mut self.ctx, entry).map_err(IntoFidl::into_fidl)
    }

    fn fidl_del_forwarding_entry(
        mut self,
        subnet: fidl_net::Subnet,
    ) -> Result<(), fidl_net_stack::Error> {
        if let Ok(subnet) = subnet.try_into_core() {
            del_device_route(&mut self.ctx, subnet).map_err(IntoFidl::into_fidl)
        } else {
            Err(fidl_net_stack::Error::InvalidArgs)
        }
    }

    fn fidl_enable_interface(mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.ctx.update_device_state(id, |dev_info| dev_info.set_admin_enabled(true));
        self.ctx.enable_interface(id)
    }

    fn fidl_disable_interface(mut self, id: u64) -> Result<(), fidl_net_stack::Error> {
        self.ctx.update_device_state(id, |dev_info| dev_info.set_admin_enabled(false));
        self.ctx.disable_interface(id)
    }
}
