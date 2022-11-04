// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        guest_ethernet::{GuestEthernetInterface, GuestEthernetNewResult, RxPacket},
        wire,
    },
    anyhow::{anyhow, Error},
    fidl_fuchsia_hardware_ethernet::MacAddress,
    fuchsia_zircon as zx,
    futures::{channel::mpsc::UnboundedReceiver, StreamExt},
    machina_virtio_device::{GuestMem, WrappedDescChainStream},
    std::{cell::RefCell, io::Write, pin::Pin},
    virtio_device::{
        chain::{ReadableChain, Remaining, WritableChain},
        mem::{DeviceRange, DriverMem},
        queue::DriverNotify,
    },
    zerocopy::AsBytes,
};

pub struct NetDevice<T: GuestEthernetInterface> {
    // Safe wrapper around the C++ FFI for interacting with the netstack.
    ethernet: Pin<Box<T>>,

    // Contains any status value sent by the C++ guest ethernet device.
    status_rx: RefCell<UnboundedReceiver<zx::Status>>,

    // Contains a notify that the netstack is ready to receive more TX packets. When resuming
    // sending packets to the netstack, this mpsc should be fully drained.
    notify_rx: RefCell<UnboundedReceiver<()>>,

    // Contains RX packets from the netstack to be sent to the guest. Memory pointed to by the
    // RX packet is guaranteed to be valid until `complete` is called with the matching buffer ID.
    receive_packet_rx: RefCell<UnboundedReceiver<RxPacket>>,
}

impl<T: GuestEthernetInterface> NetDevice<T> {
    // Create a NetDevice. This creates the C++ GuestEthernet object, initializes the C++ dispatch
    // loop on a new thread, etc.
    pub fn new() -> Result<Self, zx::Status> {
        let GuestEthernetNewResult { guest_ethernet, status_rx, notify_rx, receive_packet_rx } =
            T::new()?;

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
            if let Err(err) = self.handle_readable_chain(readable_chain).await {
                // TODO(fxbug.dev/95485): See if we want to drop this to debug level due to noise.
                tracing::error!("Dropping TX packet: {}", err);
            }
        }

        Ok(())
    }

    async fn handle_readable_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // Task is running, so any pending notifies are now stale.
        self.drain_notify_mpsc();

        let chain_bytes = chain
            .remaining()
            .map_err(|err| anyhow!("failed to query chain for remaining bytes: {}", err))?
            .bytes;
        if chain_bytes < std::mem::size_of::<wire::VirtioNetHeader>() {
            return Err(anyhow!("Chain does not contain a VirtioNetHeader"));
        }

        if (chain_bytes - std::mem::size_of::<wire::VirtioNetHeader>()) > u16::MAX.into() {
            return Err(anyhow!("Packet data must fit within u16::MAX"));
        }

        // Unwrap since we know this chain has at least one range as it has non-zero remaining
        // bytes.
        let range = chain
            .next()
            .transpose()
            .map_err(|err| anyhow!("Failed to iterate over chain: {}", err))?
            .unwrap();
        if range.len() != chain_bytes {
            // Section 5.1.6.2 Packet Transmission
            //
            // The header and packet are added as one output descriptor to the transmitq.
            return Err(anyhow!("TX Packet incorrectly fragmented over multiple descriptors"));
        }

        while let Err(err) = self.process_tx_packet(&range) {
            if err != zx::Status::SHOULD_WAIT {
                return Err(anyhow!("failed to send packet to netstack: {}", err));
            }

            // The netstack sending a SHOULD_WAIT is fine, it just means that this device has
            // utilized all currently available buffer space. The netstack will notify this device
            // when TX may resume by pushing an empty value to the notify channel.
            //
            // Notifies may be spurious as buffers are added and removed within a different thread,
            // so we may attempt to process a packet a few times under heavy load.
            self.notify_rx.borrow_mut().next().await.expect("unexpected end of notify stream");
        }

        Ok(())
    }

    // Send a packet to the netstack. The header is stripped off of the packet before it is
    // sent. Note that we have already checked that the data segment is at least as long as the
    // header, so this is a safe calculation.
    fn process_tx_packet(&self, range: &DeviceRange<'_>) -> Result<(), zx::Status> {
        let header_size = std::mem::size_of::<wire::VirtioNetHeader>();
        let data_range = unsafe {
            (range.try_ptr().unwrap() as *const u8).offset(header_size.try_into().unwrap())
        };
        let data_length = range.len() - header_size;

        self.ethernet.send(data_range, data_length as u16)
    }

    // The C++ device runs in a different thread and may notify us to continue TX processing
    // multiple times before the Rust executor resumes the TX task. Once the task is running
    // again, clear all notifies.
    fn drain_notify_mpsc(&self) {
        while let Ok(_) = self.notify_rx.borrow_mut().try_next() {
            // Keep draining the channel.
        }
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

            if let Err(err) = self.handle_writable_chain(writable_chain).await {
                // TODO(fxbug.dev/95485): See if we want to drop this to debug level due to noise.
                tracing::error!("Error processing RX packet: {}", err);
            }
        }

        Ok(())
    }

    async fn handle_writable_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let Remaining { bytes, descriptors } = chain.remaining().map_err(|err| {
            anyhow!("failed to query chain for remaining bytes and descriptors: {}", err)
        })?;
        if bytes < wire::REQUIRED_RX_BUFFER_SIZE {
            return Err(anyhow!(
                "Writable chain ({} bytes) is smaller than minimum size ({} bytes)",
                bytes,
                wire::REQUIRED_RX_BUFFER_SIZE
            ));
        }
        if descriptors != 1 {
            // 5.1.6.3.2 Device Requirements: Setting Up Receive Buffers
            //
            // The device MUST use only a single descriptor if VIRTIO_NET_F_MRG_RXBUF was not
            // negotiated.
            return Err(anyhow!("RX buffer incorrectly fragmented over multiple descriptors"));
        }

        let packet = self
            .receive_packet_rx
            .borrow_mut()
            .next()
            .await
            .expect("unexpected end of RX packet stream");

        let result = NetDevice::<T>::handle_packet(&packet, bytes, chain);
        if result.is_err() {
            self.ethernet.complete(packet, zx::Status::INTERNAL);
        } else {
            self.ethernet.complete(packet, zx::Status::OK);
        }

        result
    }

    // Helper function to write an RxPacket to a chain.
    fn handle_packet<'a, 'b, N: DriverNotify, M: DriverMem>(
        packet: &RxPacket,
        available_bytes: usize,
        mut chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        if packet.len > (available_bytes - std::mem::size_of::<wire::VirtioNetHeader>()) {
            return Err(anyhow!("Packet is too large for provided RX buffers"));
        }

        let header = wire::VirtioNetHeader {
            // Section 5.1.6.4.1 Device Requirements: Processing of Incoming Packets
            //
            // If VIRTIO_NET_F_MRG_RXBUF has not been negotiated, the device MUST
            // set num_buffers to 1.
            num_buffers: wire::LE16::new(1),
            // If none of the VIRTIO_NET_F_GUEST_TSO4, TSO6 or UFO options have been
            // negotiated, the device MUST set gso_type to VIRTIO_NET_HDR_GSO_NONE.
            gso_type: wire::GsoType::None.into(),
            // If VIRTIO_NET_F_GUEST_CSUM is not negotiated, the device MUST set
            // flags to zero and SHOULD supply a fully checksummed packet to the
            // driver.
            flags: 0,
            ..wire::VirtioNetHeader::default()
        };

        // Write the header, updating the bytes written.
        if let Err(err) = chain.write_all(header.as_bytes()) {
            return Err(anyhow!("Failed to write packet header: {}", err));
        }

        // A note on safety:
        //   * No references (mutable or unmutable) to this range are held elsewhere. Other
        //     pointers may exist but will not be dereferenced while this slice is held.
        //   * This is a u8 pointer which has no alignment constraints.
        //   * This memory is guaranteed valid until Complete is called with the buffer_id.
        let slice = unsafe { std::slice::from_raw_parts(packet.data, packet.len) };

        // Write the data portion, updating the bytes written.
        if let Err(err) = chain.write_all(slice) {
            return Err(anyhow!("Failed to write packet data: {}", err));
        }

        Ok(())
    }

    // Initialize the C++ GuestEthernet object. This parses the MAC address, prepares the Rust
    // callbacks, and registers the object with the netstack.
    pub async fn initialize(
        &mut self,
        mac_address: MacAddress,
        enable_bridge: bool,
    ) -> Result<(), zx::Status> {
        // The C++ GuestEthernet object will push an ZX_OK status after a successful initialization.
        self.ethernet.initialize(mac_address, enable_bridge)?;
        self.ready().await
    }

    async fn ready(&mut self) -> Result<(), zx::Status> {
        self.status_rx.get_mut().next().await.expect("unexpected end of status stream").into()
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
    use {
        super::*,
        async_utils::PollExt,
        fuchsia_async as fasync,
        futures::channel::mpsc::{self, UnboundedSender},
        rand::{distributions::Standard, Rng},
        std::{cell::RefCell, collections::VecDeque},
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
        zerocopy::FromBytes,
    };

    struct TestGuestEthernet {
        status_tx: UnboundedSender<zx::Status>,
        notify_tx: UnboundedSender<()>,
        receive_packet_tx: UnboundedSender<RxPacket>,

        sends: RefCell<Vec<(*const u8, u16)>>,
        send_status: RefCell<VecDeque<zx::Status>>,

        completes: RefCell<Vec<(u32, zx::Status)>>,
    }

    impl GuestEthernetInterface for TestGuestEthernet {
        fn new() -> Result<GuestEthernetNewResult<TestGuestEthernet>, zx::Status> {
            let (status_tx, status_rx) = mpsc::unbounded::<zx::Status>();
            let (notify_tx, notify_rx) = mpsc::unbounded::<()>();
            let (receive_packet_tx, receive_packet_rx) = mpsc::unbounded::<RxPacket>();
            let guest_ethernet = Box::pin(Self {
                status_tx,
                notify_tx,
                receive_packet_tx,
                sends: RefCell::new(vec![]),
                send_status: RefCell::new(VecDeque::new()),
                completes: RefCell::new(vec![]),
            });

            Ok(GuestEthernetNewResult { guest_ethernet, status_rx, notify_rx, receive_packet_rx })
        }

        fn initialize(
            &self,
            _mac_address: MacAddress,
            _enable_bridge: bool,
        ) -> Result<(), zx::Status> {
            Ok(())
        }

        fn send(&self, data: *const u8, len: u16) -> Result<(), zx::Status> {
            let status = self.send_status.borrow_mut().pop_front().unwrap();
            if status == zx::Status::OK {
                self.sends.borrow_mut().push((data, len));
            }

            status.into()
        }

        fn complete(&self, packet: RxPacket, status: zx::Status) {
            self.completes.borrow_mut().push((packet.buffer_id, status));
        }
    }

    #[fuchsia::test]
    async fn transmit_device_status() {
        let mut device = NetDevice::<TestGuestEthernet>::new().unwrap();

        device.ethernet.status_tx.unbounded_send(zx::Status::OK).unwrap();
        assert!(device.ready().await.is_ok());

        device.ethernet.status_tx.unbounded_send(zx::Status::INTERNAL).unwrap();
        assert!(device.ready().await.is_err());

        device.ethernet.status_tx.unbounded_send(zx::Status::INTERNAL).unwrap();
        assert!(device.get_error_from_guest_ethernet().await.is_err());
    }

    #[fuchsia::test]
    #[should_panic(expected = "GuestEthernet shouldn't send ZX_OK once its ready")]

    async fn bad_device_status() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();

        // The C++ device should never send ZX_OK after its ready (which is when this channel
        // will be polled with get_error_from_guest_ethernet).
        device.ethernet.status_tx.unbounded_send(zx::Status::OK).unwrap();
        device.get_error_from_guest_ethernet().await.unwrap();
    }

    #[fuchsia::test]
    async fn rx_chain_too_small() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();
        let actual_size = wire::REQUIRED_RX_BUFFER_SIZE / 2;

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable((wire::REQUIRED_RX_BUFFER_SIZE / 2) as u32, &mem)
                    .build(),
            )
            .expect("failed to publish writable chain");

        let result = device
            .handle_writable_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .await;

        assert!(result.is_err());
        assert_eq!(
            result.unwrap_err().to_string(),
            format!(
                "Writable chain ({} bytes) is smaller than minimum size ({} bytes)",
                actual_size,
                wire::REQUIRED_RX_BUFFER_SIZE
            )
        );
    }

    #[fuchsia::test]
    async fn rx_chain_fragmented_multiple_descriptors() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable((wire::REQUIRED_RX_BUFFER_SIZE / 2) as u32, &mem)
                    .writable((wire::REQUIRED_RX_BUFFER_SIZE / 2) as u32, &mem)
                    .build(),
            )
            .expect("failed to publish writable chain");

        let result = device
            .handle_writable_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .await;

        assert!(result.is_err());
        assert_eq!(
            result.unwrap_err().to_string(),
            "RX buffer incorrectly fragmented over multiple descriptors"
        );
    }

    #[fuchsia::test]
    async fn tx_chain_too_small() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();

        let random_bytes: Vec<u8> = rand::thread_rng()
            .sample_iter(Standard)
            .take(std::mem::size_of::<wire::VirtioNetHeader>() - 1)
            .collect();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        // Chunk the array into three descriptors.
        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&random_bytes, &mem).build())
            .expect("failed to publish readable chain");

        let result = device
            .handle_readable_chain(ReadableChain::new(
                queue_state.queue.next_chain().expect("failed to get next chain"),
                &mem,
            ))
            .await;

        assert!(result.is_err());
        assert_eq!(result.unwrap_err().to_string(), "Chain does not contain a VirtioNetHeader");
    }

    #[fuchsia::test]
    async fn tx_chain_fragmented_multiple_descriptors() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();

        let random_bytes: Vec<u8> = rand::thread_rng()
            .sample_iter(Standard)
            .take(std::mem::size_of::<wire::VirtioNetHeader>() * 2)
            .collect();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        // Fragment the packet into two descriptors.
        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .readable(&random_bytes[..random_bytes.len() / 2], &mem)
                    .readable(&random_bytes[random_bytes.len() / 2..], &mem)
                    .build(),
            )
            .expect("failed to publish readable chain");

        let result = device
            .handle_readable_chain(ReadableChain::new(
                queue_state.queue.next_chain().expect("failed to get next chain"),
                &mem,
            ))
            .await;

        assert!(result.is_err());
        assert_eq!(
            result.unwrap_err().to_string(),
            "TX Packet incorrectly fragmented over multiple descriptors"
        );
    }

    #[fuchsia::test]
    async fn tx_packet_send_to_netstack() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();
        let data_length = 50;

        // Actual virtio-net header doesn't matter for TX, so it can be "included" in these
        // random bytes.
        let random_bytes: Vec<u8> = rand::thread_rng()
            .sample_iter(Standard)
            .take(std::mem::size_of::<wire::VirtioNetHeader>() + data_length)
            .collect();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&random_bytes, &mem).build())
            .expect("failed to publish readable chain");

        device.ethernet.send_status.borrow_mut().push_back(zx::Status::OK);

        device
            .handle_readable_chain(ReadableChain::new(
                queue_state.queue.next_chain().expect("failed to get next chain"),
                &mem,
            ))
            .await
            .expect("failed to process TX chain");

        assert_eq!(device.ethernet.sends.borrow().len(), 1);
        let (data_ptr, actual_len) = device.ethernet.sends.borrow()[0];
        assert_eq!(data_length, actual_len as usize);
        let slice = unsafe { std::slice::from_raw_parts(data_ptr, actual_len as usize) };
        assert_eq!(slice, &random_bytes[std::mem::size_of::<wire::VirtioNetHeader>()..]);
    }

    #[fuchsia::test]
    fn too_many_tx_packets_resumable() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();
        let header_length = std::mem::size_of::<wire::VirtioNetHeader>();
        let data_length = 50;
        let packet_length = header_length + data_length;

        let packet1: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(packet_length).collect();

        let packet2: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(packet_length).collect();

        let packet3: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(packet_length).collect();

        // First packet is accepted by the netstack.
        device.ethernet.send_status.borrow_mut().push_back(zx::Status::OK);

        // Second packet is asked to wait once, and is then accepted by the netstack.
        device.ethernet.send_status.borrow_mut().push_back(zx::Status::SHOULD_WAIT);
        device.ethernet.send_status.borrow_mut().push_back(zx::Status::OK);

        // Third packet is accepted by the netstack.
        device.ethernet.send_status.borrow_mut().push_back(zx::Status::OK);

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&packet1, &mem).build())
            .expect("failed to publish readable chain");

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&packet2, &mem).build())
            .expect("failed to publish readable chain");

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().readable(&packet3, &mem).build())
            .expect("failed to publish readable chain");

        let fut = device.handle_readable_chain(ReadableChain::new(
            queue_state.queue.next_chain().expect("failed to get next chain"),
            &mem,
        ));
        futures::pin_mut!(fut);
        executor.run_until_stalled(&mut fut).expect("future should have completed").unwrap();

        let fut = device.handle_readable_chain(ReadableChain::new(
            queue_state.queue.next_chain().expect("failed to get next chain"),
            &mem,
        ));
        futures::pin_mut!(fut);

        // "Netstack" returned SHOULD_WAIT, so this chain can't be completed.
        assert!(executor.run_until_stalled(&mut fut).is_pending());

        // Normally invoked by the netstack, this should resume processing the current TX chain.
        device.ethernet.notify_tx.unbounded_send(()).unwrap();
        executor.run_until_stalled(&mut fut).expect("future should have completed").unwrap();

        let fut = device.handle_readable_chain(ReadableChain::new(
            queue_state.queue.next_chain().expect("failed to get next chain"),
            &mem,
        ));
        futures::pin_mut!(fut);
        executor.run_until_stalled(&mut fut).expect("future should have completed").unwrap();

        assert_eq!(device.ethernet.sends.borrow().len(), 3);

        let (data_ptr, actual_len) = device.ethernet.sends.borrow()[0];
        assert_eq!(data_length, actual_len as usize);
        let slice = unsafe { std::slice::from_raw_parts(data_ptr, actual_len as usize) };
        assert_eq!(slice, &packet1[header_length..]);

        let (data_ptr, actual_len) = device.ethernet.sends.borrow()[1];
        assert_eq!(data_length, actual_len as usize);
        let slice = unsafe { std::slice::from_raw_parts(data_ptr, actual_len as usize) };
        assert_eq!(slice, &packet2[header_length..]);

        let (data_ptr, actual_len) = device.ethernet.sends.borrow()[2];
        assert_eq!(data_length, actual_len as usize);
        let slice = unsafe { std::slice::from_raw_parts(data_ptr, actual_len as usize) };
        assert_eq!(slice, &packet3[header_length..]);
    }

    #[fuchsia::test]
    async fn rx_packet_too_large_to_send_to_guest() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();

        // Smaller than an RX buffer, but not small enough to fit the virtio-net header.
        let data_length =
            wire::REQUIRED_RX_BUFFER_SIZE - std::mem::size_of::<wire::VirtioNetHeader>() / 2;
        let buffer_id = 54321;

        let packet_data: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(data_length).collect();
        let rx_packet =
            RxPacket { data: packet_data.as_ptr(), len: packet_data.len(), buffer_id: buffer_id };

        device.ethernet.receive_packet_tx.unbounded_send(rx_packet).unwrap();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new().writable((wire::REQUIRED_RX_BUFFER_SIZE) as u32, &mem).build(),
            )
            .expect("failed to publish writable chain");

        let result = device
            .handle_writable_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .await;

        assert!(result.is_err());
        assert_eq!(result.unwrap_err().to_string(), "Packet is too large for provided RX buffers");

        // Packet must still be completed even if not sent.
        assert_eq!(device.ethernet.completes.borrow().len(), 1);
        let (id, status) = device.ethernet.completes.borrow()[0];
        assert_eq!(id, buffer_id);
        assert_eq!(status, zx::Status::INTERNAL);
    }

    #[fuchsia::test]
    async fn rx_packet_send_to_guest() {
        let device = NetDevice::<TestGuestEthernet>::new().unwrap();
        let data_length = 50;
        let header_length = std::mem::size_of::<wire::VirtioNetHeader>();
        let buffer_id = 12345;

        // The packet received from the netstack does not include the virtio-net header.
        let packet_data: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(data_length).collect();
        let rx_packet =
            RxPacket { data: packet_data.as_ptr(), len: packet_data.len(), buffer_id: buffer_id };

        device.ethernet.receive_packet_tx.unbounded_send(rx_packet).unwrap();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new().writable((wire::REQUIRED_RX_BUFFER_SIZE) as u32, &mem).build(),
            )
            .expect("failed to publish writable chain");

        device
            .handle_writable_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .await
            .expect("failed to handle RX packet");

        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");
        let (data_ptr, len) = used_chain.data_iter().next().unwrap();

        assert_eq!(len, used_chain.written());
        assert_eq!(len as usize, data_length + header_length);

        let slice = unsafe { std::slice::from_raw_parts(data_ptr as *const u8, len as usize) };
        let header = wire::VirtioNetHeader::read_from(&slice[..header_length]).unwrap();
        assert_eq!(header.num_buffers.get(), 1);
        assert_eq!(&slice[header_length..], &packet_data);

        assert_eq!(device.ethernet.completes.borrow().len(), 1);
        let (id, status) = device.ethernet.completes.borrow()[0];
        assert_eq!(id, buffer_id);
        assert_eq!(status, zx::Status::OK);
    }
}
