// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::buffer::{round_up, Buffer},
    std::any::Any,
    std::cell::UnsafeCell,
    std::ops::Range,
    std::pin::Pin,
    std::sync::Mutex,
    std::vec::Vec,
};

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

    pub fn block_size(&self) -> usize {
        self.block_size
    }

    pub fn buffer_source(&self) -> &dyn BufferSource {
        self.source.as_ref()
    }

    /// Takes the buffer source from the allocator and consumes the allocator.
    pub fn take_buffer_source(self) -> Box<dyn BufferSource> {
        self.source
    }

    /// Allocates a Buffer with capacity for |size| bytes.
    pub fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
        if size > self.source.size() {
            panic!("Allocation of {} bytes would exceed limit {}", size, self.source.size());
        }
        assert!(size <= self.source.size());
        let mut inner = self.inner.lock().unwrap();
        let range = inner.next_free as usize..inner.next_free as usize + size;
        inner.next_free += round_up(size, self.block_size) as u64;
        // Safety is ensured by the allocator not double-allocating any regions.
        Buffer::new(unsafe { self.source.sub_slice(&range) }, range, &self)
    }

    #[doc(hidden)]
    pub fn free_buffer(&self, range: Range<usize>) {
        let mut inner = self.inner.lock().unwrap();
        if inner.next_free == range.end as u64 {
            inner.next_free = range.start as u64;
        }
        // TODO(jfsulliv): Actually free the range in the nontrivial cases.
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
        assert_eq!(buf.len(), 1024 * 1024);
        buf.as_mut_slice().fill(0xaa as u8);
        let mut vec = vec![0 as u8; 1024 * 1024];
        vec.copy_from_slice(buf.as_slice());
        assert_eq!(vec, vec![0xaa as u8; 1024 * 1024]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_buffer_refs() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));
        let allocator = BufferAllocator::new(512, source);

        // Allocate one buffer first so that |buf| is not starting at offset 0. This helps catch
        // bugs.
        let _buf = allocator.allocate_buffer(512);
        let mut buf = allocator.allocate_buffer(4096);
        let base = buf.range().start;
        {
            let mut bref = buf.subslice_mut(1000..2000);
            assert_eq!(bref.len(), 1000);
            assert_eq!(bref.range(), base + 1000..base + 2000);
            bref.as_mut_slice().fill(0xbb);
            {
                let mut bref2 = bref.reborrow().subslice_mut(0..100);
                assert_eq!(bref2.len(), 100);
                assert_eq!(bref2.range(), base + 1000..base + 1100);
                bref2.as_mut_slice().fill(0xaa);
            }
            {
                let mut bref2 = bref.reborrow().subslice_mut(900..1000);
                assert_eq!(bref2.len(), 100);
                assert_eq!(bref2.range(), base + 1900..base + 2000);
                bref2.as_mut_slice().fill(0xcc);
            }
            assert_eq!(bref.as_slice()[..100], vec![0xaa; 100]);
            assert_eq!(bref.as_slice()[100..900], vec![0xbb; 800]);

            let bref = bref.subslice_mut(900..);
            assert_eq!(bref.len(), 100);
            assert_eq!(bref.as_slice(), vec![0xcc; 100]);
        }
        {
            let bref = buf.as_ref();
            assert_eq!(bref.len(), 4096);
            assert_eq!(bref.range(), base..base + 4096);
            assert_eq!(bref.as_slice()[0..1000], vec![0x00; 1000]);
            {
                let bref2 = bref.subslice(1000..2000);
                assert_eq!(bref2.len(), 1000);
                assert_eq!(bref2.range(), base + 1000..base + 2000);
                assert_eq!(bref2.as_slice()[..100], vec![0xaa; 100]);
                assert_eq!(bref2.as_slice()[100..900], vec![0xbb; 800]);
                assert_eq!(bref2.as_slice()[900..1000], vec![0xcc; 100]);
            }

            let bref = bref.subslice(2048..);
            assert_eq!(bref.len(), 2048);
            assert_eq!(bref.as_slice(), vec![0x00; 2048]);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_buffer_split() {
        let source = Box::new(MemBufferSource::new(1024 * 1024));
        let allocator = BufferAllocator::new(512, source);

        // Allocate one buffer first so that |buf| is not starting at offset 0. This helps catch
        // bugs.
        let _buf = allocator.allocate_buffer(512);
        let mut buf = allocator.allocate_buffer(4096);
        let base = buf.range().start;
        {
            let bref = buf.as_mut();
            let (mut s1, mut s2) = bref.split_at_mut(2048);
            assert_eq!(s1.len(), 2048);
            assert_eq!(s1.range(), base..base + 2048);
            s1.as_mut_slice().fill(0xaa);
            assert_eq!(s2.len(), 2048);
            assert_eq!(s2.range(), base + 2048..base + 4096);
            s2.as_mut_slice().fill(0xbb);
        }
        {
            let bref = buf.as_ref();
            let (s1, s2) = bref.split_at(1);
            let (s2, s3) = s2.split_at(2047);
            let (s3, s4) = s3.split_at(0);
            assert_eq!(s1.len(), 1);
            assert_eq!(s1.range(), base..base + 1);
            assert_eq!(s2.len(), 2047);
            assert_eq!(s2.range(), base + 1..base + 2048);
            assert_eq!(s3.len(), 0);
            assert_eq!(s3.range(), base + 2048..base + 2048);
            assert_eq!(s4.len(), 2048);
            assert_eq!(s4.range(), base + 2048..base + 4096);
            assert_eq!(s1.as_slice(), vec![0xaa; 1]);
            assert_eq!(s2.as_slice(), vec![0xaa; 2047]);
            assert_eq!(s3.as_slice(), vec![]);
            assert_eq!(s4.as_slice(), vec![0xbb; 2048]);
        }
    }
}
