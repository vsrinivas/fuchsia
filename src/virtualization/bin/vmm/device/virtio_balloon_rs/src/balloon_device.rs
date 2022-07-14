// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire::{LE32, PAGE_SIZE},
    anyhow::Error,
    fuchsia_zircon::{self as zx},
    std::io::Read,
    virtio_device::chain::ReadableChain,
    virtio_device::mem::DriverMem,
    virtio_device::queue::DriverNotify,
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
}

#[cfg(test)]
mod tests {
    use {
        super::*,
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
}
