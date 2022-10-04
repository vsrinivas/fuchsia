// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    devices::{BindingId, DeviceSpecificInfo, Devices, EthernetInfo},
    DeviceStatusNotifier, InterfaceControl as _, LockableContext, MutableDeviceState as _,
};
use anyhow::Error;
use assert_matches::assert_matches;
use ethernet as eth;
pub use fidl_ethernet::DeviceStatus;
use fidl_fuchsia_hardware_ethernet as fidl_ethernet;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{TryFutureExt as _, TryStreamExt as _};
use log::{debug, error, info, trace};
use netstack3_core::{device::receive_frame, BufferNonSyncContext, Ctx};
use packet::serialize::Buf;
use std::ops::DerefMut as _;

pub async fn setup_ethernet(
    dev: fidl_ethernet::DeviceProxy,
) -> Result<(eth::Client, fidl_fuchsia_hardware_ethernet_ext::EthernetInfo), Error> {
    let vmo = zx::Vmo::create(256 * eth::DEFAULT_BUFFER_SIZE as u64)?;
    let eth_client = eth::Client::new(dev, vmo, eth::DEFAULT_BUFFER_SIZE, "netstack3").await?;
    let info = eth_client.info().await?;
    eth_client.start().await?;
    Ok((eth_client, info))
}

/// The worker that receives messages from the ethernet device, and passes them
/// on to the main event loop.
pub(crate) struct EthernetWorker<C> {
    id: BindingId,
    ctx: C,
}

impl<C> EthernetWorker<C> {
    pub(crate) fn new(id: BindingId, ctx: C) -> Self {
        EthernetWorker { id, ctx }
    }
}

// TODO(https://github.com/rust-lang/rust/issues/20671): Replace the duplicate associated type with
// a where clause bounding the parent trait's associated type.
//
// OR
//
// TODO(https://github.com/rust-lang/rust/issues/52662): Replace the duplicate associated type with
// a bound on the parent trait's associated type.
pub(crate) trait EthernetWorkerContext:
    LockableContext<NonSyncCtx = <Self as EthernetWorkerContext>::NonSyncCtx> + Send + Sync + 'static
{
    type NonSyncCtx: for<'a> BufferNonSyncContext<Buf<&'a mut [u8]>>
        + AsRef<Devices>
        + AsMut<Devices>
        + DeviceStatusNotifier
        + Send
        + Sync;
}

impl<T> EthernetWorkerContext for T
where
    T: LockableContext + Send + Sync + 'static,
    T::NonSyncCtx: for<'a> BufferNonSyncContext<Buf<&'a mut [u8]>>
        + AsRef<Devices>
        + AsMut<Devices>
        + DeviceStatusNotifier
        + Send
        + Sync,
{
    type NonSyncCtx = T::NonSyncCtx;
}

impl<C: EthernetWorkerContext> EthernetWorker<C> {
    pub fn spawn(self, mut events: eth::EventStream)
    where
        Ctx<<C as EthernetWorkerContext>::NonSyncCtx>: Send,
    {
        fasync::Task::spawn(
            async move {
                let Self { ref ctx, id } = self;
                // TODO(brunodalbo) remove this temporary buffer, we should be
                // owning buffers until processing is done, this is just an
                // unnecessary copy. Also, its size is implicitly defined by the
                // buffer size that we used to create the VMO, which is not
                // taking into consideration the device's MTU at all.
                let mut buf = [0; eth::DEFAULT_BUFFER_SIZE];
                while let Some(evt) = events.try_next().await? {
                    match evt {
                        eth::Event::StatusChanged => {
                            let mut ctx = ctx.lock().await;
                            info!("device {:?} status changed signal", id);
                            // We need to call get_status even if we don't use the output, since
                            // calling it acks the message, and prevents the device from sending
                            // more status changed messages.
                            if let Some(device) = ctx.non_sync_ctx.as_ref().get_device(id) {
                                let device = assert_matches!(
                                    device.info(),
                                    DeviceSpecificInfo::Ethernet(device) => device
                                );
                                let EthernetInfo { common_info: _, client, mac: _, features: _, phy_up: _, interface_control: _} = device;
                                if let Ok(status) = client.get_status().await {
                                    info!("device {:?} status changed to: {:?}", id, status);
                                    // Handle the new device state. If this results in no change, no
                                    // state will be modified.
                                    if status.contains(fidl_ethernet::DeviceStatus::ONLINE) {
                                        ctx.update_device_state(id, |dev_info| {
                                            let phy_up: &mut bool = assert_matches!(
                                                dev_info.info_mut(),
                                                DeviceSpecificInfo::Ethernet(EthernetInfo { common_info: _, client: _, mac: _, features: _, phy_up, interface_control: _}) => phy_up
                                            );
                                            *phy_up = true;
                                        });
                                        ctx.enable_interface(id).unwrap_or_else(|e| {
                                            trace!("phy enable interface failed: {:?}", e)
                                        });
                                    } else {
                                        ctx.update_device_state(id, |dev_info| {
                                            let phy_up: &mut bool = assert_matches!(
                                                dev_info.info_mut(),
                                                DeviceSpecificInfo::Ethernet(EthernetInfo { common_info: _, client: _, mac: _, features: _, phy_up, interface_control: _}) => phy_up
                                            );
                                            *phy_up = false;
                                        });
                                        ctx.disable_interface(id).unwrap_or_else(|e| {
                                            trace!("phy enable disable failed: {:?}", e)
                                        });
                                    }
                                }
                            }
                        }
                        eth::Event::Receive(rx, _flags) => {
                            let len = rx.read(&mut buf);
                            let mut ctx = ctx.lock().await; let Ctx { sync_ctx, non_sync_ctx } = ctx.deref_mut();

                            if let Some(id) =
                                AsRef::<Devices>::as_ref(non_sync_ctx).get_core_id(id)
                            {
                                receive_frame(sync_ctx, non_sync_ctx, &id, Buf::new(&mut buf[..len], ..))
                                    .unwrap_or_else(|e| error!("error receiving frame: {}", e))
                            } else {
                                debug!("received ethernet frame on disabled device: {}", id);
                            }
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| error!("{:?}", e)),
        )
        .detach()
    }
}
