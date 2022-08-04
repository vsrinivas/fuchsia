// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire::{VirtioBalloonFeatureFlags, VirtioBalloonMemStat, LE32, PAGE_SIZE},
    anyhow::{anyhow, Error},
    fidl_fuchsia_virtualization::MemStat,
    fidl_fuchsia_virtualization_hardware::VirtioBalloonGetMemStatsResponder,
    fidl_fuchsia_virtualization_hardware::{VirtioBalloonRequest, VirtioBalloonRequestStream},
    fuchsia_zircon::{self as zx},
    futures::channel::mpsc,
    futures::{StreamExt, TryStreamExt},
    machina_virtio_device::Device,
    machina_virtio_device::WrappedDescChainStream,
    std::io::Read,
    virtio_device::chain::ReadableChain,
    virtio_device::mem::DriverMem,
    virtio_device::queue::DriverNotify,
    zerocopy::FromBytes,
};

fn read_pfn<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: &mut ReadableChain<'a, 'b, N, M>,
) -> Option<u64> {
    let mut arr = [0u8; std::mem::size_of::<LE32>()];
    if chain.read_exact(&mut arr).is_ok() {
        Some(u64::from(LE32::from_bytes(arr)))
    } else {
        None
    }
}

/// Reads a `wire::VirtioBalloonMemStat` from the chain.
fn read_mem_stat<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: &mut ReadableChain<'a, 'b, N, M>,
) -> Option<VirtioBalloonMemStat> {
    let mut arr = [0; std::mem::size_of::<VirtioBalloonMemStat>()];
    if chain.read_exact(&mut arr).is_ok() {
        VirtioBalloonMemStat::read_from(arr.as_slice())
    } else {
        None
    }
}

pub struct BalloonDevice {
    vmo: zx::Vmo,
}

impl BalloonDevice {
    pub fn new(vmo: zx::Vmo) -> Self {
        Self { vmo }
    }

    pub fn process_deflate_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        while let Some(pfn) = read_pfn(&mut chain) {
            tracing::trace!("deflate pfn {}", pfn);
        }
        Ok(())
    }

    pub fn process_inflate_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let mut base = 0;
        let mut run = 0;
        while let Some(pfn) = read_pfn(&mut chain) {
            // If the driver writes contiguous PFNs, we will combine them into runs.
            if run > 0 {
                // If this is part of the current run, extend it.
                if pfn == base + run {
                    run += 1;
                    continue;
                }
                // We have completed a run, so process it before starting a new run.
                tracing::trace!("inflate pfn range base={}, size={}", base, run);
                self.vmo.op_range(
                    zx::VmoOp::ZERO,
                    base * PAGE_SIZE as u64,
                    run * PAGE_SIZE as u64,
                )?;
            }
            base = pfn;
            run = 1;
        }
        if run > 0 {
            // Process the final run.
            tracing::trace!("inflate pfn range base={}, size={}", base, run);
            self.vmo.op_range(zx::VmoOp::ZERO, base * PAGE_SIZE as u64, run * PAGE_SIZE as u64)?;
        }
        Ok(())
    }

    pub fn process_stats_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        chain: &mut ReadableChain<'a, 'b, N, M>,
    ) -> Vec<MemStat> {
        let mut mem_stats = Vec::new();
        while let Some(mem_stat) = read_mem_stat(chain) {
            mem_stats.push(MemStat { tag: u16::from(mem_stat.tag), val: u64::from(mem_stat.val) });
        }
        mem_stats
    }

    pub async fn run_mem_stats_receiver<'a, 'b, N: DriverNotify, M: DriverMem>(
        mut stats_stream: WrappedDescChainStream<'a, 'b, N>,
        guest_mem: &'a M,
        features: &VirtioBalloonFeatureFlags,
        mut receiver: mpsc::Receiver<VirtioBalloonGetMemStatsResponder>,
    ) -> Result<(), Error> {
        let mut prev_stat_chain = None;
        while let Some(responder) = receiver.next().await {
            if features.contains(VirtioBalloonFeatureFlags::VIRTIO_BALLOON_F_STATS_VQ) {
                // See 5.5.6.3
                // The stats virtqueue is atypical because communication is driven by the device
                // (not the driver).
                // The channel becomes active at driver initialization time when the driver adds an
                // empty buffer and notifies the device. A request for memory statistics proceeds
                // as follows:
                // 1. The device uses the buffer and sends a used buffer notification.
                // 2. The driver pops the used buffer and discards it.
                // 3. The driver collects memory statistics and writes them into a new buffer.
                // 4. The driver adds the buffer to the virtqueue and notifies the device.
                // 5. The device pops the buffer (retaining it to initiate a subsequent request) and
                //    consumes the statistics.
                //
                // Here are we are popping the previously retained buffer to signal the driver.
                // If there is no previously retained buffer we'll pop an empty buffer of the chain.
                // See 5.5.6.3
                // The channel becomes active at driver initialization time when the driver
                // adds an empty buffer and notifies the device.
                // In both cases driver will get notified and perform steps 3 and 4
                match prev_stat_chain {
                    Some(chain) => drop(chain),
                    None => drop(stats_stream.next().await),
                };
                // Fetch the memory stats which driver prepared for us
                let mut stat_chain =
                    ReadableChain::new(stats_stream.next().await.unwrap(), guest_mem);
                let mut mem_stats = Self::process_stats_chain(&mut stat_chain);
                responder.send(zx::sys::ZX_OK, Some(&mut mem_stats.iter_mut()))?;
                // Retain memory stats, so we can drop it on the next request to signal the driver
                // to get the new mem stats
                prev_stat_chain = Some(stat_chain);
            } else {
                // If memory statistics are not supported, return.
                responder.send(zx::sys::ZX_ERR_NOT_SUPPORTED, None)?;
            }
        }
        Ok(())
    }

    pub async fn run_virtio_balloon_stream<'a, 'b, N: DriverNotify>(
        virtio_balloon_fidl: VirtioBalloonRequestStream,
        device: &Device<'a, N>,
        sender: mpsc::Sender<VirtioBalloonGetMemStatsResponder>,
    ) -> Result<(), Error> {
        virtio_balloon_fidl
            .err_into()
            .try_for_each(|msg| {
                futures::future::ready(match msg {
                    VirtioBalloonRequest::NotifyQueue { queue, .. } => {
                        device.notify_queue(queue).map_err(|e| anyhow!("NotifyQueue: {}", e))
                    }
                    VirtioBalloonRequest::GetMemStats { responder } => sender
                        .clone()
                        .try_send(responder)
                        .map_err(|e| anyhow!("GetMemStats: {}", e)),
                    msg => Err(anyhow!("Unexpected message: {:?}", msg)),
                })
            })
            .await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::wire::{LE16, LE64},
        virtio_device::chain::ReadableChain,
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
    };

    #[fuchsia::test]
    fn test_deflate_command() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        let pfns: [u32; 4] = [3, 2, 1, 0];
        const VMO_SIZE: u64 = 16 * PAGE_SIZE as u64;
        state.fake_queue.publish(ChainBuilder::new().readable(&pfns, &mem).build()).unwrap();

        // Process the chain.
        let vmo = zx::Vmo::create(VMO_SIZE).unwrap();
        let device = BalloonDevice::new(vmo);

        // Process the request.
        device
            .process_deflate_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .expect("Failed to process display info command");
    }

    #[fuchsia::test]
    fn test_inflate_command() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        let pfns: [u32; 6] = [4, 5, 8, 9, 11, 7];
        const VMO_SIZE: u64 = 16 * PAGE_SIZE as u64;
        state.fake_queue.publish(ChainBuilder::new().readable(&pfns, &mem).build()).unwrap();

        // Fill vmo with 1s and expect decommitted pages to be filled with 0s after
        // the call to process_inflate_chain
        let vmo = zx::Vmo::create(VMO_SIZE).unwrap();
        let ones: [u8; PAGE_SIZE] = [1u8; PAGE_SIZE];
        for i in 0..(VMO_SIZE / PAGE_SIZE as u64) {
            vmo.write(&ones, i * PAGE_SIZE as u64).unwrap();
        }
        // Process the chain.
        let device = BalloonDevice::new(vmo);

        let prev_commited_bytes = device.vmo.info().unwrap().committed_bytes;
        // Process the request.
        device
            .process_inflate_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .expect("Failed to process display info command");

        let cur_commited_bytes = device.vmo.info().unwrap().committed_bytes;
        assert_eq!(
            (prev_commited_bytes - cur_commited_bytes) / PAGE_SIZE as u64,
            pfns.len() as u64
        );

        for i in 0..(VMO_SIZE / PAGE_SIZE as u64) {
            let mut arr: [u8; PAGE_SIZE] = [2u8; PAGE_SIZE];
            device.vmo.read(&mut arr, i * PAGE_SIZE as u64).unwrap();
            let expected =
                if pfns.contains(&(i as u32)) { [0u8; PAGE_SIZE] } else { [1u8; PAGE_SIZE] };
            assert_eq!(expected, arr);
        }
    }

    #[fuchsia::test]
    fn test_inflate_command_out_of_bounds_pfn() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        // 18 here is out of bounds of passed VMO and expected to trigger an error
        let pfns: [u32; 3] = [2, 18, 1];
        const VMO_SIZE: u64 = 16 * PAGE_SIZE as u64;
        state.fake_queue.publish(ChainBuilder::new().readable(&pfns, &mem).build()).unwrap();
        let vmo = zx::Vmo::create(VMO_SIZE).unwrap();
        // Process the chain.
        let device = BalloonDevice::new(vmo);
        // Process the request.
        assert!(device
            .process_inflate_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .is_err());
    }

    #[fuchsia::test]
    fn test_parse_mem_stats_chain() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        let mem_stats: [VirtioBalloonMemStat; 3] = [
            VirtioBalloonMemStat { tag: LE16::from(1), val: LE64::from(11) },
            VirtioBalloonMemStat { tag: LE16::from(2), val: LE64::from(22) },
            VirtioBalloonMemStat { tag: LE16::from(3), val: LE64::from(33) },
        ];
        state.fake_queue.publish(ChainBuilder::new().readable(&mem_stats, &mem).build()).unwrap();
        let mem_stats = BalloonDevice::process_stats_chain(&mut ReadableChain::new(
            state.queue.next_chain().unwrap(),
            &mem,
        ));
        assert_eq!(
            mem_stats,
            vec![
                MemStat { tag: mem_stats[0].tag, val: mem_stats[0].val },
                MemStat { tag: mem_stats[1].tag, val: mem_stats[1].val },
                MemStat { tag: mem_stats[2].tag, val: mem_stats[2].val }
            ]
        );
    }
}
