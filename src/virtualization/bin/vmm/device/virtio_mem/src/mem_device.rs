// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect as inspect,
    fuchsia_zircon::{self as zx},
    virtio_device::chain::ReadableChain,
    virtio_device::mem::DriverMem,
    virtio_device::queue::DriverNotify,
};

pub trait MemBackend {
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

impl MemBackend for VmoMemoryBackend {
    fn decommit_range(&self, offset: u64, size: u64) -> Result<(), zx::Status> {
        self.vmo.op_range(zx::VmoOp::ZERO, offset, size)
    }
}

pub struct MemDevice<B: MemBackend> {
    _backend: B,
    _plugged_size_bytes: inspect::IntProperty,
    _plugged_block_size: u64,
    _plugged_region_size: u64,
}

impl<B: MemBackend> MemDevice<B> {
    pub fn new(
        backend: B,
        inspect_node: &inspect::Node,
        plugged_block_size: u64,
        plugged_region_size: u64,
    ) -> Self {
        let plugged_size_bytes = inspect_node.create_int("plugged_size_bytes", 0);
        Self {
            _backend: backend,
            _plugged_size_bytes: plugged_size_bytes,
            _plugged_block_size: plugged_block_size,
            _plugged_region_size: plugged_region_size,
        }
    }

    pub fn process_guest_request_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
    ) {
        tracing::info!("process_guest_request_chain remaining={:?}", chain.remaining());
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_inspect::Inspector,
        std::cell::RefCell,
        std::ops::Range,
        virtio_device::chain::ReadableChain,
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
    };

    pub struct TestMemBackend {
        inner: VmoMemoryBackend,
        calls: RefCell<Vec<Range<u64>>>,
    }

    impl TestMemBackend {
        pub fn new(inner: VmoMemoryBackend) -> Self {
            Self { inner, calls: RefCell::new(Vec::new()) }
        }
    }

    impl MemBackend for TestMemBackend {
        fn decommit_range(&self, offset: u64, size: u64) -> Result<(), zx::Status> {
            self.calls.borrow_mut().push(offset..offset + size);
            self.inner.decommit_range(offset, size)
        }
    }

    #[fuchsia::test]
    fn test_placeholder() {
        let plugged_block_size = 1 * 1024 * 1024;
        let plugged_region_size = 10 * 1024 * 1024 * 1024;
        let inspector = Inspector::new();
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        let test_input: [u32; 4] = [1, 2, 3, 4];
        const VMO_SIZE: u64 = 16 * 4096 as u64;
        state.fake_queue.publish(ChainBuilder::new().readable(&test_input, &mem).build()).unwrap();
        let vmo = zx::Vmo::create(VMO_SIZE).unwrap();
        // Process the chain.
        let device = MemDevice::new(
            TestMemBackend::new(VmoMemoryBackend::new(vmo)),
            &inspector.root(),
            plugged_block_size,
            plugged_region_size,
        );
        // Process the request.
        device.process_guest_request_chain(ReadableChain::new(
            state.queue.next_chain().unwrap(),
            &mem,
        ));
    }
}
