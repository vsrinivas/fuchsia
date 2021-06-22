// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    fmt::Debug,
    io::{Read, Result, Seek, SeekFrom, Write},
};

pub trait FileLike: Read + Write + Seek + Sized + Debug {}

impl<T: Read + Write + Seek + Sized + Debug> FileLike for T {}

#[derive(Debug)]
/// Naive wrapper that aligns all reads and writes to be block-sized.
/// This means that a write involves a read (to get current content of the device) followed by a
/// write (with the relevant ranges overwritten).
pub struct WrappedBlockDevice<T: FileLike> {
    inner: T,
    cur_pos: u64,
    block_size: u64,
}

fn round_down(val: u64, divisor: u64) -> u64 {
    (val / divisor) * divisor
}

fn round_up(val: u64, divisor: u64) -> u64 {
    ((val + (divisor - 1)) / divisor) * divisor
}

impl<T: FileLike> WrappedBlockDevice<T> {
    pub fn new(file: T, block_size: u64) -> Self {
        WrappedBlockDevice { inner: file, cur_pos: 0, block_size }
    }
}

impl<T: FileLike> Read for WrappedBlockDevice<T> {
    fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
        let start = self.cur_pos;
        let end = self.cur_pos + buf.len() as u64;

        let rounded_start = round_down(start, self.block_size);
        let rounded_end = round_up(end, self.block_size);

        // Read in full blocks around what was read.
        let mut real_buf = vec![0; (rounded_end - rounded_start) as usize];
        self.inner.seek(SeekFrom::Start(rounded_start))?;
        let read_result = self.inner.read(&mut real_buf)?;

        // Copy only what was asked for.
        let offset = (start - rounded_start) as usize;
        let end = std::cmp::min(offset + buf.len(), read_result);
        buf.copy_from_slice(&real_buf[offset..end]);

        self.cur_pos += end as u64;
        let bytes_read = end - offset;
        Ok(bytes_read)
    }
}

impl<T: FileLike> Write for WrappedBlockDevice<T> {
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        let start = self.cur_pos;
        let end = self.cur_pos + buf.len() as u64;
        let rounded_start = round_down(start, self.block_size);
        let rounded_end = round_up(end, self.block_size);

        // Read in full blocks around the area to be written.
        let mut real_buf = vec![0; (rounded_end - rounded_start) as usize];
        self.inner.seek(SeekFrom::Start(rounded_start))?;
        let read_result = self.inner.read(&mut real_buf)?;

        let offset = (start - rounded_start) as usize;
        // Chop off anything that goes beyond the bounds of the block device,
        // and update our copy of what's going to be written.
        let end = std::cmp::min(offset + buf.len(), read_result);
        real_buf[offset..end].copy_from_slice(buf);
        // Write out full blocks to the disk.
        self.inner.seek(SeekFrom::Start(rounded_start))?;
        self.inner.write(&real_buf)?;

        let bytes_written = end - offset;
        self.cur_pos += bytes_written as u64;

        Ok(bytes_written)
    }

    fn flush(&mut self) -> Result<()> {
        self.inner.flush()
    }
}

impl<T: FileLike> Seek for WrappedBlockDevice<T> {
    fn seek(&mut self, pos: SeekFrom) -> Result<u64> {
        let result = self.inner.seek(pos)?;
        self.cur_pos = result;
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_round_down() {
        assert_eq!(round_down(25, 16), 16);
        assert_eq!(round_down(768, 512), 512);
        assert_eq!(round_down(513, 512), 512);
        assert_eq!(round_down(512, 512), 512);
        assert_eq!(round_down(511, 512), 0);
    }

    #[test]
    fn test_round_up() {
        assert_eq!(round_up(25, 16), 32);
        assert_eq!(round_up(657, 512), 1024);
        assert_eq!(round_up(511, 512), 512);
        assert_eq!(round_up(512, 512), 512);
        assert_eq!(round_up(25, 16), 32);
        assert_eq!(round_up(25, 16), 32);
    }

    #[derive(Debug)]
    /// Wrapper around Vec that requires all reads/writes to happen on
    /// block_size alignments and block_size multiples.
    struct ExactDisk {
        inner: Vec<u8>,
        pos: u64,
        block_size: u64,
    }

    impl Read for ExactDisk {
        fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
            assert_eq!(self.pos % self.block_size, 0);
            assert_eq!(buf.len() % self.block_size as usize, 0);

            let pos = self.pos as usize;
            buf.copy_from_slice(&self.inner[pos..pos + buf.len()]);
            self.pos += buf.len() as u64;
            Ok(buf.len())
        }
    }

    impl Write for ExactDisk {
        fn write(&mut self, buf: &[u8]) -> Result<usize> {
            assert_eq!(self.pos % self.block_size, 0);
            assert_eq!(buf.len() % self.block_size as usize, 0);

            let pos = self.pos as usize;
            self.inner[pos..pos + buf.len()].copy_from_slice(buf);

            self.pos += buf.len() as u64;

            Ok(buf.len())
        }

        fn flush(&mut self) -> Result<()> {
            Ok(())
        }
    }

    impl Seek for ExactDisk {
        fn seek(&mut self, pos: SeekFrom) -> Result<u64> {
            match pos {
                SeekFrom::Start(val) => {
                    self.pos = val;
                }
                SeekFrom::Current(val) => {
                    self.pos = (self.pos as i64 + val) as u64;
                }
                SeekFrom::End(val) => self.pos = (self.inner.len() as i64 + val) as u64,
            }
            Ok(self.pos)
        }
    }

    impl ExactDisk {
        fn new(inner: Vec<u8>, block_size: u64) -> Self {
            ExactDisk { inner, pos: 0, block_size }
        }
    }

    #[test]
    fn test_block_read() {
        let disk = ExactDisk::new(vec![0, 0, 1, 1, 3, 3, 4, 4], 4);
        let mut wrapper = WrappedBlockDevice::new(disk, 4);
        let mut buf = [0; 2];
        wrapper.read(&mut buf).unwrap();
        assert_eq!(buf, [0, 0]);
        let mut buf = [0; 6];
        wrapper.read(&mut buf).unwrap();
        assert_eq!(buf, [1, 1, 3, 3, 4, 4]);
    }

    #[test]
    fn test_block_write_seek() {
        let disk = ExactDisk::new(vec![0, 0, 1, 1, 3, 3, 4, 4], 4);
        let mut wrapper = WrappedBlockDevice::new(disk, 4);
        let mut buf = [4; 2];
        let len = wrapper.write(&buf).unwrap();
        assert_eq!(len, 2);
        wrapper.seek(SeekFrom::Start(0)).unwrap();
        wrapper.read(&mut buf).unwrap();
        assert_eq!(buf, [4, 4]);
    }
}
