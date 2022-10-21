// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/95485): Remove.
#![allow(dead_code)]

use {
    crate::guest_ethernet,
    anyhow::{anyhow, Error},
    fidl_fuchsia_hardware_ethernet::MacAddress,
    fuchsia_zircon as zx,
    futures::{channel::mpsc::UnboundedReceiver, StreamExt},
    machina_virtio_device::{GuestMem, WrappedDescChainStream},
    std::{cell::RefCell, pin::Pin},
    virtio_device::{
        chain::{ReadableChain, WritableChain},
        mem::DriverMem,
        queue::DriverNotify,
    },
};

pub struct NetDevice {
    // Safe wrapper around the C++ FFI for interacting with the netstack.
    ethernet: Pin<Box<guest_ethernet::GuestEthernet>>,

    // Contains any status value sent by the C++ guest ethernet device.
    status_rx: RefCell<UnboundedReceiver<zx::Status>>,

    // Contains a notify that the netstack is ready to receive more TX packets. When resuming
    // sending packets to the netstack, this mpsc should be fully drained.
    notify_rx: RefCell<UnboundedReceiver<()>>,

    // Contains RX packets from the netstack to be sent to the guest. Memory pointed to by the
    // RX packet is guaranteed to be valid until `complete` is called with the matching buffer ID.
    receive_packet_rx: RefCell<UnboundedReceiver<guest_ethernet::RxPacket>>,
}

impl NetDevice {
    // Create a NetDevice. This creates the C++ GuestEthernet object, initializes the C++ dispatch
    // loop on a new thread, etc.
    pub fn new() -> Result<Self, Error> {
        let guest_ethernet::GuestEthernetNewResult {
            guest_ethernet,
            status_rx,
            notify_rx,
            receive_packet_rx,
        } = guest_ethernet::GuestEthernet::new()
            .map_err(|status| anyhow!("failed to create GuestEthernet: {}", status))?;

        Ok(Self {
            ethernet: guest_ethernet,
            status_rx: RefCell::new(status_rx),
            notify_rx: RefCell::new(notify_rx),
            receive_packet_rx: RefCell::new(receive_packet_rx),
        })
    }

    // Handles the TX queue stream, pulling readable chains off of the stream sequentially and
    // writing packets to the netstack. This should only be invoked once, and will return if
    // the stream is closed.
    pub async fn handle_tx_stream<'a, 'b, N: DriverNotify>(
        &self,
        mut tx_stream: WrappedDescChainStream<'a, 'b, N>,
        guest_mem: &'a GuestMem,
    ) -> Result<(), Error> {
        while let Some(chain) = tx_stream.next().await {
            let readable_chain = ReadableChain::new(chain, guest_mem);
            self.handle_readable_chain(readable_chain).await?;
        }

        Ok(())
    }

    async fn handle_readable_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        _chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // TODO(fxbug.dev/95485): Do this.
        Ok(())
    }

    // Handles the RX queue stream, pulling writable chains off of the stream sequentially. When
    // a writable chain is available and there is data available from the netstack, data will be
    // written to the chain. This should only be invoked once, and will return if the stream is
    // closed.
    pub async fn handle_rx_stream<'a, 'b, N: DriverNotify>(
        &self,
        mut rx_stream: WrappedDescChainStream<'a, 'b, N>,
        guest_mem: &'a GuestMem,
    ) -> Result<(), Error> {
        while let Some(chain) = rx_stream.next().await {
            let writable_chain = match WritableChain::new(chain, guest_mem) {
                Ok(chain) => chain,
                Err(err) => {
                    // Ignore this chain and continue processing.
                    tracing::error!(%err, "Device received a bad chain on the RX queue");
                    continue;
                }
            };

            self.handle_writable_chain(writable_chain).await?;
        }

        Ok(())
    }

    async fn handle_writable_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        _chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // TODO(fxbug.dev/95485): Do this.
        Ok(())
    }

    // Initialize the C++ GuestEthernet object. This parses the MAC address, prepares the Rust
    // callbacks, and registers the object with the netstack.
    pub async fn initialize(
        &mut self,
        mac_address: MacAddress,
        enable_bridge: bool,
    ) -> Result<(), Error> {
        self.ethernet.initialize(mac_address, enable_bridge).map_err(|status| {
            anyhow!("failed to setup asynchronous GuestEthernet initialization: {}", status)
        })?;

        // The C++ GuestEthernet object will push an ZX_OK status after a successful initialization.
        self.ready().await
    }

    async fn ready(&mut self) -> Result<(), Error> {
        let status =
            self.status_rx.get_mut().next().await.expect("unexpected end of status stream");

        if status == zx::Status::OK {
            Ok(())
        } else {
            Err(anyhow!("failed to initialize GuestEthernet: {}", status))
        }
    }

    // Surfaces any unrecoverable errors encountered by the C++ GuestEthernet object. After the
    // first ZX_OK (consumed by ready), only errors should be pushed to this channel.
    pub async fn get_error_from_guest_ethernet(&self) -> Result<(), Error> {
        let status =
            self.status_rx.borrow_mut().next().await.expect("unexpected end of status stream");
        assert!(status != zx::Status::OK, "GuestEthernet shouldn't send ZX_OK once its ready");

        Err(anyhow!("GuestEthernet encountered an unrecoverable error: {}", status))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // TODO(fxbug.dev/95485): Remove.
    #[fuchsia::test]
    async fn create_and_destroy_device() {
        let mut device = NetDevice::new().expect("failed to create net device");
        device
            .initialize(MacAddress { octets: [1u8, 2, 3, 4, 5, 6] }, true)
            .await
            .expect("failed to begin initializing net device");
    }
}
