// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{BufferSlice, MutableBufferSlice, RemoteBlockDevice, VmoId},
    anyhow::{ensure, Error},
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    futures::executor::block_on,
    linked_hash_map::LinkedHashMap,
    std::io::SeekFrom,
    std::io::Write,
};

const VMO_SIZE: u64 = 262_144;
const BLOCK_SIZE: u64 = 8192;
const BLOCK_COUNT: usize = (VMO_SIZE / BLOCK_SIZE) as usize;

struct CacheEntry {
    vmo_offset: u64,
    dirty: bool,
}

#[derive(Debug, Default, Eq, PartialEq)]
pub struct Stats {
    read_count: u64,
    write_count: u64,
    cache_hits: u64,
}

/// Wraps RemoteBlockDevice providing a simple LRU cache and trait implementations for
/// std::io::{Read, Seek, Write}. This is unlikely to be performant; the implementation is single
/// threaded. The cache works by dividing up a VMO into BLOCK_COUNT blocks of BLOCK_SIZE bytes, and
/// maintaining mappings from device offsets to offsets in the VMO.
pub struct Cache {
    device: RemoteBlockDevice,
    vmo: zx::Vmo,
    vmo_id: VmoId,
    map: LinkedHashMap<u64, CacheEntry>,
    offset: u64, // For std::io::{Read, Seek, Write}
    stats: Stats,
}

impl Cache {
    /// Returns a new Cache wrapping the given RemoteBlockDevice.
    pub fn new(device: RemoteBlockDevice) -> Result<Self, Error> {
        ensure!(BLOCK_SIZE % device.block_size as u64 == 0, "underlying block size not supported");
        let vmo = zx::Vmo::create(VMO_SIZE)?;
        let vmo_id = device.attach_vmo(&vmo)?;
        Ok(Cache {
            device,
            vmo,
            vmo_id,
            map: Default::default(),
            offset: 0,
            stats: Stats::default(),
        })
    }

    fn device_size(&self) -> u64 {
        self.device.block_count * self.device.block_size as u64
    }

    // Finds a block that can be used for the given offset, marking dirty if requested. Returns a
    // tuple with the VMO offset and whether it was a cache hit. If not a cache hit, the caller is
    // responsible for initializing the data and inserting a cache entry.
    fn get_block(&mut self, offset: u64, mark_dirty: bool) -> Result<(u64, bool), Error> {
        if let Some(ref mut entry) = self.map.get_refresh(&offset) {
            self.stats.cache_hits += 1;
            if mark_dirty {
                entry.dirty = true;
            }
            Ok((entry.vmo_offset, true))
        } else {
            let vmo_offset = if self.map.len() < BLOCK_COUNT {
                self.map.len() as u64 * BLOCK_SIZE
            } else {
                let entry = self.map.pop_front().unwrap();
                if entry.1.dirty {
                    self.stats.write_count += 1;
                    block_on(self.device.write_at(
                        BufferSlice::new_with_vmo_id(
                            &self.vmo_id,
                            entry.1.vmo_offset,
                            std::cmp::min(BLOCK_SIZE, self.device_size() - entry.0),
                        ),
                        entry.0,
                    ))?;
                }
                entry.1.vmo_offset
            };
            Ok((vmo_offset, false))
        }
    }

    // Reads the block at the given offset and marks it dirty if requested. Returns the offset in
    // the VMO.
    fn read_block(&mut self, offset: u64, mark_dirty: bool) -> Result<u64, Error> {
        let (vmo_offset, hit) = self.get_block(offset, mark_dirty)?;
        if !hit {
            self.stats.read_count += 1;
            block_on(self.device.read_at(
                MutableBufferSlice::new_with_vmo_id(
                    &self.vmo_id,
                    vmo_offset,
                    std::cmp::min(BLOCK_SIZE, self.device_size() - offset),
                ),
                offset,
            ))?;
            self.map.insert(offset, CacheEntry { vmo_offset, dirty: mark_dirty });
        }
        Ok(vmo_offset)
    }

    /// Reads at |offset| into |buf|.
    pub fn read_at(&mut self, mut buf: &mut [u8], offset: u64) -> Result<(), Error> {
        ensure!(
            offset <= self.device_size() && buf.len() as u64 <= self.device_size() - offset,
            "read exceeds device size"
        );

        // Start by reading the head.
        let mut aligned_offset = offset - offset % BLOCK_SIZE;
        let end = offset + buf.len() as u64;
        if aligned_offset < offset {
            let vmo_offset = self.read_block(aligned_offset, false)?;
            let to_copy = std::cmp::min(aligned_offset + BLOCK_SIZE, end) - offset;
            self.vmo.read(&mut buf[..to_copy as usize], vmo_offset + offset - aligned_offset)?;
            aligned_offset += BLOCK_SIZE;
            buf = &mut buf[to_copy as usize..];
        }

        // Now do whole blocks.
        while aligned_offset + BLOCK_SIZE <= end {
            let vmo_offset = self.read_block(aligned_offset, false)?;
            self.vmo.read(&mut buf[..BLOCK_SIZE as usize], vmo_offset)?;
            aligned_offset += BLOCK_SIZE;
            buf = &mut buf[BLOCK_SIZE as usize..];
        }

        // And finally the tail.
        if end > aligned_offset {
            let vmo_offset = self.read_block(aligned_offset, false)?;
            self.vmo.read(buf, vmo_offset)?;
        }
        Ok(())
    }

    /// Writes from |buf| to |offset|.
    pub fn write_at(&mut self, mut buf: &[u8], offset: u64) -> Result<(), Error> {
        ensure!(
            offset <= self.device_size() && buf.len() as u64 <= self.device_size() - offset,
            "write exceeds device size"
        );

        // Start by writing the head.
        let mut aligned_offset = offset - offset % BLOCK_SIZE;
        let end = offset + buf.len() as u64;
        if aligned_offset < offset {
            let vmo_offset = self.read_block(aligned_offset, true)?;
            let to_copy = std::cmp::min(aligned_offset + BLOCK_SIZE, end) - offset;
            self.vmo.write(&buf[..to_copy as usize], vmo_offset + offset - aligned_offset)?;
            aligned_offset += BLOCK_SIZE;
            buf = &buf[to_copy as usize..];
        }

        // Now do whole blocks.
        while aligned_offset + BLOCK_SIZE <= end {
            let (vmo_offset, hit) = self.get_block(aligned_offset, true)?;
            self.vmo.write(&buf[..BLOCK_SIZE as usize], vmo_offset)?;
            if !hit {
                self.map.insert(aligned_offset, CacheEntry { vmo_offset, dirty: true });
            }
            aligned_offset += BLOCK_SIZE;
            buf = &buf[BLOCK_SIZE as usize..];
        }

        // And finally the tail.
        if end > aligned_offset {
            let vmo_offset = self.read_block(aligned_offset, true)?;
            self.vmo.write(buf, vmo_offset)?;
        }
        Ok(())
    }

    /// Returns statistics.
    pub fn stats(&self) -> &Stats {
        &self.stats
    }

    /// Returns a reference to the underlying device
    /// Can be used for additional control, like instructing the device to flush any written data
    pub fn device(&self) -> &RemoteBlockDevice {
        &self.device
    }
}

impl Drop for Cache {
    fn drop(&mut self) {
        if let Err(e) = self.flush() {
            fx_log_err!("Flush failed: {}", e);
        }
        self.vmo_id.take().into_id(); // Ok to leak because fifo will be closed.
    }
}

fn into_io_error(error: Error) -> std::io::Error {
    std::io::Error::new(std::io::ErrorKind::Other, error)
}

impl std::io::Read for Cache {
    fn read(&mut self, mut buf: &mut [u8]) -> std::io::Result<usize> {
        if self.offset > self.device_size() {
            return Ok(0);
        }
        let max_len = self.device_size() - self.offset;
        if buf.len() as u64 > max_len {
            buf = &mut buf[0..max_len as usize];
        }
        self.read_at(buf, self.offset).map_err(into_io_error)?;
        self.offset += buf.len() as u64;
        Ok(buf.len())
    }
}

impl Write for Cache {
    fn write(&mut self, mut buf: &[u8]) -> std::io::Result<usize> {
        if self.offset > self.device_size() {
            return Ok(0);
        }
        let max_len = self.device_size() - self.offset;
        if buf.len() as u64 > max_len {
            buf = &buf[0..max_len as usize];
        }
        self.write_at(&buf, self.offset).map_err(into_io_error)?;
        self.offset += buf.len() as u64;
        Ok(buf.len())
    }

    /// This does *not* issue a flush to the underlying block device; this will only send the
    /// writes.
    fn flush(&mut self) -> std::io::Result<()> {
        let max = self.device_size();
        for mut entry in self.map.entries() {
            if entry.get().dirty {
                self.stats.write_count += 1;
                block_on(self.device.write_at(
                    BufferSlice::new_with_vmo_id(
                        &self.vmo_id,
                        entry.get().vmo_offset,
                        std::cmp::min(BLOCK_SIZE, max - *entry.key()),
                    ),
                    *entry.key(),
                ))
                .map_err(into_io_error)?;
                entry.get_mut().dirty = false;
            }
        }
        Ok(())
    }
}

impl std::io::Seek for Cache {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        self.offset = match pos {
            SeekFrom::Start(offset) => Some(offset),
            SeekFrom::End(delta) => {
                if delta >= 0 {
                    self.device_size().checked_add(delta as u64)
                } else {
                    self.device_size().checked_sub(-delta as u64)
                }
            }
            SeekFrom::Current(delta) => {
                if delta >= 0 {
                    self.offset.checked_add(delta as u64)
                } else {
                    self.offset.checked_sub(-delta as u64)
                }
            }
        }
        .ok_or(std::io::Error::new(std::io::ErrorKind::InvalidInput, "bad delta"))?;
        Ok(self.offset)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{Cache, Stats},
        crate::RemoteBlockDevice,
        ramdevice_client::RamdiskClient,
        std::io::{Read, Seek, SeekFrom, Write},
    };

    const RAMDISK_BLOCK_SIZE: u64 = 1024;
    const RAMDISK_BLOCK_COUNT: u64 = 1023; // Deliberate for testing max offset.
    const RAMDISK_SIZE: u64 = RAMDISK_BLOCK_SIZE * RAMDISK_BLOCK_COUNT;

    pub fn make_ramdisk() -> (RamdiskClient, RemoteBlockDevice) {
        isolated_driver_manager::launch_isolated_driver_manager()
            .expect("launch_isolated_driver_manager failed");
        ramdevice_client::wait_for_device("/dev/misc/ramctl", std::time::Duration::from_secs(10))
            .expect("ramctl did not appear");
        let ramdisk = RamdiskClient::create(RAMDISK_BLOCK_SIZE, RAMDISK_BLOCK_COUNT)
            .expect("RamdiskClient::create failed");
        let remote_block_device =
            RemoteBlockDevice::new_sync(ramdisk.open().expect("ramdisk.open failed"))
                .expect("RemoteBlockDevice::new_sync failed");
        (ramdisk, remote_block_device)
    }

    #[test]
    fn test_cache_read_at_and_write_at_with_no_hits() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        let mut offset = 5;
        const TEST_COUNT: usize = super::BLOCK_COUNT * 2; // Chosen so there are no cache hits.
        const DATA: &[u8] = b"hello";
        for _ in 0..TEST_COUNT {
            cache.write_at(DATA, offset).expect("cache.write failed");
            // The delta here is deliberately chosen to catch mistakes such as returning data from
            // the wrong block.
            offset += super::BLOCK_SIZE + 1;
        }
        assert_eq!(
            cache.stats(),
            &Stats {
                read_count: TEST_COUNT as u64,
                write_count: super::BLOCK_COUNT as u64,
                cache_hits: 0
            }
        );
        offset = 5;
        for _ in 0..TEST_COUNT {
            let mut buf = [0; 5];
            cache.read_at(&mut buf, offset).expect("cache.read_at failed");
            assert_eq!(&buf, DATA);
            offset += super::BLOCK_SIZE + 1;
        }
        assert_eq!(
            cache.stats(),
            &Stats {
                read_count: 2 * TEST_COUNT as u64,
                write_count: TEST_COUNT as u64,
                cache_hits: 0
            }
        );
    }

    #[test]
    fn test_cache_read_at_and_write_at_with_hit() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        const OFFSET: u64 = 11;
        const DATA: &[u8] = b"hello";
        cache.write_at(DATA, OFFSET).expect("cache.write failed");
        let mut buf = [0; 5];
        cache.read_at(&mut buf, OFFSET).expect("cache.read_at failed");
        assert_eq!(&buf, DATA);
        assert_eq!(cache.stats(), &Stats { read_count: 1, write_count: 0, cache_hits: 1 });
    }

    #[test]
    fn test_cache_aligned_read_at_and_write_at() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        const OFFSET: u64 = 11;
        const BLOCKS: usize = 3;
        const DATA_LEN: usize = super::BLOCK_SIZE as usize * BLOCKS + 11;
        let data = [0xe2u8; DATA_LEN];
        // This should require alignment at the start, and at the end with some whole blocks.
        cache.write_at(&data, OFFSET).expect("cache.write failed");
        let mut buf = [0; DATA_LEN + 2]; // Read an extra byte at the start and at the end.
        cache.read_at(&mut buf, OFFSET - 1).expect("cache.read_at failed");
        assert_eq!(buf[0], 0);
        assert_eq!(buf[DATA_LEN + 1], 0);
        assert_eq!(&buf[1..DATA_LEN + 1], &data[0..DATA_LEN]);
        // We should have only read the first and last blocks. The writes to the whole blocks should
        // not have triggered reads.
        assert_eq!(
            cache.stats(),
            &Stats { read_count: 2, write_count: 0, cache_hits: BLOCKS as u64 + 1 }
        );
    }

    #[test]
    fn test_cache_aligned_read_at_and_write_at_cold() {
        // The same as the previous test, but tear down the cache after the writes.
        let (ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        const OFFSET: u64 = 11;
        const BLOCKS: usize = 3;
        const DATA_LEN: usize = super::BLOCK_SIZE as usize * BLOCKS + 11;
        let data = [0xe2u8; DATA_LEN];
        // This should require alignment at the start, and at the end with some whole blocks.
        cache.write_at(&data, OFFSET).expect("cache.write failed");
        assert_eq!(cache.stats(), &Stats { read_count: 2, write_count: 0, cache_hits: 0 });

        drop(cache);
        let mut cache = Cache::new(
            RemoteBlockDevice::new_sync(ramdisk.open().expect("ramdisk.open failed"))
                .expect("RemoteBlockDevice::new_sync failed"),
        )
        .expect("Cache::new failed");

        let mut buf = [0; DATA_LEN + 2]; // Read an extra byte at the start and at the end.
        cache.read_at(&mut buf, OFFSET - 1).expect("cache.read_at failed");
        assert_eq!(buf[0], 0);
        assert_eq!(buf[DATA_LEN + 1], 0);
        assert_eq!(&buf[1..DATA_LEN + 1], &data[0..DATA_LEN]);
        // We should have only read the first and last blocks. The writes to the whole blocks should
        // not have triggered reads.
        assert_eq!(
            cache.stats(),
            &Stats { read_count: BLOCKS as u64 + 1, write_count: 0, cache_hits: 0 }
        );
    }

    #[test]
    fn test_io_read_write_and_seek() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        const OFFSET: u64 = 11;
        const DATA: &[u8] = b"hello";
        assert_eq!(cache.seek(SeekFrom::Start(OFFSET)).expect("seek failed"), OFFSET);
        cache.write(DATA).expect("cache.write failed");
        assert_eq!(
            cache.seek(SeekFrom::Current(-(DATA.len() as i64))).expect("seek failed"),
            OFFSET
        );
        let mut buf = [0u8; 5];
        assert_eq!(cache.read(&mut buf).expect("cache.read failed"), DATA.len());
        assert_eq!(&buf, DATA);
    }

    #[test]
    fn test_io_read_write_and_seek_at_max_offset() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        const DATA: &[u8] = b"hello";
        assert_eq!(cache.seek(SeekFrom::End(-1)).expect("seek failed"), RAMDISK_SIZE - 1);
        assert_eq!(cache.write(DATA).expect("cache.write failed"), 1);
        assert_eq!(cache.seek(SeekFrom::End(-4)).expect("seek failed"), RAMDISK_SIZE - 4);
        let mut buf = [0x56u8; 5];
        assert_eq!(cache.read(&mut buf).expect("cache.read failed"), 4);
        assert_eq!(&buf, &[0, 0, 0, b'h', 0x56]);
    }

    #[test]
    fn test_read_beyond_max_offset_returns_error() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        let mut buf = [0u8; 2];
        cache.read_at(&mut buf, RAMDISK_SIZE).expect_err("read_at succeeded");
        cache.read_at(&mut buf, RAMDISK_SIZE - 1).expect_err("read_at succeeded");
    }

    #[test]
    fn test_write_beyond_max_offset_returns_error() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        let buf = [0u8; 2];
        cache.write_at(&buf, RAMDISK_SIZE).expect_err("write_at succeeded");
        cache.write_at(&buf, RAMDISK_SIZE - 1).expect_err("write_at succeeded");
    }

    #[test]
    fn test_read_with_overflow_returns_error() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        let mut buf = [0u8; 2];
        cache.read_at(&mut buf, u64::MAX - 1).expect_err("read_at succeeded");
    }

    #[test]
    fn test_write_with_overflow_returns_error() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        let buf = [0u8; 2];
        cache.write_at(&buf, u64::MAX - 1).expect_err("write_at succeeded");
    }

    #[test]
    fn test_read_and_write_at_max_offset_suceeds() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        let buf = [0xd4u8; 2];
        cache.write_at(&buf, RAMDISK_SIZE - buf.len() as u64).expect("write_at failed");
        let mut read_buf = [0xf3u8; 2];
        cache.read_at(&mut read_buf, RAMDISK_SIZE - buf.len() as u64).expect("read_at failed");
        assert_eq!(&buf, &read_buf);
    }

    #[test]
    fn test_seek_with_bad_delta_returns_error() {
        let (_ramdisk, remote_block_device) = make_ramdisk();
        let mut cache = Cache::new(remote_block_device).expect("Cache::new failed");
        cache.seek(SeekFrom::End(-(RAMDISK_SIZE as i64) - 1)).expect_err("seek suceeded");
        cache.seek(SeekFrom::Current(-1)).expect_err("seek succeeded");
    }

    #[test]
    fn test_ramdisk_with_large_block_size_returns_error() {
        isolated_driver_manager::launch_isolated_driver_manager()
            .expect("launch_isolated_driver_manager failed");
        ramdevice_client::wait_for_device("/dev/misc/ramctl", std::time::Duration::from_secs(10))
            .expect("ramctl did not appear");
        let ramdisk =
            RamdiskClient::create(super::BLOCK_SIZE * 2, 10).expect("RamdiskClient::create failed");
        let remote_block_device =
            RemoteBlockDevice::new_sync(ramdisk.open().expect("ramdisk.open failed"))
                .expect("RemoteBlockDevice::new_sync failed");
        Cache::new(remote_block_device).err().expect("Cache::new succeeded");
    }
}
