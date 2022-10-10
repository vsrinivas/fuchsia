// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{AsyncReadExt, AsyncWriteExt, StreamExt},
    machina_virtio_device::{GuestMem, WrappedDescChainStream},
    virtio_device::{
        chain::{ReadableChain, WritableChain},
        mem::DriverMem,
        queue::DriverNotify,
    },
};

pub struct ConsoleDevice {
    // Guest end of a socket provided by the controller.
    socket: fasync::Socket,
}

impl ConsoleDevice {
    pub fn new(socket: zx::Socket) -> Result<Self, Error> {
        let socket = fasync::Socket::from_socket(socket)?;
        Ok(Self { socket })
    }

    // Handles the TX queue stream, pulling readable chains off of the stream sequentially and
    // writing them to the console socket. This should only be invoked once, and will return when
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
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let mut socket = &self.socket;
        while let Some(range) = chain
            .next()
            .transpose()
            .map_err(|err| anyhow!("Failed to iterate over chain: {}", err))?
        {
            // A note on safety:
            //   * No references (mutable or unmutable) to this range are held elsewhere. Other
            //     pointers may exist but will not be dereferenced while this slice is held.
            //   * This is a u8 pointer which has no alignment constraints.
            let slice =
                unsafe { std::slice::from_raw_parts(range.try_ptr().unwrap(), range.len()) };
            socket.write_all(slice).await?;
        }

        chain.return_complete().map_err(|err| anyhow!("Failed to complete chain: {}", err))
    }

    // Handles the RX queue stream, pulling writable chains off of the stream sequentially. When
    // a writable chain is available and there is data on the console socket, data will be written
    // to the chain. The data is not buffered -- the chain will be returned when the socket is
    // empty regardless of how much data has been written.
    //
    // This should only be invoked once, and will return when the stream is closed.
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
        mut chain: WritableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let mut socket = &self.socket;
        let mut idx = 0;
        while let Some(range) = chain
            .next()
            .transpose()
            .map_err(|err| anyhow!("Failed to iterate over chain: {}", err))?
        {
            // A note on safety:
            //   * No references (mutable or unmutable) to this range are held elsewhere. Other
            //     pointers may exist but will not be dereferenced while this slice is held.
            //   * This is a u8 pointer which has no alignment constraints.
            let slice = unsafe {
                std::slice::from_raw_parts_mut(range.try_mut_ptr().unwrap(), range.len())
            };

            let count = if idx == 0 {
                // Block until any data is available to write to the chain.
                let count = socket.read(slice).await?;
                if count == 0 {
                    Err(anyhow!("Socket is closed"))
                } else {
                    Ok(count)
                }
            } else {
                // As soon as data is on the chain, synchronously fill any remaining descriptors
                // with any remaining data.
                socket.as_ref().read(slice).or(Ok(0))
            }?;

            // After the first descriptor, subsequent descriptors may contain no data.
            if count != 0 {
                chain.add_written(count.try_into()?);
            }

            // If this descriptor wasn't fully filled, there's no more data available on the socket
            // without waiting so the chain must be returned to the guest.
            if count != range.len() {
                break;
            }

            idx += 1;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_utils::PollExt,
        futures::{AsyncReadExt, FutureExt},
        rand::{distributions::Standard, Rng},
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
    };

    #[test]
    fn tx_blocked_on_full_socket() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (remote, local) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");

        let max_socket_bytes = local.info().unwrap().tx_buf_max;
        let total_bytes_to_send = max_socket_bytes + 128;
        let device = ConsoleDevice::new(local).expect("failed to create console device");

        let random_bytes: Vec<u8> =
            rand::thread_rng().sample_iter(Standard).take(total_bytes_to_send).collect();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        // Chunk the array into three descriptors.
        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .readable(&random_bytes[..random_bytes.len() / 4], &mem)
                    .readable(&random_bytes[random_bytes.len() / 4..random_bytes.len() / 2], &mem)
                    .readable(&random_bytes[random_bytes.len() / 2..], &mem)
                    .build(),
            )
            .expect("failed to publish readable chain");

        let tx_future = device.handle_readable_chain(ReadableChain::new(
            queue_state.queue.next_chain().expect("failed to get next chain"),
            &mem,
        ));
        futures::pin_mut!(tx_future);

        // This will pend as the socket buffer is now full.
        assert!(executor.run_until_stalled(&mut tx_future).is_pending());
        assert_eq!(remote.outstanding_read_bytes().unwrap(), max_socket_bytes);

        let mut actual_bytes = vec![0u8; random_bytes.len()];
        let mut remote = fasync::Socket::from_socket(remote).unwrap();

        let rx_future = remote.read_exact(&mut actual_bytes);
        futures::pin_mut!(rx_future);

        // Read all available bytes, which are still fewer than the expected.
        assert!(executor.run_until_stalled(&mut rx_future).is_pending());

        // Write the remaining bytes.
        executor.run_until_stalled(&mut tx_future).expect("future should have completed").unwrap();

        // Read the remaining bytes.
        executor.run_until_stalled(&mut rx_future).expect("future should have completed").unwrap();

        assert_eq!(actual_bytes, random_bytes);
    }

    #[fuchsia::test]
    async fn rx_chain_returns_any_data_available() {
        let (remote, local) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let mut remote = fasync::Socket::from_socket(remote).unwrap();
        let device = ConsoleDevice::new(local).expect("failed to create console device");

        let random_bytes: Vec<u8> = rand::thread_rng().sample_iter(Standard).take(96).collect();
        remote
            .write_all(&random_bytes)
            .now_or_never()
            .expect("future should have completed")
            .unwrap();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable(32, &mem)
                    .writable(64, &mem)
                    .writable(64, &mem)
                    .build(),
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
            .unwrap();

        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");

        // Coalesce the returned chain into one contiguous buffer.
        let mut result = Vec::new();
        let mut iter = used_chain.data_iter();
        while let Some((data, len)) = iter.next() {
            let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };
            result.extend_from_slice(slice);
        }
        assert_eq!(result.len(), usize::try_from(used_chain.written()).unwrap());
        assert_eq!(result, random_bytes);
    }

    #[fuchsia::test]
    async fn rx_chain_returns_data_from_closed_socket() {
        let (remote, local) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let mut remote = fasync::Socket::from_socket(remote).unwrap();
        let device = ConsoleDevice::new(local).expect("failed to create console device");

        let random_bytes: Vec<u8> = rand::thread_rng().sample_iter(Standard).take(128).collect();
        remote
            .write_all(&random_bytes)
            .now_or_never()
            .expect("future should have completed")
            .unwrap();
        drop(remote);

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    .writable(32, &mem)
                    .writable(64, &mem)
                    .writable(64, &mem)
                    .build(),
            )
            .expect("failed to publish writable chain");

        // The remote end of the socket is closed, but there are bytes remaining to be transmitted
        // to the guest.
        device
            .handle_writable_chain(
                WritableChain::new(
                    queue_state.queue.next_chain().expect("failed to get next chain"),
                    &mem,
                )
                .expect("failed to get writable chain"),
            )
            .await
            .unwrap();

        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");

        // Coalesce the returned chain into one contiguous buffer.
        let mut result = Vec::new();
        let mut iter = used_chain.data_iter();
        while let Some((data, len)) = iter.next() {
            let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };
            result.extend_from_slice(slice);
        }
        assert_eq!(result.len(), usize::try_from(used_chain.written()).unwrap());
        assert_eq!(result, random_bytes);

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().writable(64, &mem).build())
            .expect("failed to publish writable chain");

        // The socket is closed with no bytes remaining so this future returns error.
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
    }

    #[fuchsia::test]
    async fn rx_more_bytes_on_socket_than_chain_size() {
        let (remote, local) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("failed to create socket");
        let mut remote = fasync::Socket::from_socket(remote).unwrap();
        let device = ConsoleDevice::new(local).expect("failed to create console device");

        let random_bytes: Vec<u8> = rand::thread_rng().sample_iter(Standard).take(64).collect();
        remote
            .write_all(&random_bytes)
            .now_or_never()
            .expect("future should have completed")
            .unwrap();

        let mem = IdentityDriverMem::new();
        let mut queue_state = TestQueue::new(32, &mem);

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().writable(32, &mem).build())
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
            .unwrap();

        // First half of the data.
        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");
        assert_eq!(used_chain.written(), 32);
        let (data, len) =
            used_chain.data_iter().next().expect("there should be one filled descriptor");
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };
        assert_eq!(slice, &random_bytes[..32]);

        queue_state
            .fake_queue
            .publish(ChainBuilder::new().writable(64, &mem).build())
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
            .unwrap();

        // Second half of the data.
        let used_chain = queue_state.fake_queue.next_used().expect("no next used chain");
        assert_eq!(used_chain.written(), 32);
        let (data, len) =
            used_chain.data_iter().next().expect("there should be one filled descriptor");
        let slice = unsafe { std::slice::from_raw_parts(data as *const u8, len as usize) };
        assert_eq!(slice, &random_bytes[32..]);
    }
}
