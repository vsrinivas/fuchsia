// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resource::Resource2D,
    crate::wire,
    anyhow::{anyhow, Context, Error},
    futures::StreamExt,
    machina_virtio_device::WrappedDescChainStream,
    std::collections::{hash_map::Entry, HashMap},
    std::io::{Read, Write},
    tracing,
    virtio_device::chain::{ReadableChain, WritableChain},
    virtio_device::mem::{DriverMem, DriverRange},
    virtio_device::queue::DriverNotify,
    zerocopy::{AsBytes, FromBytes},
};

const VIRTIO_GPU_STARTUP_HEIGHT: u32 = 720;
const VIRTIO_GPU_STARTUP_WIDTH: u32 = 1280;
/// This is a (somewhat arbitrary) upper bound to the number of entries in a
/// VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING command.
const ATTACH_BACKING_MAX_ENTRIES: u32 = 1024;

/// Consumes bytes equal to the size of `message` and writes `message` to the chain.
///
/// Will fail if there is an error walking the chain, or if there is insufficient space left in
/// `chain`.
fn write_to_chain<'a, 'b, N: DriverNotify, M: DriverMem, T: AsBytes>(
    mut chain: WritableChain<'a, 'b, N, M>,
    message: T,
) -> Result<(), Error> {
    let size = chain.remaining()?;
    if size < std::mem::size_of::<wire::VirtioGpuCtrlHeader>() {
        return Err(anyhow!("Insufficient wriable space to write message to the chain"));
    }
    // unwrap here because since we already checked if there is space in the writable chain
    // we do not expect this to fail.
    chain.write_all(message.as_bytes()).unwrap();
    Ok(())
}

/// Writes a response to the chain with the given `wire::VirtioGpuError`.
fn write_error_to_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: ReadableChain<'a, 'b, N, M>,
    error_code: wire::VirtioGpuError,
) -> Result<(), Error> {
    // Since this is an error, assume it's possible some bytes may not have been read from the
    // readable portion of the chain.
    let chain =
        WritableChain::from_incomplete_readable(chain).context("Failed to get a writable chain")?;
    let response = wire::VirtioGpuCtrlHeader { ty: error_code.to_wire(), ..Default::default() };
    write_to_chain(chain, response).with_context(|| format!("Failed to write error {}", error_code))
}

/// Reads a `FromBytes` type from the chain. Fails if there are insufficient bytes remaining in the
/// chain to fully read the the value.
fn read_from_chain<'a, 'b, N: DriverNotify, M: DriverMem, T: FromBytes>(
    chain: &mut ReadableChain<'a, 'b, N, M>,
) -> Result<T, anyhow::Error> {
    // Ideally we'd just use an array here instead of doing this short-lived heap allocation.
    // Unfortunately that is not currently possible with rust.
    //
    // See https://github.com/rust-lang/rust/issues/43408 for more details. If and when the rust
    // compiler will accept an array here we can remove this allocation.
    let mut buffer = vec![0u8; std::mem::size_of::<T>()];
    chain.read_exact(&mut buffer)?;
    // read_from should not fail since we've sized the buffer appropriately. Any failures here are
    // unexpected.
    Ok(T::read_from(buffer.as_slice()).unwrap())
}

pub struct GpuDevice<'a, M: DriverMem> {
    resources: HashMap<u32, Resource2D<'a>>,
    mem: &'a M,
}

impl<'a, M: DriverMem> GpuDevice<'a, M> {
    pub fn new(mem: &'a M) -> Self {
        Self { resources: HashMap::new(), mem }
    }

    pub async fn process_queues<'b, N: DriverNotify>(
        &mut self,
        control_stream: WrappedDescChainStream<'a, 'b, N>,
        cursor_stream: WrappedDescChainStream<'a, 'b, N>,
    ) -> Result<(), Error> {
        // Merge the two streams but tag each value with the source queue. Doing it this way allows
        // use to pass our '&mut self' reference to the handler function because we don't process
        // any command concurrently.
        enum CommandQueue {
            Control,
            Cursor,
        }
        let mut stream = futures::stream::select(
            control_stream.map(|chain| (CommandQueue::Control, chain)),
            cursor_stream.map(|chain| (CommandQueue::Cursor, chain)),
        );

        while let Some((queue, chain)) = stream.next().await {
            let result = match queue {
                CommandQueue::Control => {
                    self.process_control_chain(ReadableChain::new(chain, self.mem))
                }
                CommandQueue::Cursor => {
                    self.process_cursor_chain(ReadableChain::new(chain, self.mem))
                }
            };
            if let Err(e) = result {
                tracing::warn!("Error processing control queue: {}", e);
            }
        }
        Ok(())
    }

    pub fn process_control_chain<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let header: wire::VirtioGpuCtrlHeader = match read_from_chain(&mut chain) {
            Ok(header) => header,
            Err(e) => {
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified)
                    .with_context(|| format!("Failed to read request header from queue: {}", e));
            }
        };
        match header.ty.get() {
            wire::VIRTIO_GPU_CMD_GET_DISPLAY_INFO => self.get_display_info(chain)?,
            wire::VIRTIO_GPU_CMD_RESOURCE_CREATE_2D => self.resource_create_2d(chain)?,
            wire::VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING => self.resource_attach_backing(chain)?,
            cmd => self.unsupported_command(chain, cmd)?,
        }
        Ok(())
    }

    pub fn process_cursor_chain<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let header: wire::VirtioGpuCtrlHeader = match read_from_chain(&mut chain) {
            Ok(header) => header,
            Err(e) => {
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified)
                    .with_context(|| format!("Failed to read request header from queue: {}", e));
            }
        };
        match header.ty.get() {
            cmd => self.unsupported_command(chain, cmd)?,
        }
        Ok(())
    }

    fn unsupported_command<'b, N: DriverNotify>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
        cmd: u32,
    ) -> Result<(), Error> {
        tracing::info!("Command {:#06x} is not implemented", cmd);
        write_error_to_chain(chain, wire::VirtioGpuError::Unspecified)
    }

    fn get_display_info<'b, N: DriverNotify>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let mut display_info: wire::VirtioGpuRespDisplayInfo = Default::default();
        display_info.hdr.ty = wire::VIRTIO_GPU_RESP_OK_DISPLAY_INFO.into();
        // TODO(fxbug.dev/102870): For now we just report a single pmode with some initial
        // geometry. This will eventually map to the geometry of our backing view and we might
        // want to consider just marking the scanout as disabled before we get our initial
        // geometry.
        display_info.pmodes[0] = wire::VirtioGpuDisplayOne {
            r: wire::VirtioGpuRect {
                x: 0.into(),
                y: 0.into(),
                width: VIRTIO_GPU_STARTUP_WIDTH.into(),
                height: VIRTIO_GPU_STARTUP_HEIGHT.into(),
            },
            enabled: 1.into(),
            flags: 0.into(),
        };

        // Now write the response to the chain.
        write_to_chain(WritableChain::from_readable(chain)?, display_info)
    }

    fn resource_create_2d<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let cmd: wire::VirtioGpuResourceCreate2d = match read_from_chain(&mut chain) {
            Ok(cmd) => cmd,
            Err(e) => {
                tracing::warn!(
                    "Failed to read VirtioGpuResourceCreate2d request from queue: {}",
                    e
                );
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
            }
        };
        // 0 is used to specify 'no resource in other commands (ex: SET_SCANOUT), so let's not
        // allow creating any resources with ID 0.
        if cmd.resource_id.get() == 0 {
            tracing::warn!("Failed to create resource with null resource id");
            return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
        }
        // Require that we have a non-zero size.
        if cmd.width.get() == 0 || cmd.height.get() == 0 {
            tracing::warn!(
                "Failed to create resource with zero size: {}x{}",
                cmd.width.get(),
                cmd.height.get()
            );
            return write_error_to_chain(chain, wire::VirtioGpuError::InvalidParameter);
        }
        {
            let entry = self.resources.entry(cmd.resource_id.get());
            if let Entry::Occupied(_) = &entry {
                tracing::warn!(
                    "Failed to create resource with duplicate ID: {}",
                    cmd.resource_id.get()
                );
                return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
            }
            match Resource2D::allocate_from_request(&cmd) {
                Ok((resource, _)) => {
                    entry.or_insert(resource);
                }
                Err(e) => {
                    tracing::info!("Failed to allocate resource {:?}: {}", cmd, e);
                    return write_error_to_chain(chain, wire::VirtioGpuError::OutOfMemory);
                }
            }
        }

        // Send success response.
        write_to_chain(
            WritableChain::from_readable(chain)?,
            wire::VirtioGpuCtrlHeader {
                ty: wire::VIRTIO_GPU_RESP_OK_NODATA.into(),
                ..Default::default()
            },
        )
    }

    fn resource_attach_backing<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let cmd: wire::VirtioGpuResourceAttachBacking = match read_from_chain(&mut chain) {
            Ok(cmd) => cmd,
            Err(e) => {
                tracing::warn!("Failed to read VirtioGpuResourceAttachBacking from queue: {}", e);
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
            }
        };

        // Validate resource_id
        let resource = match self.resources.get_mut(&cmd.resource_id.get()) {
            None => {
                tracing::warn!("AttachBacking to unknown resource_id {}", cmd.resource_id.get());
                return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
            }
            Some(resource) => resource,
        };

        // Enforce an upper bound to the number of entries we attempt to read.
        if cmd.nr_entries.get() > ATTACH_BACKING_MAX_ENTRIES {
            tracing::warn!(
                "Rejecting RESOURCE_ATTACH_BACKING request too many entries {} > {}",
                cmd.nr_entries.get(),
                ATTACH_BACKING_MAX_ENTRIES
            );
            return write_error_to_chain(chain, wire::VirtioGpuError::OutOfMemory);
        }

        // For each of `nr_entries`, there exists a VirtioGpuMemEntry struct in the chain that
        // holds a region of backing memory for this resource. For each entry, we want to read it
        // from the chain and translate those guest-physical addresses into DeviceRanges which will
        // allow us to read that memory from our virtual address space.
        let mut device_ranges = Vec::with_capacity(cmd.nr_entries.get() as usize);
        for _ in 0..cmd.nr_entries.get() {
            let entry: wire::VirtioGpuMemEntry = match read_from_chain(&mut chain) {
                Ok(entry) => entry,
                Err(e) => {
                    tracing::warn!("Failed to read VirtioGpuMemEntry from queue: {}", e);
                    return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
                }
            };
            let driver_addr = entry.addr.get() as usize;
            let length = entry.length.get() as usize;
            match self.mem.translate(DriverRange(driver_addr..driver_addr + length)) {
                // If translation failed, then the memory range does not reside within a valid
                // region in guest RAM.
                None => {
                    tracing::warn!("Invalid device range provided: {:?}", entry);
                    return write_error_to_chain(chain, wire::VirtioGpuError::InvalidParameter);
                }
                Some(device_range) => device_ranges.push(device_range),
            }
        }

        // Insert the backing pages into the resource.
        resource.attach_backing(device_ranges);

        write_to_chain(
            WritableChain::from_readable(chain)?,
            wire::VirtioGpuCtrlHeader {
                ty: wire::VIRTIO_GPU_RESP_OK_NODATA.into(),
                ..Default::default()
            },
        )
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::wire,
        virtio_device::chain::ReadableChain,
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
    };

    fn read_returned<T: FromBytes>(range: (u64, u32)) -> T {
        let (data, len) = range;
        let slice =
            unsafe { std::slice::from_raw_parts::<u8>(data as usize as *const u8, len as usize) };
        T::read_from(slice).expect("Failed to read result from returned chain")
    }

    #[fuchsia::test]
    fn test_unsupported_command() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioGpuCtrlHeader {
                            ty: 0xdeadbeef.into(),
                            ..Default::default()
                        }),
                        &mem,
                    )
                    // Add an arbitrarily large output descriptor.
                    .writable(8192, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let mut device = GpuDevice::new(&mem);

        // Process the request.
        device
            .process_control_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .expect("Failed to process control chain");

        // Validate the returned chain has a written header.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(std::mem::size_of::<wire::VirtioGpuCtrlHeader>(), returned.written() as usize);

        // Verify we get an ERR_UNSPEC back.
        let mut iter = returned.data_iter();
        let result = read_returned::<wire::VirtioGpuCtrlHeader>(iter.next().unwrap());
        assert_eq!(result.ty.get(), wire::VIRTIO_GPU_RESP_ERR_UNSPEC);
    }

    #[fuchsia::test]
    fn test_get_display_info() {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioGpuCtrlHeader {
                            ty: wire::VIRTIO_GPU_CMD_GET_DISPLAY_INFO.into(),
                            ..Default::default()
                        }),
                        &mem,
                    )
                    .writable(std::mem::size_of::<wire::VirtioGpuRespDisplayInfo>() as u32, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let mut device = GpuDevice::new(&mem);

        // Process the request.
        device
            .process_control_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .expect("Failed to process control chain");

        // Validate returned chain. We should have a DISPLAY_INFO response structure.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(
            std::mem::size_of::<wire::VirtioGpuRespDisplayInfo>(),
            returned.written() as usize
        );

        // Expect a single scanout reported with hard-coded geometry. The other scanouts should be
        // disabled.
        let mut iter = returned.data_iter();
        let result = read_returned::<wire::VirtioGpuRespDisplayInfo>(iter.next().unwrap());
        assert_eq!(result.hdr.ty.get(), wire::VIRTIO_GPU_RESP_OK_DISPLAY_INFO);
        // The first pmode is enabled.
        assert_eq!(result.pmodes[0].r.x.get(), 0);
        assert_eq!(result.pmodes[0].r.y.get(), 0);
        assert_eq!(result.pmodes[0].r.width.get(), VIRTIO_GPU_STARTUP_WIDTH);
        assert_eq!(result.pmodes[0].r.height.get(), VIRTIO_GPU_STARTUP_HEIGHT);
        assert_eq!(result.pmodes[0].enabled.get(), 1);
        assert_eq!(result.pmodes[0].flags.get(), 0);
        // The rest should be disabled.
        for pmode in 1..wire::VIRTIO_GPU_MAX_SCANOUTS {
            assert_eq!(result.pmodes[pmode].r.x.get(), 0);
            assert_eq!(result.pmodes[pmode].r.y.get(), 0);
            assert_eq!(result.pmodes[pmode].r.width.get(), 0);
            assert_eq!(result.pmodes[pmode].r.height.get(), 0);
            assert_eq!(result.pmodes[pmode].enabled.get(), 0);
            assert_eq!(result.pmodes[pmode].flags.get(), 0);
        }
    }

    const VALID_RESOURCE_ID: u32 = 1;
    const VALID_RESOURCE_WIDTH: u32 = 1024;
    const VALID_RESOURCE_HEIGHT: u32 = 768;
    const VALID_RESOURCE_FORMAT: u32 = wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;

    /// A fixture for reusing logic across different tests.
    struct TestFixture<'a> {
        mem: &'a IdentityDriverMem,
        state: TestQueue<'a>,
        device: GpuDevice<'a, IdentityDriverMem>,
    }

    impl<'a> TestFixture<'a> {
        pub fn new(mem: &'a IdentityDriverMem) -> Self {
            let state = TestQueue::new(32, mem);
            let device = GpuDevice::new(mem);
            Self { mem, state, device }
        }

        /// Sends a VIRTIO_GPU_CMD_RESOURCE_CREATE_2D and returns the response.
        ///
        /// This asserts that the device has written the correct number of bytes to represent the
        /// expected response struct.
        fn resource_create_2d(
            &mut self,
            cmd: wire::VirtioGpuResourceCreate2d,
        ) -> wire::VirtioGpuCtrlHeader {
            self.state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        // Header
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuCtrlHeader {
                                ty: wire::VIRTIO_GPU_CMD_RESOURCE_CREATE_2D.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        // Request
                        .readable(std::slice::from_ref(&cmd), self.mem)
                        .writable(std::mem::size_of::<wire::VirtioGpuCtrlHeader>() as u32, self.mem)
                        .build(),
                )
                .unwrap();

            self.device
                .process_control_chain(ReadableChain::new(
                    self.state.queue.next_chain().unwrap(),
                    self.mem,
                ))
                .expect("Failed to process control chain");

            let returned = self.state.fake_queue.next_used().unwrap();
            assert_eq!(
                std::mem::size_of::<wire::VirtioGpuCtrlHeader>(),
                returned.written() as usize
            );

            // Read and return the header.
            let mut iter = returned.data_iter();
            read_returned::<wire::VirtioGpuCtrlHeader>(iter.next().unwrap())
        }

        /// Sends a VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING and returns the response.
        fn resource_attach_backing(
            &mut self,
            resource_id: u32,
            entries: Vec<wire::VirtioGpuMemEntry>,
        ) -> wire::VirtioGpuCtrlHeader {
            self.state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        // Header
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuCtrlHeader {
                                ty: wire::VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        // Command
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuResourceAttachBacking {
                                resource_id: resource_id.into(),
                                nr_entries: (entries.len() as u32).into(),
                            }),
                            self.mem,
                        )
                        // entries
                        .readable(entries.as_slice(), self.mem)
                        .writable(std::mem::size_of::<wire::VirtioGpuCtrlHeader>() as u32, self.mem)
                        .build(),
                )
                .unwrap();

            self.device
                .process_control_chain(ReadableChain::new(
                    self.state.queue.next_chain().unwrap(),
                    self.mem,
                ))
                .expect("Failed to process control chain");

            let returned = self.state.fake_queue.next_used().unwrap();
            assert_eq!(
                std::mem::size_of::<wire::VirtioGpuCtrlHeader>(),
                returned.written() as usize
            );

            // Read and return the header.
            let mut iter = returned.data_iter();
            read_returned::<wire::VirtioGpuCtrlHeader>(iter.next().unwrap())
        }
    }

    #[fuchsia::test]
    fn test_resource_create_2d() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);
        let response = test.resource_create_2d(wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
        });
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);
    }

    #[fuchsia::test]
    fn test_resource_create_2d_null_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);
        let response = test.resource_create_2d(wire::VirtioGpuResourceCreate2d {
            // 0 is not a valid resource id.
            resource_id: 0.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
        });
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }

    #[fuchsia::test]
    fn test_resource_create_2d_duplicate_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let request = wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
        };

        // Create a resource, this should succeed.
        let response = test.resource_create_2d(request.clone());
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Create another resource with the same request. This will fail because the id is already
        // in use.
        let response = test.resource_create_2d(request.clone());
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }

    #[fuchsia::test]
    fn test_resource_create_2d_zero_sized_resource() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let response = test.resource_create_2d(wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: 0.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
        });
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);

        let response = test.resource_create_2d(wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: 0.into(),
            format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
        });
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    }

    #[fuchsia::test]
    fn test_resource_attach_backing() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let create_request = wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: VALID_RESOURCE_FORMAT.into(),
        };
        let response = test.resource_create_2d(create_request);
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Allocate a backing buffer in guest memory. We need to allocate this through our
        // IdentityDriverMem because these addresses will need to be translated by the device to
        // obtain a DeviceRange.
        let backing_size = create_request.width.get() * create_request.height.get() * 4;
        let alloc =
            mem.new_range(backing_size as usize).expect("Failed to allocate backing memory");

        // Try to attach the backing memory.
        let response = test.resource_attach_backing(
            create_request.resource_id.get(),
            vec![wire::VirtioGpuMemEntry {
                addr: (alloc.get().start as u64).into(),
                length: (alloc.len() as u32).into(),
                ..Default::default()
            }],
        );
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);
    }

    #[fuchsia::test]
    fn test_resource_attach_backing_invalid_range() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let create_request = wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: VALID_RESOURCE_FORMAT.into(),
        };
        let response = test.resource_create_2d(create_request);
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Allocate some memory on the heap. Since this is not allocated through our DriverMem, the
        // device will be unable to translate these addresses. This is what would happen if the
        // driver provided the device with a memory region outside of the valid memory area.
        //
        // Note that the primary reason we allocate on the heap here it to guarantee we're using
        // addresses the IdentityDriverMem could not possibly be using.
        let backing_size = create_request.width.get() * create_request.height.get() * 4;
        let alloc = vec![0u8; backing_size as usize];

        // Try to attach the backing memory.
        let response = test.resource_attach_backing(
            create_request.resource_id.get(),
            vec![wire::VirtioGpuMemEntry {
                addr: (alloc.as_ptr() as u64).into(),
                length: (alloc.len() as u32).into(),
                ..Default::default()
            }],
        );
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    }

    #[fuchsia::test]
    fn test_resource_attach_backing_invalid_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        // Allocate a backing buffer in guest memory. We need to allocate this through our
        // IdentityDriverMem because these addresses will need to be translated by the device to
        // obtain a DeviceRange.
        let backing_size = VALID_RESOURCE_WIDTH * VALID_RESOURCE_HEIGHT * 4;
        let alloc =
            mem.new_range(backing_size as usize).expect("Failed to allocate backing memory");

        // Try to attach the backing memory. We didn't create a resource so we expect this to fail.
        let response = test.resource_attach_backing(
            VALID_RESOURCE_ID,
            vec![wire::VirtioGpuMemEntry {
                addr: (alloc.get().start as u64).into(),
                length: (alloc.len() as u32).into(),
                ..Default::default()
            }],
        );

        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }
}
