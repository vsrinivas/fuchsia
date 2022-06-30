// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire,
    anyhow::Error,
    std::io::{Read, Write},
    tracing,
    virtio_device::chain::{ReadableChain, WritableChain},
    virtio_device::mem::DriverMem,
    virtio_device::queue::DriverNotify,
    zerocopy::{AsBytes, FromBytes},
};

const VIRTIO_GPU_STARTUP_HEIGHT: u32 = 720;
const VIRTIO_GPU_STARTUP_WIDTH: u32 = 1280;

/// Reads a `wire::VirtioGpuCtrlHeader` from the chain.
fn read_header<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: &mut ReadableChain<'a, 'b, N, M>,
) -> Result<wire::VirtioGpuCtrlHeader, anyhow::Error> {
    let mut header_buf: [u8; std::mem::size_of::<wire::VirtioGpuCtrlHeader>()] =
        [0; std::mem::size_of::<wire::VirtioGpuCtrlHeader>()];
    chain.read_exact(&mut header_buf)?;
    // read_from should not fail since we've sized the buffer appropriately. Any failures here are
    // unexpected.
    Ok(wire::VirtioGpuCtrlHeader::read_from(header_buf.as_slice())
        .expect("Failed to deserialize VirtioGpuCtrlHeader."))
}

pub struct GpuDevice {}

impl GpuDevice {
    pub fn new() -> Self {
        Self {}
    }

    pub fn process_control_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let header = read_header(&mut chain)?;
        match header.ty.get() {
            wire::VIRTIO_GPU_CMD_GET_DISPLAY_INFO => self.get_display_info(chain),
            cmd => self.unsupported_command(chain, cmd),
        }
        Ok(())
    }

    pub fn process_cursor_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), Error> {
        let header = read_header(&mut chain)?;
        match header.ty.get() {
            cmd => self.unsupported_command(chain, cmd),
        }
        Ok(())
    }

    fn unsupported_command<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
        cmd: u32,
    ) {
        tracing::info!("Command {:#06x} is not implemented", cmd);
        let response = wire::VirtioGpuCtrlHeader {
            ty: wire::VIRTIO_GPU_RESP_ERR_UNSPEC.into(),
            ..Default::default()
        };

        let mut chain = WritableChain::from_readable(chain).unwrap();
        chain.write_all(response.as_bytes()).unwrap();
    }

    fn get_display_info<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
    ) {
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

        let mut chain = WritableChain::from_readable(chain).unwrap();
        chain.write_all(display_info.as_bytes()).unwrap();
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
        let device = GpuDevice::new();

        // Process the request.
        device
            .process_control_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .expect("Failed to process display info command");

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
        let device = GpuDevice::new();

        // Process the request.
        device
            .process_control_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
            .expect("Failed to process display info command");

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
}
