// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::resource::Resource2D,
    crate::scanout::Scanout,
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
    scanout: Option<Scanout>,
}

impl<'a, M: DriverMem> GpuDevice<'a, M> {
    /// Create a GpuDevice without any scanouts (ex: no attached displays will be reported in
    /// GET_DISPLAY_INFOs).
    pub fn new(mem: &'a M) -> Self {
        Self { resources: HashMap::new(), mem, scanout: None }
    }

    /// Create a GpuDevice with a single scanout.
    pub fn with_scanout(mem: &'a M, scanout: Scanout) -> Self {
        Self { resources: HashMap::new(), mem, scanout: Some(scanout) }
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
                    self.process_control_chain(ReadableChain::new(chain, self.mem)).await
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

    pub async fn process_control_chain<'b, N: DriverNotify>(
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
            wire::VIRTIO_GPU_CMD_RESOURCE_CREATE_2D => self.resource_create_2d(chain).await?,
            wire::VIRTIO_GPU_CMD_RESOURCE_UNREF => self.resource_unref(chain)?,
            wire::VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING => self.resource_attach_backing(chain)?,
            wire::VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING => self.resource_detach_backing(chain)?,
            wire::VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D => self.transfer_to_host_2d(chain)?,
            wire::VIRTIO_GPU_CMD_SET_SCANOUT => self.set_scanout(chain)?,
            wire::VIRTIO_GPU_CMD_RESOURCE_FLUSH => self.resource_flush(chain)?,
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

    fn get_resource_mut<'b>(&'b mut self, id: u32) -> Option<&'b mut Resource2D<'a>> {
        if id == 0 {
            return None;
        }
        self.resources.get_mut(&id)
    }

    fn get_resource<'b>(&'b self, id: u32) -> Option<&'b Resource2D<'a>> {
        if id == 0 {
            return None;
        }
        self.resources.get(&id)
    }

    fn get_display_info<'b, N: DriverNotify>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let mut display_info: wire::VirtioGpuRespDisplayInfo = Default::default();
        display_info.hdr.ty = wire::VIRTIO_GPU_RESP_OK_DISPLAY_INFO.into();
        if self.scanout.is_some() {
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
        }

        // Now write the response to the chain.
        write_to_chain(WritableChain::from_readable(chain)?, display_info)
    }

    async fn resource_create_2d<'b, N: DriverNotify>(
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
            match Resource2D::allocate(&cmd).await {
                Ok(resource) => {
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

    fn resource_unref<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let cmd: wire::VirtioGpuResourceUnref = match read_from_chain(&mut chain) {
            Ok(cmd) => cmd,
            Err(e) => {
                tracing::warn!("Failed to read VirtioGpuResourceAttachBacking from queue: {}", e);
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
            }
        };

        // Remove the resource from the map if it exists.
        if self.resources.remove(&cmd.resource_id.get()).is_none() {
            tracing::warn!("Failed to unref unknown resource: {}", cmd.resource_id.get());
            return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
        }

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

        // Validate resource_id
        let resource = match self.get_resource_mut(cmd.resource_id.get()) {
            None => {
                tracing::warn!("AttachBacking to unknown resource_id {}", cmd.resource_id.get());
                return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
            }
            Some(resource) => resource,
        };

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

    fn resource_detach_backing<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let cmd: wire::VirtioGpuResourceDetachBacking = match read_from_chain(&mut chain) {
            Ok(cmd) => cmd,
            Err(e) => {
                tracing::warn!("Failed to read VirtioGpuResourceDetachBacking from queue: {}", e);
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
            }
        };

        // Lookup resource
        let resource = match self.get_resource_mut(cmd.resource_id.get()) {
            None => {
                tracing::warn!("DetachBacking to unknown resource_id {}", cmd.resource_id.get());
                return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
            }
            Some(resource) => resource,
        };

        // Detach backing memory
        resource.detach_backing();

        write_to_chain(
            WritableChain::from_readable(chain)?,
            wire::VirtioGpuCtrlHeader {
                ty: wire::VIRTIO_GPU_RESP_OK_NODATA.into(),
                ..Default::default()
            },
        )
    }

    fn transfer_to_host_2d<'b, N: DriverNotify>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let cmd: wire::VirtioGpuTransferToHost2d = match read_from_chain(&mut chain) {
            Ok(cmd) => cmd,
            Err(e) => {
                tracing::warn!("Failed to read VirtioGpuTransferToHost2d from queue: {}", e);
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
            }
        };

        // Validate resource_id
        let resource = match self.get_resource(cmd.resource_id.get()) {
            None => {
                tracing::warn!("AttachBacking to unknown resource_id {}", cmd.resource_id.get());
                return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
            }
            Some(resource) => resource,
        };

        // Do the transfer.
        if let Err(e) = resource.transfer_to_host_2d(cmd.offset.get(), &cmd.r) {
            tracing::warn!("Failed to transfer_to_host_2d: {}", e);
            return write_error_to_chain(chain, e);
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

    fn set_scanout<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let cmd: wire::VirtioGpuSetScanout = match read_from_chain(&mut chain) {
            Ok(cmd) => cmd,
            Err(e) => {
                tracing::warn!("Failed to read VirtioGpuSetScanout from queue: {}", e);
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
            }
        };

        // Validate resource_id. Resource ID 0 is a special case that indicates that no resource
        // should be associated with the target scanout.
        let resource_id = cmd.resource_id.get();
        let resource = if resource_id == 0 {
            None
        } else {
            let resource = self.resources.get(&cmd.resource_id.get());
            if resource.is_none() {
                tracing::warn!("SetScanout with invalid resource_id: {}", resource_id);
                return write_error_to_chain(chain, wire::VirtioGpuError::InvalidResourceId);
            }
            resource
        };

        // Validate scanout_id
        let scanout_id: usize = cmd.scanout_id.get().try_into()?;
        if scanout_id != 0 || self.scanout.is_none() {
            tracing::warn!("SetScanout to invalid scanout_id {}", scanout_id);
            return write_error_to_chain(chain, wire::VirtioGpuError::InvalidScanoutId);
        }

        // Update the scanout.
        self.scanout.as_mut().unwrap().set_resource(&cmd, resource)?;

        write_to_chain(
            WritableChain::from_readable(chain)?,
            wire::VirtioGpuCtrlHeader {
                ty: wire::VIRTIO_GPU_RESP_OK_NODATA.into(),
                ..Default::default()
            },
        )
    }

    fn resource_flush<'b, N: DriverNotify>(
        &mut self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let _cmd: wire::VirtioGpuResourceFlush = match read_from_chain(&mut chain) {
            Ok(cmd) => cmd,
            Err(e) => {
                tracing::warn!("Failed to read VirtioGpuResourceFlush from queue: {}", e);
                return write_error_to_chain(chain, wire::VirtioGpuError::Unspecified);
            }
        };

        if let Some(scanout) = self.scanout.as_mut() {
            scanout.present()?;
        }

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
        crate::resource::bytes_per_pixel,
        crate::wire,
        virtio_device::{
            chain::ReadableChain,
            fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
            mem::DeviceRange,
        },
    };

    fn read_returned<T: FromBytes>(range: (u64, u32)) -> T {
        let (data, len) = range;
        let slice =
            unsafe { std::slice::from_raw_parts::<u8>(data as usize as *const u8, len as usize) };
        T::read_from(slice).expect("Failed to read result from returned chain")
    }

    #[fuchsia::test]
    async fn test_unsupported_command() {
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
            .await
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
    async fn test_get_display_info() {
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
            .await
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
        // All pmodes are disabled.
        for pmode in 0..wire::VIRTIO_GPU_MAX_SCANOUTS {
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
        async fn resource_create_2d(
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
                .await
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

        async fn default_resource_create_2d(
            &mut self,
        ) -> (wire::VirtioGpuResourceCreate2d, wire::VirtioGpuCtrlHeader) {
            let request = wire::VirtioGpuResourceCreate2d {
                resource_id: VALID_RESOURCE_ID.into(),
                width: VALID_RESOURCE_WIDTH.into(),
                height: VALID_RESOURCE_HEIGHT.into(),
                format: VALID_RESOURCE_FORMAT.into(),
            };
            let response = self.resource_create_2d(request.clone()).await;
            (request, response)
        }

        async fn resource_unref(&mut self, resource_id: u32) -> wire::VirtioGpuCtrlHeader {
            self.state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        // Header
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuCtrlHeader {
                                ty: wire::VIRTIO_GPU_CMD_RESOURCE_UNREF.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        // Command
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuResourceUnref {
                                resource_id: resource_id.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        .writable(std::mem::size_of::<wire::VirtioGpuCtrlHeader>() as u32, self.mem)
                        .build(),
                )
                .unwrap();

            self.device
                .process_control_chain(ReadableChain::new(
                    self.state.queue.next_chain().unwrap(),
                    self.mem,
                ))
                .await
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
        async fn resource_attach_backing(
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
                .await
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

        /// Performs a RESOURCE_ATTACH_BACKING with a default request that will succeed for simple
        /// tests. Returns a `DeviceRange` to cover the backing memory and the response for the
        /// RESOURCE_ATTACH_BACKING command.
        async fn default_resource_attach_backing(
            &mut self,
            resource: &wire::VirtioGpuResourceCreate2d,
        ) -> (DeviceRange<'a>, wire::VirtioGpuCtrlHeader) {
            let backing_size: usize =
                (resource.width.get() * resource.height.get()).try_into().unwrap();
            let backing_size = backing_size * bytes_per_pixel();
            let alloc = self
                .mem
                .new_range(backing_size as usize)
                .expect("Failed to allocate backing memory");
            let response = self
                .resource_attach_backing(
                    resource.resource_id.get(),
                    vec![wire::VirtioGpuMemEntry {
                        addr: (alloc.get().start as u64).into(),
                        length: (alloc.len() as u32).into(),
                        ..Default::default()
                    }],
                )
                .await;
            (alloc, response)
        }

        async fn resource_detach_backing(&mut self, resource_id: u32) -> wire::VirtioGpuCtrlHeader {
            self.state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        // Header
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuCtrlHeader {
                                ty: wire::VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        // Command
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuResourceDetachBacking {
                                resource_id: resource_id.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        .writable(std::mem::size_of::<wire::VirtioGpuCtrlHeader>() as u32, self.mem)
                        .build(),
                )
                .unwrap();

            self.device
                .process_control_chain(ReadableChain::new(
                    self.state.queue.next_chain().unwrap(),
                    self.mem,
                ))
                .await
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

        async fn transfer_to_host_2d(
            &mut self,
            resource_id: u32,
            rect: wire::VirtioGpuRect,
        ) -> wire::VirtioGpuCtrlHeader {
            let resource_width = self
                .device
                .get_resource(resource_id)
                .map(|resource| u64::from(resource.width()))
                .unwrap();
            let offset = {
                let pixel_offset =
                    u64::from(rect.y.get()) * resource_width + u64::from(rect.x.get());
                let bytes_per_pixel = u64::try_from(bytes_per_pixel()).unwrap();
                pixel_offset * bytes_per_pixel
            };
            self.state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        // Header
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuCtrlHeader {
                                ty: wire::VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        // Command
                        .readable(
                            std::slice::from_ref(&wire::VirtioGpuTransferToHost2d {
                                resource_id: resource_id.into(),
                                r: rect,
                                offset: offset.into(),
                                ..Default::default()
                            }),
                            self.mem,
                        )
                        .writable(std::mem::size_of::<wire::VirtioGpuCtrlHeader>() as u32, self.mem)
                        .build(),
                )
                .unwrap();

            self.device
                .process_control_chain(ReadableChain::new(
                    self.state.queue.next_chain().unwrap(),
                    self.mem,
                ))
                .await
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

        async fn transfer_entire_resource(
            &mut self,
            create_request: &wire::VirtioGpuResourceCreate2d,
        ) -> wire::VirtioGpuCtrlHeader {
            self.transfer_to_host_2d(
                create_request.resource_id.get(),
                wire::VirtioGpuRect {
                    x: 0.into(),
                    y: 0.into(),
                    width: create_request.width,
                    height: create_request.height,
                },
            )
            .await
        }

        /// Like `check_resource_rect` except the entire resource is checked.
        fn check_resource(&self, resource_id: u32, value: u8) {
            let (width, height) = {
                let resource = self.device.get_resource(resource_id).unwrap();
                (resource.width(), resource.height())
            };
            self.check_resource_rect(
                resource_id,
                value,
                wire::VirtioGpuRect {
                    x: 0.into(),
                    y: 0.into(),
                    width: width.into(),
                    height: height.into(),
                },
            );
        }

        /// Checks that a rectangular region within a given host resource contains only the byte
        /// `value`.
        ///
        /// Will panic if there is a non-matching byte, or any other error is encountered.
        fn check_resource_rect(&self, resource_id: u32, value: u8, rect: wire::VirtioGpuRect) {
            let rect_x: usize = rect.x.get().try_into().unwrap();
            let rect_y: usize = rect.y.get().try_into().unwrap();
            let rect_width: usize = rect.width.get().try_into().unwrap();
            let rect_height: usize = rect.height.get().try_into().unwrap();

            let resource = self.device.get_resource(resource_id).unwrap();
            let res_width: usize = resource.width().try_into().unwrap();

            let mut offset = (rect_y * res_width + rect_x) * bytes_per_pixel();
            let mut row_data = vec![0u8; rect_width * bytes_per_pixel()];
            for _ in 0..rect_height {
                resource.mapping().read_at(offset, row_data.as_mut());
                assert_eq!(None, row_data.iter().find(|x| **x != value));
                offset += res_width * bytes_per_pixel();
            }
        }
    }

    #[fuchsia::test]
    async fn test_resource_create_2d() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);
        let (_, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);
    }

    #[fuchsia::test]
    async fn test_resource_create_2d_null_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);
        let response = test
            .resource_create_2d(wire::VirtioGpuResourceCreate2d {
                // 0 is not a valid resource id.
                resource_id: 0.into(),
                width: VALID_RESOURCE_WIDTH.into(),
                height: VALID_RESOURCE_HEIGHT.into(),
                format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
            })
            .await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }

    #[fuchsia::test]
    async fn test_resource_create_2d_duplicate_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        // Create one resource, this should succeed.
        let (request, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Create another resource with the same request. This will fail because the id is already
        // in use.
        let response = test.resource_create_2d(request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }

    #[fuchsia::test]
    async fn test_resource_create_2d_zero_sized_resource() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let response = test
            .resource_create_2d(wire::VirtioGpuResourceCreate2d {
                resource_id: VALID_RESOURCE_ID.into(),
                width: 0.into(),
                height: VALID_RESOURCE_HEIGHT.into(),
                format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
            })
            .await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);

        let response = test
            .resource_create_2d(wire::VirtioGpuResourceCreate2d {
                resource_id: VALID_RESOURCE_ID.into(),
                width: VALID_RESOURCE_WIDTH.into(),
                height: 0.into(),
                format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
            })
            .await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    }

    #[fuchsia::test]
    async fn test_resource_attach_backing() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let (create_request, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Allocate a backing buffer in guest memory. We need to allocate this through our
        // IdentityDriverMem because these addresses will need to be translated by the device to
        // obtain a DeviceRange.
        let (_, response) = test.default_resource_attach_backing(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);
    }

    #[fuchsia::test]
    async fn test_resource_attach_backing_invalid_range() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let (create_request, response) = test.default_resource_create_2d().await;
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
        let response = test
            .resource_attach_backing(
                create_request.resource_id.get(),
                vec![wire::VirtioGpuMemEntry {
                    addr: (alloc.as_ptr() as u64).into(),
                    length: (alloc.len() as u32).into(),
                    ..Default::default()
                }],
            )
            .await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    }

    #[fuchsia::test]
    async fn test_resource_attach_backing_invalid_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let create_request = wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: VALID_RESOURCE_FORMAT.into(),
        };

        // ATTACH_BACKING without creating a resource. This will fail because the resource will not
        // yet exist.
        let (_, response) = test.default_resource_attach_backing(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }

    #[fuchsia::test]
    async fn test_transfer_to_host_2d_full_resource() {
        // Test TRANSFER_TO_HOST_2D where the transfer copies the entire resource.
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let (create_request, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        let (alloc, response) = test.default_resource_attach_backing(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Fill backing memory with some data.
        const BACKING_MEMORY_BYTE_PATTERN: u8 = 0xde;
        let slice = unsafe {
            std::slice::from_raw_parts_mut(alloc.try_mut_ptr::<u8>().unwrap(), alloc.len())
        };
        slice.fill(BACKING_MEMORY_BYTE_PATTERN);

        // Read the host resource data. This should be all zeros since we haven't written anything
        // to it yet.
        test.check_resource(create_request.resource_id.get(), 0);

        // Do the TRANSFER_TO_HOST_2D
        let response = test
            .transfer_to_host_2d(
                create_request.resource_id.get(),
                wire::VirtioGpuRect {
                    x: 0.into(),
                    y: 0.into(),
                    width: create_request.width,
                    height: create_request.height,
                },
            )
            .await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Now we expect that host resource has the data from the backing memory.
        test.check_resource(create_request.resource_id.get(), BACKING_MEMORY_BYTE_PATTERN);
    }

    #[fuchsia::test]
    async fn test_transfer_to_host_2d_subrect() {
        // Test TRANSFER_TO_HOST_2D where the transfer only copies a portion of the resource.
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let (create_request, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        let (alloc, response) = test.default_resource_attach_backing(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Fill backing memory with some data.
        const BACKING_MEMORY_BYTE_PATTERN: u8 = 0xde;
        let slice = unsafe {
            std::slice::from_raw_parts_mut(alloc.try_mut_ptr::<u8>().unwrap(), alloc.len())
        };
        slice.fill(BACKING_MEMORY_BYTE_PATTERN);

        // Read the host resource data. This should be all zeros since we haven't written anything
        // to it yet.
        test.check_resource(create_request.resource_id.get(), 0);

        // Do the TRANSFER_TO_HOST_2D of a small rectangle.
        let rect =
            wire::VirtioGpuRect { x: 10.into(), y: 10.into(), width: 10.into(), height: 10.into() };
        let response =
            test.transfer_to_host_2d(create_request.resource_id.get(), rect.clone()).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // The transferred region should have the byte pattern from the backing memory.
        test.check_resource_rect(
            create_request.resource_id.get(),
            BACKING_MEMORY_BYTE_PATTERN,
            rect.clone(),
        );

        // And the other regions should be zero still.
        // Top
        test.check_resource_rect(
            create_request.resource_id.get(),
            0,
            wire::VirtioGpuRect {
                x: 0.into(),
                y: 0.into(),
                width: create_request.width,
                height: rect.y,
            },
        );
        // Bottom
        test.check_resource_rect(
            create_request.resource_id.get(),
            0,
            wire::VirtioGpuRect {
                x: 0.into(),
                y: (rect.y.get() + rect.height.get()).into(),
                width: create_request.width,
                height: (create_request.height.get() - (rect.y.get() + rect.height.get())).into(),
            },
        );
        // Left
        test.check_resource_rect(
            create_request.resource_id.get(),
            0,
            wire::VirtioGpuRect {
                x: 0.into(),
                y: 0.into(),
                width: rect.x,
                height: create_request.height,
            },
        );
        // Right
        test.check_resource_rect(
            create_request.resource_id.get(),
            0,
            wire::VirtioGpuRect {
                x: (rect.x.get() + rect.width.get()).into(),
                y: 0.into(),
                width: (create_request.width.get() - (rect.x.get() + rect.width.get())).into(),
                height: create_request.height,
            },
        );
    }

    /// A simple test helper that will setup a gpu device and run a TRANSFER_TO_HOST_2D command
    /// with the provided rect and verify the given result is returned.
    ///
    /// This is primarily interesting for simple or negative cases where we don't actually want or
    /// need to do any validation of the transferred data into the host resource.
    async fn test_transfer_to_host_rect(rect: wire::VirtioGpuRect, result: u32) {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let (create_request, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        let (alloc, response) = test.default_resource_attach_backing(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Fill backing memory with some data.
        const BACKING_MEMORY_BYTE_PATTERN: u8 = 0xde;
        let slice = unsafe {
            std::slice::from_raw_parts_mut(alloc.try_mut_ptr::<u8>().unwrap(), alloc.len())
        };
        slice.fill(BACKING_MEMORY_BYTE_PATTERN);

        let response = test.transfer_to_host_2d(create_request.resource_id.get(), rect).await;
        assert_eq!(response.ty.get(), result);
    }

    #[fuchsia::test]
    async fn test_transfer_to_host_2d_zero_width() {
        test_transfer_to_host_rect(
            wire::VirtioGpuRect { x: 0.into(), y: 0.into(), width: 0.into(), height: 10.into() },
            wire::VIRTIO_GPU_RESP_OK_NODATA,
        )
        .await;
    }

    #[fuchsia::test]
    async fn test_transfer_to_host_2d_zero_height() {
        test_transfer_to_host_rect(
            wire::VirtioGpuRect { x: 0.into(), y: 0.into(), width: 10.into(), height: 0.into() },
            wire::VIRTIO_GPU_RESP_OK_NODATA,
        )
        .await;
    }

    #[fuchsia::test]
    async fn test_transfer_to_host_2d_overflow_width() {
        // width overflow
        test_transfer_to_host_rect(
            wire::VirtioGpuRect {
                x: 0.into(),
                y: 0.into(),
                width: (VALID_RESOURCE_WIDTH + 1).into(),
                height: VALID_RESOURCE_HEIGHT.into(),
            },
            wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
        )
        .await;

        // x + width is overflow.
        test_transfer_to_host_rect(
            wire::VirtioGpuRect {
                x: VALID_RESOURCE_WIDTH.into(),
                y: 0.into(),
                width: 1.into(),
                height: VALID_RESOURCE_HEIGHT.into(),
            },
            wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
        )
        .await;
    }

    #[fuchsia::test]
    async fn test_transfer_to_host_2d_overflow_height() {
        // height overflow
        test_transfer_to_host_rect(
            wire::VirtioGpuRect {
                x: 0.into(),
                y: 0.into(),
                width: VALID_RESOURCE_WIDTH.into(),
                height: (VALID_RESOURCE_HEIGHT + 1).into(),
            },
            wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
        )
        .await;

        // y + height is overflow.
        test_transfer_to_host_rect(
            wire::VirtioGpuRect {
                x: 0.into(),
                y: VALID_RESOURCE_HEIGHT.into(),
                width: VALID_RESOURCE_WIDTH.into(),
                height: 1.into(),
            },
            wire::VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER,
        )
        .await;
    }

    #[fuchsia::test]
    async fn test_resource_detach_backing() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        // Create a resource
        let (create_request, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Attach backing
        let (_alloc, response) = test.default_resource_attach_backing(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Transfer the resource. The should succeed.
        let response = test.transfer_entire_resource(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Now detach backing memory.
        let response = test.resource_detach_backing(create_request.resource_id.get()).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Transfer the resource again. The should fail now that there is no backing memory.
        let response = test.transfer_entire_resource(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_UNSPEC);
    }

    #[fuchsia::test]
    async fn test_resource_detach_backing_invalid_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        // Detach backing with no created resource. This should fail.
        let create_request = wire::VirtioGpuResourceCreate2d {
            resource_id: VALID_RESOURCE_ID.into(),
            width: VALID_RESOURCE_WIDTH.into(),
            height: VALID_RESOURCE_HEIGHT.into(),
            format: VALID_RESOURCE_FORMAT.into(),
        };
        let response = test.resource_detach_backing(create_request.resource_id.get()).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }

    #[fuchsia::test]
    async fn test_resource_unref() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        // Create a resource
        let (create_request, response) = test.default_resource_create_2d().await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Unref that resource.
        let response = test.resource_unref(create_request.resource_id.get()).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_OK_NODATA);

        // Unref again; this should fail.
        let response = test.resource_unref(create_request.resource_id.get()).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);

        // Attach backing; this should fail.
        let (_alloc, response) = test.default_resource_attach_backing(&create_request).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }

    #[fuchsia::test]
    async fn test_resource_unref_invalid_resource_id() {
        let mem = IdentityDriverMem::new();
        let mut test = TestFixture::new(&mem);

        let response = test.resource_unref(1).await;
        assert_eq!(response.ty.get(), wire::VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    }
}
