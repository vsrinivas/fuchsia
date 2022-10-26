// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::backend::{BlockBackend, DeviceAttrs, Request, Sector},
    crate::wire,
    anyhow::anyhow,
    fidl_fuchsia_virtualization::BlockMode,
    std::io::{Read, Write},
    thiserror::Error,
    virtio_device::chain::{ReadableChain, WritableChain},
    virtio_device::mem::{DeviceRange, DriverMem},
    virtio_device::queue::DriverNotify,
    zerocopy::FromBytes,
};

#[derive(Error, Debug, PartialEq, Eq)]
enum BlockError {
    #[error("Request size does not have 512-byte alignment.")]
    UnalignedSize,
    #[error("The entire region [sector, sector + request_size) cannot be represented bytewise with a u64.")]
    IntegerOverflow,
    #[error("The requested range is outside the capacity of the device.")]
    DeviceOverflow,
}

/// Performs some basic tests on request to read/write a block to ensure it looks coherent.
/// Specifically:
///
///   * The size must be a multiple of `wire::VIRTIO_BLOCK_SECTOR_SIZE`.
///   * `sector` and `request_size` should not overflow `u64`
///   * `sector` and `request_size` should not overflow the device capcity.
fn check_request(request_size: u64, sector: Sector, capacity: Sector) -> Result<(), BlockError> {
    if request_size % wire::VIRTIO_BLOCK_SECTOR_SIZE != 0 {
        return Err(BlockError::UnalignedSize);
    }
    let sector_byte_offset = sector.to_bytes().ok_or(BlockError::IntegerOverflow)?;
    let last_byte_accessed =
        request_size.checked_add(sector_byte_offset).ok_or(BlockError::IntegerOverflow)?;
    if last_byte_accessed > capacity.to_bytes().ok_or(BlockError::IntegerOverflow)? {
        return Err(BlockError::DeviceOverflow);
    }
    Ok(())
}

/// Writes zeros to the entire guest memory range covered by `range`.
fn zero_range<'a>(range: &DeviceRange<'a>) {
    // SAFETY: range comes from a virtio descriptor and is guaranteed to be valid for the lifetime
    // 'a.
    unsafe {
        libc::memset(range.try_mut_ptr().unwrap(), 0, range.len());
    }
}

// If we are going to report a status we indicate to the driver that we've written all the bytes in
// the writable chain. This is because the status byte comes after the data descriptors.
//
// Relevant sections from Virtio 1.1:
//
//   Section 2.6.8.2 Device Requirements: The Virtqueue Used Ring
//
//   The device MUST write at least len bytes to descriptor, beginning at the first device-writable
//   buffer, prior to updating the used idx. The device MAY write more than len bytes to
//   descriptor.
//
//     Note: There are potential error cases where a device might not know what parts of the
//     buffers have been written. This is why len is permitted to be an underestimate: that’s
//     preferable to the driver believing that uninitialized memory has been overwritten when it
//     has not.
//
//   Section 2.6.8.3 Driver Requirements: The Virtqueue Used Ring
//
//   The driver MUST NOT make assumptions about data in device-writable buffers beyond the first
//   len bytes, and SHOULD ignore this data
//
// Together this means we need to mark the entire chain as written to return a status byte and we
// also need to make sure we've written all those bytes. Since it's possible we haven't written
// some of the bytes on failure, we'll explicitly zero out all the data buffers on error.
fn seek_to_status<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: &mut WritableChain<'a, 'b, N, M>,
) -> Result<(), anyhow::Error> {
    let mut remaining_bytes = chain.remaining()?.bytes;
    while remaining_bytes > 1 {
        // Unwrap here since we requested less than the number of remaining bytes.
        let range = chain.next_with_limit(remaining_bytes - 1).unwrap()?;
        zero_range(&range);
        chain.add_written(range.len() as u32);
        remaining_bytes = chain.remaining()?.bytes;
    }
    if remaining_bytes != 1 {
        return Err(anyhow!("Failed to locate status byte"));
    }
    Ok(())
}

/// Given a `WritableChain`, ignore all the writable descriptors up until the status byte.
///
/// Upon success `chain` will have a single byte writable descriptor left. The `status` is not
/// written and passed along unmodified in the return value.
fn writable_chain_error<'a, 'b, N: DriverNotify, M: DriverMem>(
    mut chain: WritableChain<'a, 'b, N, M>,
    status: wire::VirtioBlockStatus,
) -> Result<(WritableChain<'a, 'b, N, M>, wire::VirtioBlockStatus), anyhow::Error> {
    seek_to_status(&mut chain)?;
    Ok((chain, status))
}

/// Like `writable_chain_error` but also ignores any unused readable descriptors.
fn readable_chain_error<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: ReadableChain<'a, 'b, N, M>,
    status: wire::VirtioBlockStatus,
) -> Result<(WritableChain<'a, 'b, N, M>, wire::VirtioBlockStatus), anyhow::Error> {
    writable_chain_error(WritableChain::from_incomplete_readable(chain)?, status)
}

/// Reads a `wire::VirtioBlockHeader` from the chain.
fn read_header<'a, 'b, N: DriverNotify, M: DriverMem>(
    chain: &mut ReadableChain<'a, 'b, N, M>,
) -> Result<wire::VirtioBlockHeader, anyhow::Error> {
    let mut header_buf: [u8; std::mem::size_of::<wire::VirtioBlockHeader>()] =
        [0; std::mem::size_of::<wire::VirtioBlockHeader>()];
    chain.read_exact(&mut header_buf)?;
    // read_from should not fail since we've sized the buffer appropriately. Any failures here are
    // unexpected.
    Ok(wire::VirtioBlockHeader::read_from(header_buf.as_slice())
        .expect("Failed to deserialize VirtioBlockHeader."))
}

/// Note that AccessMode should be determined based on if the RO feature was offered to the device,
/// and not if the feature was negotiated. In other words, a driver must not be able to get write
/// access to a device by simply failing to confirm the RO feature.
#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum AccessMode {
    ReadOnly,
    ReadWrite,
}

impl AccessMode {
    pub fn is_read_only(&self) -> bool {
        match self {
            AccessMode::ReadOnly => true,
            AccessMode::ReadWrite => false,
        }
    }
}

impl From<BlockMode> for AccessMode {
    fn from(mode: BlockMode) -> Self {
        match mode {
            BlockMode::ReadOnly => AccessMode::ReadOnly,
            BlockMode::ReadWrite => AccessMode::ReadWrite,
            // Volatile write controls the backend used, but the device is still writable.
            BlockMode::VolatileWrite => AccessMode::ReadWrite,
        }
    }
}

pub struct BlockDevice {
    backend: Box<dyn BlockBackend>,
    device_attrs: DeviceAttrs,
    id: String,
    mode: AccessMode,
}

impl BlockDevice {
    pub async fn new(
        id: String,
        mode: AccessMode,
        backend: Box<dyn BlockBackend>,
    ) -> Result<Self, anyhow::Error> {
        // FIDL already enforces this upper bound.
        assert!(id.len() <= wire::VIRTIO_BLK_ID_LEN);
        Ok(Self { device_attrs: backend.get_attrs().await?, backend, id, mode })
    }

    /// Returns the cached `DeviceAttrs` for this device.
    pub fn attrs(&self) -> &DeviceAttrs {
        &self.device_attrs
    }

    /// Processes `chain` to completion.
    ///
    /// Where possible, errors will be reported to the driver using the status byte in the
    /// descriptor chain. In cases where an error is able to be reported to the driver,
    /// `process_chain` will return Ok(()).
    pub async fn process_chain<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), anyhow::Error> {
        let (mut chain, block_status) = match read_header(&mut chain) {
            Ok(header) => match header.request_type.get() {
                wire::VIRTIO_BLK_T_IN => self.read(header, chain).await?,
                wire::VIRTIO_BLK_T_OUT => self.write(header, chain).await?,
                wire::VIRTIO_BLK_T_FLUSH => self.flush(header, chain).await?,
                wire::VIRTIO_BLK_T_GET_ID => self.get_id(chain)?,
                _ => {
                    // If the command is unsupported we need to seek the chain to the final writable
                    // status byte.
                    readable_chain_error(chain, wire::VirtioBlockStatus::Unsupported)?
                }
            },
            // If we couldn't read the header (ex: not enough readable bytes in the chain), then
            // try to report an error.
            Err(_e) => readable_chain_error(chain, wire::VirtioBlockStatus::IoError)?,
        };

        // If we're here the chain should be coherent, meaning we should have a block status and
        // a single byte range left in the chain to write to.
        if chain.remaining()?.bytes != 1 {
            return Err(anyhow!(
                "Expected a single byte remaining in the chain; unable to report status"
            ));
        }

        // Now we just need to write the statatus to the correct spot in guest memory.
        let status_desciptor = chain
            .next()
            .transpose()?
            .ok_or_else(|| anyhow!("Unable to read the status byte from the descriptor"))?;
        // SAFETY: The status_ptr is valid for lifetime 'a. Alignment is verififed by try_mut_ptr.
        let status_ptr: *mut u8 = status_desciptor
            .try_mut_ptr()
            .ok_or_else(|| anyhow!("Unable to get a mutable pointer to the status byte"))?;
        unsafe {
            status_ptr.write(block_status.to_wire());
        }
        chain.add_written(1);
        Ok(())
    }

    async fn read<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        header: wire::VirtioBlockHeader,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(WritableChain<'a, 'b, N, M>, wire::VirtioBlockStatus), anyhow::Error> {
        // If there are extra readable bytes before the writable section of the chain, the request
        // is malformed.
        if chain.remaining()?.bytes != 0 {
            return readable_chain_error(chain, wire::VirtioBlockStatus::IoError);
        }

        let mut chain = WritableChain::from_readable(chain)?;
        let bytes_to_read = chain.remaining()?.bytes.checked_sub(1).unwrap();
        let sector = Sector::from_raw_sector(header.sector.get());
        if let Err(_e) = check_request(bytes_to_read as u64, sector, self.device_attrs.capacity) {
            // The request is malformed, so don't attempt to process it but do attempt to report
            // the failure back to the driver.
            return writable_chain_error(chain, wire::VirtioBlockStatus::IoError);
        }

        // The request appears valid so build a request. First we collect a set of device ranges
        // that represent the descriptors that the read data will be written to. This will be the
        // entire length of the chain minus 1 byte to reserve for the status.
        let mut bytes_to_read_remaining = bytes_to_read;
        let mut ranges: Vec<DeviceRange<'a>> = Vec::new();
        while let Some(range) = chain.next_with_limit(bytes_to_read_remaining).transpose()? {
            ranges.push(range);
            let remaining = chain.remaining()?.bytes;
            if remaining <= 1 {
                break;
            }
            bytes_to_read_remaining = remaining - 1;
        }

        // Dispatch the request to the backend.
        let request =
            Request::from_ref(ranges.as_slice(), Sector::from_raw_sector(header.sector.get()));

        let block_status = match self.backend.read(request).await {
            Err(_e) => wire::VirtioBlockStatus::IoError,
            Ok(()) => wire::VirtioBlockStatus::Ok,
        };

        // If there was an error, it will be undefined how much of the payload buffers have been
        // written to. Since we will report a status we need to make sure these buffers have
        // actually been written to.
        //
        // See `seek_to_status` for additional details.
        chain.add_written(bytes_to_read as u32);
        if block_status != wire::VirtioBlockStatus::Ok {
            ranges.iter().for_each(zero_range);
        }
        Ok((chain, block_status))
    }

    async fn write<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        header: wire::VirtioBlockHeader,
        mut chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(WritableChain<'a, 'b, N, M>, wire::VirtioBlockStatus), anyhow::Error> {
        if self.mode.is_read_only() {
            return readable_chain_error(chain, wire::VirtioBlockStatus::IoError);
        }
        let bytes_to_write = chain.remaining()?.bytes;
        let sector = Sector::from_raw_sector(header.sector.get());
        if let Err(_e) = check_request(bytes_to_write as u64, sector, self.device_attrs.capacity) {
            // The request is malformed, so don't attempt to process it.
            return readable_chain_error(chain, wire::VirtioBlockStatus::IoError);
        }

        // The request appears valid so now dispatch the request to the backend.
        let mut ranges: Vec<DeviceRange<'a>> = Vec::new();
        while let Some(range) = chain.next().transpose()? {
            ranges.push(range);
        }
        let request = Request::from_ref(ranges.as_slice(), sector);
        if let Err(_e) = self.backend.write(request).await {
            Ok((WritableChain::from_readable(chain)?, wire::VirtioBlockStatus::IoError))
        } else {
            Ok((WritableChain::from_readable(chain)?, wire::VirtioBlockStatus::Ok))
        }
    }

    async fn flush<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        header: wire::VirtioBlockHeader,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(WritableChain<'a, 'b, N, M>, wire::VirtioBlockStatus), anyhow::Error> {
        // Virtio 1.1, Section 5.2.6.1: A driver MUST set sector to 0 for a VIRTIO_BLK_T_FLUSH
        // request.
        if header.sector.get() != 0 {
            return readable_chain_error(chain, wire::VirtioBlockStatus::IoError);
        }

        // Virtio 1.1, Section 5.2.6.1: A driver SHOULD NOT include any data in a
        // VIRTIO_BLK_T_FLUSH request.
        //
        // Since this is a SHOULD NOT and not a MUST NOT we will accommodate data buffers. Using
        // from_incomplete_readable will allow output buffers and seek_to_status will zero out any
        // unused input buffers.
        let mut chain = WritableChain::from_incomplete_readable(chain)?;
        seek_to_status(&mut chain)?;

        if let Err(_e) = self.backend.flush().await {
            Ok((chain, wire::VirtioBlockStatus::IoError))
        } else {
            Ok((chain, wire::VirtioBlockStatus::Ok))
        }
    }

    fn get_id<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(WritableChain<'a, 'b, N, M>, wire::VirtioBlockStatus), anyhow::Error> {
        let mut chain = WritableChain::from_incomplete_readable(chain)?;

        // Section 5.2.6.1: The length of `data` MUST be 20 bytes for VIRTIO_BLK_T_GET_ID requests.
        if chain.remaining()?.bytes != wire::VIRTIO_BLK_ID_LEN + 1 {
            return writable_chain_error(chain, wire::VirtioBlockStatus::IoError);
        }

        // The device ID string is a NUL-padded ASCII string up to 20 bytes long. If the string is
        // 20 bytes long then there is no NUL terminator.
        let mut id_buf = [0u8; wire::VIRTIO_BLK_ID_LEN];

        // We truncated the id upon creation, so this should always be a valid size.
        let id_len = self.id.len();
        assert!(id_len <= wire::VIRTIO_BLK_ID_LEN);
        id_buf[..id_len].copy_from_slice(self.id.as_bytes());
        chain.write_all(&id_buf)?;

        // We already validated the size of the writable chain and we need to have written the
        // entire data buffer for the id (including null trailing bytes), so this should always
        // hold.
        assert!(chain.remaining()?.bytes == 1);
        Ok((chain, wire::VirtioBlockStatus::Ok))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::backend_test::BackendController,
        crate::memory_backend::MemoryBackend,
        crate::wire,
        fuchsia_async as fasync,
        virtio_device::chain::ReadableChain,
        virtio_device::fake_queue::{ChainBuilder, IdentityDriverMem, TestQueue},
    };

    #[test]
    fn test_check_request() -> Result<(), anyhow::Error> {
        // Happy cases
        check_request(
            wire::VIRTIO_BLOCK_SECTOR_SIZE,
            Sector::from_raw_sector(0),
            Sector::from_raw_sector(1),
        )?;
        check_request(
            wire::VIRTIO_BLOCK_SECTOR_SIZE,
            Sector::from_raw_sector(0),
            Sector::from_raw_sector(2),
        )?;
        check_request(
            wire::VIRTIO_BLOCK_SECTOR_SIZE,
            Sector::from_raw_sector(1),
            Sector::from_raw_sector(2),
        )?;
        check_request(wire::VIRTIO_BLOCK_SECTOR_SIZE, Sector::from_raw_sector(0), Sector::MAX)?;
        Ok(())
    }

    #[test]
    fn test_check_request_integer_overflow() {
        // The largest number of sectors, when expressed in bytes, that can fit within a u64.
        const MAX_SECTORS: u64 = (u64::MAX - (u64::MAX % wire::VIRTIO_BLOCK_SECTOR_SIZE))
            / wire::VIRTIO_BLOCK_SECTOR_SIZE;
        // The the largest sector, when expressed in bytes, that can be addressed with a u64.
        const LAST_SECTOR: u64 = MAX_SECTORS - 1;

        // First establish that LAST_SECTOR is valid to read in various ways.
        check_request(
            wire::VIRTIO_BLOCK_SECTOR_SIZE,
            Sector::from_raw_sector(LAST_SECTOR),
            Sector::MAX,
        )
        .unwrap();
        check_request(
            wire::VIRTIO_BLOCK_SECTOR_SIZE * 2,
            Sector::from_raw_sector(LAST_SECTOR),
            Sector::MAX,
        )
        .unwrap_err();
        check_request(
            wire::VIRTIO_BLOCK_SECTOR_SIZE,
            Sector::from_raw_sector(LAST_SECTOR + 1),
            Sector::MAX,
        )
        .unwrap_err();
        check_request(0, Sector::from_raw_sector(MAX_SECTORS), Sector::MAX).unwrap();

        // Overflow because MAX_SECTORS + 1 as bytes cannot be represented with a u64.
        assert_eq!(
            check_request(0, Sector::from_raw_sector(MAX_SECTORS + 1), Sector::MAX).unwrap_err(),
            BlockError::IntegerOverflow
        );

        // Overflow adding in the 1 sector length.
        assert_eq!(
            check_request(
                wire::VIRTIO_BLOCK_SECTOR_SIZE,
                Sector::from_raw_sector(MAX_SECTORS),
                Sector::MAX
            )
            .unwrap_err(),
            BlockError::IntegerOverflow
        );
    }

    #[test]
    fn test_check_request_device_overflow() {
        // Check the situation where the requested range overflows the device capacity.
        assert_eq!(
            check_request(
                wire::VIRTIO_BLOCK_SECTOR_SIZE,
                Sector::from_raw_sector(10),
                Sector::from_raw_sector(10)
            )
            .unwrap_err(),
            BlockError::DeviceOverflow
        );
        assert_eq!(
            check_request(
                2 * wire::VIRTIO_BLOCK_SECTOR_SIZE,
                Sector::from_raw_sector(9),
                Sector::from_raw_sector(10),
            )
            .unwrap_err(),
            BlockError::DeviceOverflow
        );
    }

    fn read_returned_range<'a>(_: &'a IdentityDriverMem, range: (u64, u32)) -> &'a [u8] {
        let (data, len) = range;
        unsafe { std::slice::from_raw_parts::<u8>(data as usize as *const u8, len as usize) }
    }

    fn check_returned_range(range: (u64, u32), color: u8) {
        let (data, len) = range;
        let slice =
            unsafe { std::slice::from_raw_parts::<u8>(data as usize as *const u8, len as usize) };
        slice.iter().for_each(|c| assert_eq!(*c, color));
    }

    fn check_returned_status(range: (u64, u32), expected: wire::VirtioBlockStatus) {
        let (data, len) = range;
        assert_eq!(1, len);
        let status = unsafe { *(data as usize as *mut u8) };
        assert_eq!(expected.to_wire(), status);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_simple() -> Result<(), anyhow::Error> {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioBlockHeader {
                            request_type: wire::LE32::new(wire::VIRTIO_BLK_T_IN),
                            reserved: wire::LE32::new(0),
                            sector: wire::LE64::new(0),
                        }),
                        &mem,
                    )
                    // Read 2 sectors into this descriptor
                    .writable(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32, &mem)
                    .writable(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32, &mem)
                    // Status byte
                    .writable(1, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let (backend, mut controller) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;

        // Fill some sectors in the backend.
        controller.color_sector(Sector::from_raw_sector(0), 0xaa)?;
        controller.color_sector(Sector::from_raw_sector(1), 0xbb)?;

        // Process the request.
        device.process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem)).await?;

        // Validate returned chain. We expect 2 data descriptors followed by an OK status.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(2 * wire::VIRTIO_BLOCK_SECTOR_SIZE + 1, returned.written() as u64);

        let mut iter = returned.data_iter();
        check_returned_range(iter.next().unwrap(), 0xaa);
        check_returned_range(iter.next().unwrap(), 0xbb);
        check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::Ok);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_unaligned_sector() -> Result<(), anyhow::Error> {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioBlockHeader {
                            request_type: wire::LE32::new(wire::VIRTIO_BLK_T_IN),
                            reserved: wire::LE32::new(0),
                            sector: wire::LE64::new(0),
                        }),
                        &mem,
                    )
                    // Read a half sector (illegal)
                    .writable(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32 / 2, &mem)
                    // Status byte
                    .writable(1, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let (backend, mut controller) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;

        // Fill some sectors in the backend.
        controller.color_sector(Sector::from_raw_sector(0), 0xaa)?;
        controller.color_sector(Sector::from_raw_sector(1), 0xbb)?;

        // Process the request.
        device.process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem)).await?;

        // Validate returned chain. We expect 2 data descriptors followed by an OK status.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(wire::VIRTIO_BLOCK_SECTOR_SIZE / 2 + 1, returned.written() as u64);

        // The data must be zero'd out and the status will indicate error.
        let mut iter = returned.data_iter();
        check_returned_range(iter.next().unwrap(), 0x00);
        check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::IoError);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_extra_readable_descriptors() -> Result<(), anyhow::Error> {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioBlockHeader {
                            request_type: wire::LE32::new(wire::VIRTIO_BLK_T_IN),
                            reserved: wire::LE32::new(0),
                            sector: wire::LE64::new(0),
                        }),
                        &mem,
                    )
                    // Add some reaable bytes here. This makes the request invalid since the
                    // writable descriptors should immediately follow the header.
                    .readable_zeroed(32, &mem)
                    // Read 2 sectors into this descriptor
                    .writable(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32, &mem)
                    // Status byte
                    .writable(1, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let (backend, _) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;

        // Process the request.
        device.process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem)).await?;

        // Validate returned chain. We should have reported all the writable buffers have been
        // written, with an IoError status.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(wire::VIRTIO_BLOCK_SECTOR_SIZE + 1, returned.written() as u64);

        let mut iter = returned.data_iter();
        check_returned_range(iter.next().unwrap(), 0x00);
        check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::IoError);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_simple() -> Result<(), anyhow::Error> {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);

        // Prepare the queue with a request to write 2 sectors starting at sector 10.
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioBlockHeader {
                            request_type: wire::LE32::new(wire::VIRTIO_BLK_T_OUT),
                            reserved: wire::LE32::new(0),
                            sector: wire::LE64::new(10),
                        }),
                        &mem,
                    )
                    // Write 2 sectors
                    .readable(&[0xaau8; wire::VIRTIO_BLOCK_SECTOR_SIZE as usize], &mem)
                    .readable(&[0xbbu8; wire::VIRTIO_BLOCK_SECTOR_SIZE as usize], &mem)
                    // Status byte
                    .writable(1, &mem)
                    .build(),
            )
            .unwrap();

        // Process the request.
        let (backend, mut controller) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;
        device.process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem)).await?;

        // Validate returned chain. We should only have a single status byte written.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(1, returned.written() as u64);
        let mut iter = returned.data_iter();
        check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::Ok);

        // Verify the data was written to the backend.
        controller.check_sector(Sector::from_raw_sector(10), 0xaa)?;
        controller.check_sector(Sector::from_raw_sector(11), 0xbb)?;
        // Sectors immediately before and immediately after should not be touched.
        controller.check_sector(Sector::from_raw_sector(9), 0x00)?;
        controller.check_sector(Sector::from_raw_sector(12), 0x00)?;
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_unaligned_sector() -> Result<(), anyhow::Error> {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioBlockHeader {
                            request_type: wire::LE32::new(wire::VIRTIO_BLK_T_OUT),
                            reserved: wire::LE32::new(0),
                            sector: wire::LE64::new(0),
                        }),
                        &mem,
                    )
                    // Write a half sector (illegal)
                    .readable(&[0xaau8; wire::VIRTIO_BLOCK_SECTOR_SIZE as usize / 2], &mem)
                    // Status byte
                    .writable(1, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let (backend, _) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;

        // Process the request.
        device.process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem)).await?;

        // Validate returned chain. We expect a single status byte indicating error.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(1, returned.written() as u64);
        let mut iter = returned.data_iter();
        check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::IoError);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unsupported_operation() -> Result<(), anyhow::Error> {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioBlockHeader {
                            // Invalid
                            request_type: wire::LE32::new(u32::MAX),
                            reserved: wire::LE32::new(0),
                            sector: wire::LE64::new(0),
                        }),
                        &mem,
                    )
                    // Add some unused descriptors. This is to test that there could be anundefined
                    // amount of readable/writable buffer space before the status byte for unknown
                    // commands.
                    .readable_zeroed(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32, &mem)
                    .writable(wire::VIRTIO_BLOCK_SECTOR_SIZE as u32, &mem)
                    // Status byte
                    .writable(1, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let (backend, _) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;

        // Process the request.
        device.process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem)).await?;

        // Validate returned chain. We expect 2 data descriptors followed by an OK status.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(wire::VIRTIO_BLOCK_SECTOR_SIZE + 1, returned.written() as u64);

        // The data must be zero'd out and the status will indicate error.
        let mut iter = returned.data_iter();
        check_returned_range(iter.next().unwrap(), 0x00);
        check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::Unsupported);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_id() -> Result<(), anyhow::Error> {
        let mem = IdentityDriverMem::new();
        let mut state = TestQueue::new(32, &mem);
        state
            .fake_queue
            .publish(
                ChainBuilder::new()
                    // Header
                    .readable(
                        std::slice::from_ref(&wire::VirtioBlockHeader {
                            request_type: wire::LE32::new(wire::VIRTIO_BLK_T_GET_ID),
                            reserved: wire::LE32::new(0),
                            sector: wire::LE64::new(0),
                        }),
                        &mem,
                    )
                    .writable(wire::VIRTIO_BLK_ID_LEN as u32, &mem)
                    // Status byte
                    .writable(1, &mem)
                    .build(),
            )
            .unwrap();

        // Process the chain.
        let (backend, _) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;

        // Process the request.
        device.process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem)).await?;

        // Validate returned chain.
        let returned = state.fake_queue.next_used().unwrap();
        assert_eq!(wire::VIRTIO_BLK_ID_LEN + 1, returned.written() as usize);

        // ID should be returned with null bytes filling out the remaining bytes.
        let mut iter = returned.data_iter();
        let actual_id = read_returned_range(&mem, iter.next().unwrap());
        let expected_id: [u8; wire::VIRTIO_BLK_ID_LEN] = [
            b'd', b'e', b'v', b'i', b'c', b'e', b'-', b'i', b'd', b'\0', b'\0', b'\0', b'\0',
            b'\0', b'\0', b'\0', b'\0', b'\0', b'\0', b'\0',
        ];
        assert_eq!(actual_id, expected_id);
        check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::Ok);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_id_short_buffer() -> Result<(), anyhow::Error> {
        let (backend, _) = MemoryBackend::new();
        let device =
            BlockDevice::new("device-id".to_string(), AccessMode::ReadWrite, Box::new(backend))
                .await?;
        for buf_size in [wire::VIRTIO_BLK_ID_LEN as u32 - 1, wire::VIRTIO_BLK_ID_LEN as u32 + 1] {
            let mem = IdentityDriverMem::new();
            let mut state = TestQueue::new(32, &mem);
            state
                .fake_queue
                .publish(
                    ChainBuilder::new()
                        // Header
                        .readable(
                            std::slice::from_ref(&wire::VirtioBlockHeader {
                                request_type: wire::LE32::new(wire::VIRTIO_BLK_T_GET_ID),
                                reserved: wire::LE32::new(0),
                                sector: wire::LE64::new(0),
                            }),
                            &mem,
                        )
                        .writable(buf_size, &mem)
                        // Status byte
                        .writable(1, &mem)
                        .build(),
                )
                .unwrap();

            // Process the request.
            device
                .process_chain(ReadableChain::new(state.queue.next_chain().unwrap(), &mem))
                .await?;

            // Validate returned chain.
            let returned = state.fake_queue.next_used().unwrap();
            assert_eq!(buf_size + 1, returned.written());

            // Verify failure.
            let mut iter = returned.data_iter();
            check_returned_range(iter.next().unwrap(), 0);
            check_returned_status(iter.next().unwrap(), wire::VirtioBlockStatus::IoError);
        }
        Ok(())
    }
}
