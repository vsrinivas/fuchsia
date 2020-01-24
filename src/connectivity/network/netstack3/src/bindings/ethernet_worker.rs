// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use ethernet as eth;
use fidl_fuchsia_hardware_ethernet as fidl_ethernet;
pub use fidl_fuchsia_hardware_ethernet_ext::{EthernetInfo, EthernetStatus};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{TryFutureExt, TryStreamExt};
use log::{debug, error, info, trace};
use netstack3_core::receive_frame;
use packet::serialize::Buf;
use std::ops::DerefMut;

use super::{
    context::MultiInnerValue,
    devices::{BindingId, Devices},
    StackContext, StackDispatcher,
};

pub async fn setup_ethernet(
    dev: fidl_ethernet::DeviceProxy,
) -> Result<(eth::Client, EthernetInfo), Error> {
    let vmo = zx::Vmo::create(256 * eth::DEFAULT_BUFFER_SIZE as u64)?;
    let eth_client = eth::Client::new(dev, vmo, eth::DEFAULT_BUFFER_SIZE, "netstack3").await?;
    let info = eth_client.info().await?;
    eth_client.start().await?;
    Ok((eth_client, info))
}

/// The worker that receives messages from the ethernet device, and passes them
/// on to the main event loop.
pub(crate) struct EthernetWorker<C: StackContext> {
    id: BindingId,
    ctx: C,
}

impl<C: StackContext> EthernetWorker<C> {
    pub(crate) fn new(id: BindingId, ctx: C) -> Self {
        EthernetWorker { id, ctx }
    }

    async fn status_changed(&self) {
        let mut ctx = self.ctx.lock().await;
        info!("device {:?} status changed signal", self.id);
        // We need to call get_status even if we don't use the output, since calling it
        // acks the message, and prevents the device from sending more status changed
        // messages.
        if let Some(device) = ctx.dispatcher().get_device_info(self.id) {
            if let Ok(status) = device.client().get_status().await {
                info!("device {:?} status changed to: {:?}", self.id, status);
                // Handle the new device state. If this results in no change, no state
                // will be modified.
                if status.contains(EthernetStatus::ONLINE) {
                    ctx.update_device_state(self.id, |dev_info| dev_info.set_phy_up(true));
                    ctx.enable_interface(self.id)
                        .unwrap_or_else(|e| trace!("Phy enable interface failed: {:?}", e));
                } else {
                    ctx.update_device_state(self.id, |dev_info| dev_info.set_phy_up(false));
                    ctx.disable_interface(self.id)
                        .unwrap_or_else(|e| trace!("Phy enable disable failed: {:?}", e));
                }
            }
        }
    }

    async fn receive(&self, rx: eth::RxBuffer, buf: &mut [u8]) {
        let len = rx.read(buf);
        let mut ctx = self.ctx.lock().await;

        if let Some(id) = ctx.dispatcher().get_inner::<Devices>().get_core_id(self.id) {
            receive_frame::<Buf<&mut [u8]>, _>(ctx.deref_mut(), id, Buf::new(&mut buf[..len], ..));
        } else {
            debug!("Received ethernet frame on disabled device: {}", self.id);
        }
    }

    pub fn spawn(self, mut events: eth::EventStream) {
        fasync::spawn(
            async move {
                // TODO(brunodalbo) remove this temporary buffer, we should be
                // owning buffers until processing is done, this is just an
                // unnecessary copy. Also, its size is implicitly defined by the
                // buffer size that we used to create the VMO, which is not
                // taking into consideration the device's MTU at all.
                let mut tmp_buff = [0; eth::DEFAULT_BUFFER_SIZE];
                while let Some(evt) = events.try_next().await? {
                    match evt {
                        eth::Event::StatusChanged => {
                            self.status_changed().await;
                        }
                        eth::Event::Receive(rx, _flags) => {
                            self.receive(rx, &mut tmp_buff[..]).await;
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}
