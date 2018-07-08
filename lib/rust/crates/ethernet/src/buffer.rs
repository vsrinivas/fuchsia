// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use shared_buffer::SharedBuffer;
use std::fmt;
use std::sync::{Arc, Mutex};
use sys;
use zx;

fn fifo_entry(offset: u32, length: u16) -> sys::eth_fifo_entry {
    sys::eth_fifo_entry {
        offset,
        length,
        flags: 0,
        cookie: 0,
    }
}

pub struct RxBuffer {
    data: SharedBuffer,
    offset: usize,
    buflist: Arc<Mutex<BufferMap>>,
}

impl RxBuffer {
    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn read(&self, dst: &mut [u8]) -> usize {
        self.data.read(dst)
    }
}

impl Drop for RxBuffer {
    fn drop(&mut self) {
        self.buflist.lock().unwrap().set_bit(self.offset);
    }
}

impl fmt::Debug for RxBuffer {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        f.debug_struct("RxBuffer")
            .field("offset", &self.offset)
            .field("len", &self.data.len())
            .finish()
    }
}

pub struct TxBuffer<'a> {
    data: SharedBuffer,
    offset: usize,
    length: usize,
    marker: ::std::marker::PhantomData<&'a BufferPool>,
}

impl<'a> TxBuffer<'a> {
    pub fn write(&mut self, src: &[u8]) -> usize {
        self.length = self.data.write(src);
        self.length
    }

    pub fn entry(self) -> sys::eth_fifo_entry {
        sys::eth_fifo_entry {
            offset: self.offset as u32,
            length: self.length as u16,
            flags: 0,
            cookie: 0,
        }
    }
}

/// A BufferPool represents a pool of memory that may be parceled out and used as buffers for
/// communicating with an Ethernet device.
///
/// Transmit and receive buffers are currently tracked separately. (TODO(tkilbourn): explain why)
/// In the memory pool, receive buffers are allocated first, followed by the transmit buffers. The
/// available/in-flight indexes represent the index within the given type of buffer, not the global
/// buffer pool.
pub struct BufferPool {
    /// The pointer and length representing the entire memory available to the pool.
    base: *mut u8,
    len: usize,
    /// The total number of buffers of each type.
    num_buffers: usize,
    /// The size of a buffer in the pool.
    buffer_size: usize,
    /// The offsets of the available buffers within the pool.
    rx_avail: Arc<Mutex<BufferMap>>,
    tx_avail: BufferMap,
    /// The offsets of buffers sent to another process.
    rx_in_flight: BufferMap,
    tx_in_flight: BufferMap,
}

impl BufferPool {
    /// Create a new `BufferPool` out of the given VMO, with each buffer having length
    /// `buffer_size`.
    pub fn new(vmo: zx::Vmo, buffer_size: usize) -> Result<BufferPool, zx::Status> {
        let len = vmo.get_size()? as usize;
        let mapped = zx::Vmar::root_self().map(
            0,
            &vmo,
            0,
            len,
            zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
        )?;
        let num_buffers = len / buffer_size / 2;
        let mut rx_avail = BufferMap::new(num_buffers);
        for i in 0..num_buffers {
            rx_avail.set_bit(i);
        }
        let mut tx_avail = BufferMap::new(num_buffers);
        for i in 0..num_buffers {
            tx_avail.set_bit(i);
        }
        Ok(BufferPool {
            base: mapped as *mut u8,
            len,
            num_buffers,
            rx_avail: Arc::new(Mutex::new(rx_avail)),
            tx_avail,
            rx_in_flight: BufferMap::new(num_buffers),
            tx_in_flight: BufferMap::new(num_buffers),
            buffer_size,
        })
    }

    /// Allocate a receive buffer and return the Ethernet fifo entry needed to queue it for the
    /// driver.
    pub fn alloc_rx_buffer(&mut self) -> Option<sys::eth_fifo_entry> {
        let mut rx_avail = self.rx_avail.lock().unwrap();
        let offset = rx_avail.find_first_set()?;
        rx_avail.clear_bit(offset);
        self.rx_in_flight.set_bit(offset);
        let fifo_offset = offset * self.buffer_size;
        Some(fifo_entry(fifo_offset as u32, self.buffer_size as u16))
    }

    /// Allocate a transmit buffer that may later be queued to the Ethernet device.
    pub fn alloc_tx_buffer<'a>(&'a mut self) -> Option<TxBuffer<'a>> {
        let offset = self.tx_avail.find_first_set()?;
        self.tx_avail.clear_bit(offset);
        self.tx_in_flight.set_bit(offset);
        let fifo_offset = (self.num_buffers + offset) * self.buffer_size;
        Some(TxBuffer {
            data: unsafe {
                SharedBuffer::new(self.base.offset(fifo_offset as isize), self.buffer_size)
            },
            offset,
            length: 0,
            marker: ::std::marker::PhantomData,
        })
    }

    /// Return a transmit buffer returned by the Ethernet device via the tx fifo. The index is
    /// determined by the offset from the `base` of the pool.
    pub fn release_tx_buffer(&mut self, fifo_offset: usize) {
        assert!(fifo_offset % self.buffer_size == 0);
        let offset = fifo_offset / self.buffer_size - self.num_buffers;
        assert!(self.tx_in_flight.get_bit(offset));
        self.tx_avail.set_bit(offset);
        self.tx_in_flight.clear_bit(offset);
    }

    /// Create an `RxBuffer` from the offset+len obtained from the rx fifo from the Ethernet
    /// device. The buffer is no longer considered either available or in-flight.
    pub fn map_rx_buffer(&mut self, fifo_offset: usize, len: usize) -> RxBuffer {
        assert!(fifo_offset % self.buffer_size == 0);
        assert!(len <= self.buffer_size);
        let offset = fifo_offset / self.buffer_size;
        assert!(self.rx_in_flight.get_bit(offset));
        self.rx_in_flight.clear_bit(offset);
        RxBuffer {
            data: unsafe { SharedBuffer::new(self.base.offset(fifo_offset as isize), len) },
            offset,
            buflist: Arc::clone(&self.rx_avail),
        }
    }
}

impl fmt::Debug for BufferPool {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        f.debug_struct("BufferPool")
            .field("base", &self.base)
            .field("len", &self.len)
            .field("num_buffers", &self.num_buffers)
            .field("buffer_size", &self.buffer_size)
            .field("rx_avail", &self.rx_avail)
            .field("tx_avail", &self.tx_avail)
            .field("rx_in_flight", &self.rx_in_flight)
            .field("tx_in_flight", &self.tx_in_flight)
            .finish()
    }
}

impl Drop for BufferPool {
    fn drop(&mut self) {
        unsafe {
            zx::Vmar::root_self()
                .unmap(self.base as usize, self.len)
                .unwrap();
        }
    }
}

/// A bitmap used to store buffer status.
#[derive(Debug)]
struct BufferMap {
    map: Vec<u64>,
    len: usize,
}

impl BufferMap {
    /// Create a new `BufferMap` with the given number of bits. The underlying storage may use more
    /// bits, but these are not accessible.
    fn new(size: usize) -> BufferMap {
        assert!(size < usize::max_value() - 63);
        let byte_size = (size + 63) / 64;
        BufferMap {
            map: vec![0; byte_size],
            len: size,
        }
    }

    /// Set the given bit. Panics if the bit is out of the range.
    fn set_bit(&mut self, bit: usize) {
        assert!(bit < self.len, "bit index out of bounds");
        let byte_offset = bit / 64;
        let bit_offset = bit % 64;
        self.map[byte_offset] |= 1 << bit_offset;
    }

    /// Clear the given bit. Panics if the bit is out of range.
    fn clear_bit(&mut self, bit: usize) {
        assert!(bit < self.len, "bit index out of bounds");
        let byte_offset = bit / 64;
        let bit_offset = bit % 64;
        self.map[byte_offset] &= !(1 << bit_offset);
    }

    /// Check whether the given bit is set. Panics if the bit is out of range.
    fn get_bit(&self, bit: usize) -> bool {
        assert!(bit < self.len, "bit index out of bounds");
        let byte_offset = bit / 64;
        let bit_offset = bit % 64;
        self.map[byte_offset] & (1 << bit_offset) != 0
    }

    /// Finds the lowest index bit that is set in the map or `None` if all bits are cleared. The
    /// bit is *not* cleared after returning from this method.
    fn find_first_set(&self) -> Option<usize> {
        for (i, byte) in self.map.iter().enumerate() {
            let ntz = byte.trailing_zeros();
            if ntz < 64 {
                return Some(i * 64 + ntz as usize);
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_buffer_map() {
        let map = BufferMap::new(1);
        assert_eq!(1, map.map.len());
        assert_eq!(1, map.len);

        let map = BufferMap::new(64);
        assert_eq!(1, map.map.len());
        assert_eq!(64, map.len);

        let map = BufferMap::new(65);
        assert_eq!(2, map.map.len());
        assert_eq!(65, map.len);
    }

    #[test]
    fn test_set_bit() {
        let mut map = BufferMap::new(100);
        assert_eq!(&[0, 0], &map.map[..]);

        map.set_bit(0);
        assert_eq!(&[1, 0], &map.map[..]);

        map.set_bit(7);
        assert_eq!(&[0b1000_0001, 0], &map.map[..]);

        map.set_bit(7);
        assert_eq!(&[0b1000_0001, 0], &map.map[..]);

        map.set_bit(64);
        assert_eq!(&[0b1000_0001, 1], &map.map[..]);

        map.set_bit(99);
        assert_eq!(&[0b1000_0001, (1 << 35) | 1], &map.map[..]);
    }

    #[test]
    fn test_clear_bit() {
        let mut map = BufferMap::new(100);
        map.map[0] = 0xff;

        map.clear_bit(0);
        assert_eq!(&[0b1111_1110, 0], &map.map[..]);

        map.clear_bit(7);
        assert_eq!(&[0b0111_1110, 0], &map.map[..]);

        map.clear_bit(64);
        assert_eq!(&[0b0111_1110, 0], &map.map[..]);

        map.map[1] = 0x7f;

        map.clear_bit(64);
        assert_eq!(&[0b0111_1110, 0b0111_1110], &map.map[..]);

        map.clear_bit(70);
        assert_eq!(&[0b0111_1110, 0b0011_1110], &map.map[..]);
    }

    #[test]
    fn test_get_bit() {
        let mut map = BufferMap::new(100);

        map.map[0] = 1;
        assert!(map.get_bit(0));
        assert!(!map.get_bit(1));

        map.map[0] = 1 << 63;
        assert!(!map.get_bit(62));
        assert!(map.get_bit(63));
        assert!(!map.get_bit(64));

        map.map[0] = 0;
        map.map[1] = 1;
        assert!(!map.get_bit(63));
        assert!(map.get_bit(64));
        assert!(!map.get_bit(65));

        map.map[1] = 1 << 35;
        assert!(!map.get_bit(98));
        assert!(map.get_bit(99));
    }

    #[test]
    fn test_find_first_set() {
        let mut map = BufferMap::new(100);

        assert_eq!(None, map.find_first_set());

        map.set_bit(0);
        assert_eq!(Some(0), map.find_first_set());

        map.clear_bit(0);
        map.set_bit(1);
        assert_eq!(Some(1), map.find_first_set());

        map.set_bit(63);
        assert_eq!(Some(1), map.find_first_set());

        map.clear_bit(1);
        assert_eq!(Some(63), map.find_first_set());

        map.set_bit(64);
        assert_eq!(Some(63), map.find_first_set());

        map.clear_bit(63);
        assert_eq!(Some(64), map.find_first_set());

        map.set_bit(99);
        assert_eq!(Some(64), map.find_first_set());

        map.clear_bit(64);
        assert_eq!(Some(99), map.find_first_set());

        map.set_bit(5);
        assert_eq!(Some(5), map.find_first_set());
    }

    #[test]
    #[should_panic]
    fn test_set_bit_oob() {
        let mut map = BufferMap::new(1);
        map.set_bit(1);
    }

    #[test]
    #[should_panic]
    fn test_clear_bit_oob() {
        let mut map = BufferMap::new(1);
        map.clear_bit(1);
    }

    #[test]
    #[should_panic]
    fn test_get_bit_oob() {
        let map = BufferMap::new(1);
        let _ = map.get_bit(1);
    }
}
