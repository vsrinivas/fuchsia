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
    std::ops::Range,
    virtio_device::chain::{ReadableChain, WritableChain},
    virtio_device::mem::{DeviceRange, DriverMem, DriverRange},
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

pub trait BalloonBackend {
    fn decommit_range(&self, offset: u64, size: u64) -> Result<(), zx::Status>;
}

pub struct VmoMemoryBackend {
    vmo: zx::Vmo,
}

impl VmoMemoryBackend {
    pub fn new(vmo: zx::Vmo) -> Self {
        Self { vmo }
    }
}

impl BalloonBackend for VmoMemoryBackend {
    fn decommit_range(&self, offset: u64, size: u64) -> Result<(), zx::Status> {
        self.vmo.op_range(zx::VmoOp::ZERO, offset, size)
    }
}

pub struct BalloonDevice<B: BalloonBackend> {
    backend: B,
}

impl<B: BalloonBackend> BalloonDevice<B> {
    pub fn new(backend: B) -> Self {
        Self { backend }
    }

    pub fn process_deflate_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
    ) {
        match chain.remaining() {
            Ok(remaining) => {
                // each PFN is LE32, so we divide the amount of memory in read chain by 4
                tracing::trace!("Deflated {} KiB", remaining.bytes / 4 * PAGE_SIZE / 1024);
            }
            Err(_) => {}
        }
    }

    pub fn process_inflate_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        // each PFN is LE32, so we divide the amount of memory in read chain by 4
        let inflated_amount = chain.remaining()?.bytes / 4 * PAGE_SIZE;
        let mut base = 0;
        let mut run = 0;
        // Normally Linux driver will send out descending list of PFNs
        // However, occasionally we get ascending list of PFNs
        // So we'll support runs in both directions
        let mut dir: i64 = 1;
        while let Some(pfn) = read_pfn(&mut chain) {
            // If the driver writes contiguous PFNs, we will combine them into runs.
            if run > 0 {
                if run == 1 && pfn == base - run {
                    // Detected descending run
                    // Flip the run direction
                    dir = -1;
                }
                if pfn as i64 == base as i64 + run as i64 * dir {
                    // If this is part of the current run, extend it.
                    run += 1;
                    continue;
                }
                // We have completed a run, so process it before starting a new run.
                if dir == -1 {
                    assert!(run > 1);
                    // For the descending run we want to offset the base position
                    base -= run - 1;
                }
                tracing::trace!("inflate pfn range base={}, size={} dir={}", base, run, dir);
                self.backend.decommit_range(base * PAGE_SIZE as u64, run * PAGE_SIZE as u64)?;
            }
            base = pfn;
            run = 1;
            dir = 1;
        }
        if run > 0 {
            // Process the final run.
            if dir == -1 {
                // For the descending run we want to offset the base position
                base -= run - 1;
            }
            tracing::trace!("inflate pfn range base={}, size={} dir={}", base, run, dir);
            self.backend.decommit_range(base * PAGE_SIZE as u64, run * PAGE_SIZE as u64)?;
        }
        tracing::trace!("Inflated {} KiB", inflated_amount / 1024);
        Ok(())
    }

    pub fn process_free_page_report_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: WritableChain<'a, 'b, N, M>,
        vmo_range: Range<usize>,
    ) -> Result<(), Error> {
        fn untranslate<'a>(
            range: DeviceRange<'a>,
            vmo_range: &Range<usize>,
        ) -> Result<DriverRange, Error> {
            if range.get().start >= vmo_range.start && range.get().end <= vmo_range.end {
                Ok(DriverRange(
                    range.get().start - vmo_range.start..range.get().end - vmo_range.start,
                ))
            } else {
                Err(anyhow!("Failed to untranslate range={:?} vmo_mapping={:?}", range, vmo_range))
            }
        }
        // Here is an excerpt from the virtio spec
        // 5.5.6.7 Free Page Reporting
        // Free Page Reporting provides a mechanism similar to balloon
        // inflation, however it does not provide a deflation queue. Reported
        // free pages can be reused by the driver after the reporting request
        // has been acknowledged without notifying the device. The driver will
        // begin reporting free pages. When exactly and which free pages are
        // reported is up to the driver.

        // 1. The driver determines it has enough pages available to begin
        // reporting free pages.
        // 2. The driver gathers free pages into a scatter-gather list and adds
        // them to the reporting_vq.
        // 3. The device acknowledges the reporting request by using the
        // reporting_vq descriptor.
        //
        // NB: Scatter-gather list contains descriptors which directly refer to
        // ranges that are to be free'd
        //
        // Spec is not clear on this, but it doesn't specify the data format
        // descriptors refer to so this is somewhat implied. I confirmed this by
        // running experiments on debian and termina. Behaviour is consistent
        // across linux kernel 5.10 and 5.15
        let mut total_len = 0;
        while let Some(range) = chain.next().transpose()? {
            let range = untranslate(range, &vmo_range)?;
            total_len += range.len();
            self.backend.decommit_range(range.0.start as u64, range.len() as u64)?;
        }
        tracing::debug!("Reclaimed {} MiB", total_len / 1024 / 1024);
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
    use std::cell::RefCell;

    use {
        super::*,
        crate::wire::{LE16, LE64},
        std::ops::Range,
        virtio_device::chain::{ReadableChain, WritableChain},
        virtio_device::fake_queue::{Chain, ChainBuilder, IdentityDriverMem, TestQueue},
        virtio_device::ring::DescAccess,
    };

    pub struct TestBalloonBackend {
        inner: Option<VmoMemoryBackend>,
        calls: RefCell<Vec<Range<u64>>>,
    }

    impl TestBalloonBackend {
        pub fn new(inner: Option<VmoMemoryBackend>) -> Self {
            Self { inner, calls: RefCell::new(Vec::new()) }
        }
    }

    impl BalloonBackend for TestBalloonBackend {
        fn decommit_range(&self, offset: u64, size: u64) -> Result<(), zx::Status> {
            self.calls.borrow_mut().push(offset..offset + size);
            if let Some(inner) = &self.inner {
                inner.decommit_range(offset, size)
            } else {
                Ok(())
            }
        }
    }

    fn inflate_and_validate(pfns: &[u32], expected_calls: &[Range<u64>]) {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        let vmo_size = (*pfns.iter().max().unwrap() as u64 + 1) * PAGE_SIZE as u64;
        state.fake_queue.publish(ChainBuilder::new().readable(&pfns, &mem).build()).unwrap();

        // Fill vmo with 1s and expect decommitted pages to be filled with 0s after
        // the call to process_inflate_chain
        let vmo = zx::Vmo::create(vmo_size).unwrap();
        let ones: [u8; PAGE_SIZE] = [1u8; PAGE_SIZE];
        for i in 0..(vmo_size / PAGE_SIZE as u64) {
            vmo.write(&ones, i * PAGE_SIZE as u64).unwrap();
        }
        // Process the chain.
        let device = BalloonDevice::new(TestBalloonBackend::new(Some(VmoMemoryBackend::new(vmo))));

        let prev_commited_bytes =
            device.backend.inner.as_ref().unwrap().vmo.info().unwrap().committed_bytes;
        // Process the request.
        device
            .process_inflate_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .expect("Failed to process inflate chain");

        let cur_commited_bytes =
            device.backend.inner.as_ref().unwrap().vmo.info().unwrap().committed_bytes;
        assert_eq!(
            (prev_commited_bytes - cur_commited_bytes) / PAGE_SIZE as u64,
            pfns.len() as u64
        );

        for i in 0..(vmo_size / PAGE_SIZE as u64) {
            let mut arr: [u8; PAGE_SIZE] = [2u8; PAGE_SIZE];
            device
                .backend
                .inner
                .as_ref()
                .unwrap()
                .vmo
                .read(&mut arr, i * PAGE_SIZE as u64)
                .unwrap();
            let expected =
                if pfns.contains(&(i as u32)) { [0u8; PAGE_SIZE] } else { [1u8; PAGE_SIZE] };
            assert_eq!(expected, arr);
        }

        let expected_calls: Vec<Range<u64>> = expected_calls
            .iter()
            .map(|x| x.start * PAGE_SIZE as u64..x.end * PAGE_SIZE as u64)
            .collect();
        assert_eq!(device.backend.calls.into_inner(), expected_calls);
    }

    #[fuchsia::test]
    fn test_deflate_command() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        let pfns: [u32; 4] = [3, 2, 1, 0];
        const VMO_SIZE: u64 = 16 * PAGE_SIZE as u64;
        state.fake_queue.publish(ChainBuilder::new().readable(&pfns, &mem).build()).unwrap();

        // Process the chain.
        let vmo = zx::Vmo::create(VMO_SIZE).unwrap();
        let device = BalloonDevice::new(TestBalloonBackend::new(Some(VmoMemoryBackend::new(vmo))));

        // Process the request.
        device.process_deflate_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem));
    }

    #[fuchsia::test]
    fn test_inflate_command_mix_ascending_descending() {
        inflate_and_validate(
            &[4, 5, 8, 9, 11, 7, 14, 13, 12, 1, 17, 16],
            &[4..6, 8..10, 11..12, 7..8, 12..15, 1..2, 16..18],
        );
    }

    #[fuchsia::test]
    fn test_inflate_command_ascending_sequence() {
        inflate_and_validate(&[1, 2, 3, 4, 7, 8, 11, 12, 13], &[1..5, 7..9, 11..14]);
    }

    #[fuchsia::test]
    fn test_inflate_command_descending_sequence() {
        inflate_and_validate(&[3, 2, 1, 5, 12, 11, 10], &[1..4, 5..6, 10..13]);
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
        let device = BalloonDevice::new(TestBalloonBackend::new(Some(VmoMemoryBackend::new(vmo))));
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
        let mem_stats = BalloonDevice::<TestBalloonBackend>::process_stats_chain(
            &mut ReadableChain::new(state.queue.next_chain().unwrap(), &mem),
        );
        assert_eq!(
            mem_stats,
            vec![
                MemStat { tag: mem_stats[0].tag, val: mem_stats[0].val },
                MemStat { tag: mem_stats[1].tag, val: mem_stats[1].val },
                MemStat { tag: mem_stats[2].tag, val: mem_stats[2].val }
            ]
        );
    }

    #[fuchsia::test]
    fn test_free_page_reporting() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        let device = BalloonDevice::new(TestBalloonBackend::new(None));

        let range0 = mem.new_range(512 * PAGE_SIZE).unwrap();
        let range1 = mem.new_range(1024 * PAGE_SIZE).unwrap();

        assert!(state
            .fake_queue
            .publish_indirect(
                Chain::with_exact_data(&[
                    (DescAccess::DeviceWrite, range0.get().start as u64, range0.len() as u32),
                    (DescAccess::DeviceWrite, range1.get().start as u64, range1.len() as u32)
                ]),
                &mem
            )
            .is_some());

        // Process the request.
        let fake_vmo_range = 1024..usize::max_value();
        device
            .process_free_page_report_chain(
                WritableChain::new(state.queue.next_chain().unwrap(), &mem)
                    .expect("Writable chain must be available"),
                fake_vmo_range.clone(),
            )
            .expect("Failed to process free page report chain");

        assert_eq!(
            device.backend.calls.into_inner(),
            &[
                (range0.get().start as u64 - fake_vmo_range.start as u64
                    ..range0.get().end as u64 - fake_vmo_range.start as u64),
                (range1.get().start as u64 - fake_vmo_range.start as u64
                    ..range1.get().end as u64 - fake_vmo_range.start as u64),
            ]
        );
    }
}
