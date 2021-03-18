// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::buffer::Buffer, std::any::Any, std::cell::UnsafeCell, std::ops::Range, std::pin::Pin,
    std::sync::Mutex, std::vec::Vec,
};

fn round_down<T>(value: T, granularity: T) -> T
where
    T: num::Num + Copy,
{
    value - value % granularity
}

fn round_up<T>(value: T, granularity: T) -> T
where
    T: num::Num + Copy,
{
    round_down(value + granularity - T::one(), granularity)
}

/// BufferSource is a contiguous range of memory which may have some special properties (such as
/// being contained within a special memory region that can be used for block transactions, e.g.
/// a VMO).
/// This is the backing of a BufferAllocator which the allocator uses to create Buffer objects.
pub trait BufferSource: Any + std::fmt::Debug + Send + Sync {
    /// Returns the capacity of the BufferSource.
    fn size(&self) -> usize;
    /// Returns a mutable slice covering |range| in the BufferSource.
    /// Panics if |range| exceeds |self.size()|.
    unsafe fn sub_slice(&self, range: &Range<usize>) -> &mut [u8];
    fn as_any(&self) -> &dyn Any;
    fn into_any(self: Box<Self>) -> Box<dyn Any>;
}

/// A basic heap-backed memory source.
#[derive(Debug)]
pub struct MemBufferSource {
    // We use an UnsafeCell here because we need interior mutability of the buffer (to hand out
    // mutable slices to it in |buffer()|), but don't want to pay the cost of wrapping the buffer in
    // a Mutex. We must guarantee that the Buffer objects we hand out don't overlap, but that is
    // already a requirement for correctness.
    data: UnsafeCell<Pin<Vec<u8>>>,
}

// Safe because none of the fields in MemBufferSource are modified, except the contents of |data|,
// but that is managed by the BufferAllocator.
unsafe impl Sync for MemBufferSource {}

impl MemBufferSource {
    pub fn new(size: usize) -> Self {
        Self { data: UnsafeCell::new(Pin::new(vec![0 as u8; size])) }
    }
}

impl BufferSource for MemBufferSource {
    fn size(&self) -> usize {
        // Safe because the reference goes out of scope as soon as we use it.
        unsafe { (&*self.data.get()).len() }
    }

    unsafe fn sub_slice(&self, range: &Range<usize>) -> &mut [u8] {
        assert!(range.start < self.size() && range.end <= self.size());
        assert!(range.start % std::mem::align_of::<u8>() == 0);
        let data = (&mut *self.data.get())[..].as_mut_ptr();
        std::slice::from_raw_parts_mut(
            (data as usize + range.start) as *mut u8,
            range.end - range.start,
        )
    }

    fn as_any(&self) -> &dyn Any {
        self
    }

    fn into_any(self: Box<Self>) -> Box<dyn Any> {
        self
    }
}

#[derive(Debug)]
struct Inner {
    next_free: u64,
}

/// BufferAllocator creates Buffer objects to be used for block device I/O requests.
/// TODO(jfsulliv): Improve the allocation strategy (buddy allocation, perhaps).
#[derive(Debug)]
pub struct BufferAllocator {
    block_size: usize,
    source: Box<dyn BufferSource>,
    inner: Mutex<Inner>,
}

impl BufferAllocator {
    pub fn new(block_size: usize, source: Box<dyn BufferSource>) -> Self {
        Self { block_size, source, inner: Mutex::new(Inner { next_free: 0 }) }
    }

    pub fn buffer_source(&self) -> &dyn BufferSource {
        self.source.as_ref()
    }

    /// Takes the buffer source from the allocator and consumes the allocator.
    pub fn take_buffer_source(self) -> Box<dyn BufferSource> {
        self.source
    }

    /// Allocates a Buffer with capacity for at least |size| bytes.
    pub fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        let mut inner = self.inner.lock().unwrap();
        let range =
            inner.next_free as usize..(inner.next_free as usize + round_up(size, self.block_size));
        inner.next_free = range.end as u64;
        // Safety is ensured by the allocator not double-allocating any regions.
        Buffer::new(unsafe { self.source.sub_slice(&range) }, range, &self)
    }

    #[doc(hidden)]
    pub fn take_buffer(&self, _buf: Buffer<'_>) {
        // TODO(jfsulliv): Actually free the buffer.
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::device::buffer_allocator::{BufferAllocator, MemBufferSource},
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_buffer_read_write() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));

        let allocator = BufferAllocator::new(8192, source);

        let mut buf = allocator.allocate_buffer(8192);
        buf.as_mut_slice().fill(0xaa as u8);
        let mut vec = vec![0 as u8; 8192];
        vec.copy_from_slice(buf.as_slice());
        assert_eq!(vec, vec![0xaa as u8; 8192]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_buffer_consecutive_calls_do_not_overlap() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));

        let allocator = BufferAllocator::new(8192, source);

        let buf1 = allocator.allocate_buffer(8192);
        let buf2 = allocator.allocate_buffer(8192);
        assert!(buf1.range().end <= buf2.range().start || buf2.range().end <= buf1.range().start);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_many_buffers() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));

        let allocator = BufferAllocator::new(8192, source);

        for _ in 0..10 {
            let _ = allocator.allocate_buffer(8192);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_buffer_rounded_up_to_block() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));

        let allocator = BufferAllocator::new(8192, source);

        let mut buf = allocator.allocate_buffer(1);
        assert!(buf.size() == 8192);
        buf.as_mut_slice().fill(0xaa as u8);
        let mut vec = vec![0 as u8; 8192];
        vec.copy_from_slice(buf.as_slice());
        assert_eq!(vec, vec![0xaa as u8; 8192]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_small_buffers_dont_overlap() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));

        let allocator = BufferAllocator::new(8192, source);

        let buf1 = allocator.allocate_buffer(1);
        let buf2 = allocator.allocate_buffer(1);
        assert!(buf1.range().end <= buf2.range().start || buf2.range().end <= buf1.range().start);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_allocate_large_buffer() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));

        let allocator = BufferAllocator::new(8192, source);

        let mut buf = allocator.allocate_buffer(1024 * 1024);
        assert_eq!(buf.size(), 1024 * 1024);
        buf.as_mut_slice().fill(0xaa as u8);
        let mut vec = vec![0 as u8; 1024 * 1024];
        vec.copy_from_slice(buf.as_slice());
        assert_eq!(vec, vec![0xaa as u8; 1024 * 1024]);
    }
}
