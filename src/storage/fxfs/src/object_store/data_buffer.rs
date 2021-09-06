// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_handle::ReadObjectHandle,
        round::{round_down, round_up},
    },
    anyhow::Error,
    async_trait::async_trait,
    async_utils::event::Event,
    either::Either::{Left, Right},
    futures::{stream::futures_unordered::FuturesUnordered, try_join, TryStreamExt},
    slab::Slab,
    std::{
        cell::UnsafeCell, cmp::Ordering, collections::BTreeSet, convert::TryInto, ops::Range,
        sync::Mutex,
    },
};

/// A readable, writable memory buffer that is not necessarily mapped into memory.
/// Mainly serves as a portable abstraction over a VMO (see VmoDataBuffer).
#[async_trait]
pub trait DataBuffer: Send + Sync {
    /// raw_read reads from the data buffer without reading any content from a data source if it is
    /// not present.  Any data that is not present will likely be returned as zeroes, but the caller
    /// should not rely on that behaviour; this function is intended to be used where the caller
    /// knows the data is present.
    fn raw_read(&self, offset: u64, buf: &mut [u8]);

    /// Writes to the buffer.  If the writes are unaligned, data will be read from the source to
    /// complete any unaligned pages.
    async fn write(
        &self,
        offset: u64,
        buf: &[u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<(), Error>;

    fn size(&self) -> u64;
    async fn resize(&self, size: u64);
    /// Zeroes |range|.
    fn zero(&self, range: Range<u64>);

    /// Read from the buffer but supply content from source where the data is not present.
    async fn read(
        &self,
        offset: u64,
        buf: &mut [u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<usize, Error>;
}

/// A default implementation of a DataBuffer.
pub struct MemDataBuffer(Mutex<Inner>);

struct Inner {
    size: u64,
    buf: Vec<u8>,

    // Records which pages are present.
    pages: BTreeSet<BoxedPage>,

    // In-flight readers.
    readers: Slab<ReadContext>,
}

impl Inner {
    // Make all pages within the specified range as present.
    fn mark_present(&mut self, range: Range<u64>) {
        let mut new_pages = Vec::new();
        let mut offset = range.start - range.start % PAGE_SIZE;
        for page in self.pages.range(offset..range.end) {
            // Preempt any reads: this is done by setting the read_key for the page to usize::MAX.
            let page = unsafe { page.page_mut() };
            page.read_key = usize::MAX;
            // Fill in any missing pages.
            if page.offset > offset {
                for offset in (offset..page.offset).step_by(PAGE_SIZE as usize) {
                    new_pages.push(BoxedPage(Box::new(PageCell(UnsafeCell::new(Page::new(
                        offset,
                        usize::MAX,
                    ))))));
                }
            }
            offset = page.offset + PAGE_SIZE;
        }
        for page in new_pages {
            self.pages.insert(page);
        }
        // Add any pages for the end of the range.
        for offset in (offset..range.end).step_by(PAGE_SIZE as usize) {
            self.pages.insert(BoxedPage(Box::new(PageCell(UnsafeCell::new(Page::new(
                offset,
                usize::MAX,
            ))))));
        }
    }

    fn resize(&mut self, size: u64) {
        let aligned_size = round_up(size, PAGE_SIZE).unwrap() as usize;
        self.buf.resize(aligned_size, 0u8);
        let old_size = self.size;
        self.size = size;
        if size < old_size {
            let end = std::cmp::min(old_size as usize, aligned_size);
            self.buf[size as usize..end].fill(0);
            let mut to_remove = Vec::new();
            for page in self.pages.range(size..) {
                to_remove.push(unsafe { page.page() }.offset());
            }
            for offset in to_remove {
                self.pages.remove(&offset);
            }
        } else {
            self.mark_present(old_size..aligned_size as u64);
        }
    }
}

// This is unrelated to any system page size; this is merely the size of the pages used by
// MemDataBuffer.  These pages are similar in concept but completely independent of any system
// pages.
const PAGE_SIZE: u64 = 4096;

struct BoxedPage(Box<PageCell>);

impl BoxedPage {
    unsafe fn page(&self) -> &Page {
        self.0.page()
    }

    unsafe fn page_mut(&self) -> &mut Page {
        self.0.page_mut()
    }
}

impl Ord for BoxedPage {
    fn cmp(&self, other: &Self) -> Ordering {
        unsafe { self.page().cmp(other.page()) }
    }
}

impl PartialOrd for BoxedPage {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Eq for BoxedPage {}

impl PartialEq for BoxedPage {
    fn eq(&self, other: &Self) -> bool {
        unsafe { self.page() == other.page() }
    }
}

impl std::borrow::Borrow<u64> for BoxedPage {
    fn borrow(&self) -> &u64 {
        unsafe { &self.page().offset }
    }
}

struct PageCell(UnsafeCell<Page>);

impl PageCell {
    unsafe fn page(&self) -> &Page {
        &*self.0.get()
    }

    unsafe fn page_mut(&self) -> &mut Page {
        &mut *self.0.get()
    }
}

// TODO(csuter): eventually, we should add some kind of LRU list which would chain through the
// pages.
struct Page {
    offset: u64,

    // When the page is being read, read_key is the slab key for ReadContext.
    read_key: usize,
}

impl Page {
    fn new(offset: u64, read_key: usize) -> Self {
        Self { offset: offset, read_key }
    }

    fn offset(&self) -> u64 {
        self.offset
    }

    fn is_reading(&self) -> bool {
        self.read_key != usize::MAX
    }
}

impl Ord for Page {
    fn cmp(&self, other: &Self) -> Ordering {
        self.offset().cmp(&other.offset())
    }
}

impl PartialOrd for Page {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Eq for Page {}

impl PartialEq for Page {
    fn eq(&self, other: &Self) -> bool {
        self.offset() == other.offset()
    }
}

// ReadContext allows other readers to wait on other readers that are reading the same range at the
// same time.
struct ReadContext {
    range: Range<u64>,
    wait_event: Option<Event>,
}

impl MemDataBuffer {
    pub fn new(size: u64) -> Self {
        Self(Mutex::new(Inner {
            buf: vec![0u8; round_up(size, PAGE_SIZE).unwrap().try_into().unwrap()],
            pages: BTreeSet::new(),
            readers: Slab::new(),
            size,
        }))
    }

    async fn read_some(
        &self,
        offset: u64,
        out_buf: &mut [u8],
        source: &dyn ReadObjectHandle,
        read_key: usize,
    ) -> Result<(), Error> {
        let aligned_range = round_down(offset, PAGE_SIZE)
            ..round_up(offset + out_buf.len() as u64, PAGE_SIZE).unwrap();
        let mut read_buf =
            source.allocate_buffer((aligned_range.end - aligned_range.start) as usize);
        let amount = source.read(aligned_range.start, read_buf.as_mut()).await?;
        read_buf.as_mut_slice()[amount..].fill(0);

        let mut inner = self.0.lock().unwrap();
        let Inner { pages, buf, readers, .. } = &mut *inner;
        let mut read_buf = read_buf.as_slice();
        for page in pages.range(aligned_range) {
            let page = unsafe { page.page_mut() };
            if page.read_key == read_key {
                buf[page.offset as usize..page.offset as usize + PAGE_SIZE as usize]
                    .copy_from_slice(&read_buf[..PAGE_SIZE as usize]);
                page.read_key = usize::MAX;
            }
            read_buf = &read_buf[PAGE_SIZE as usize..];
        }
        out_buf.copy_from_slice(&buf[offset as usize..offset as usize + out_buf.len()]);
        readers.get(read_key).unwrap().wait_event.as_ref().map(|e| e.signal());
        Ok(())
    }

    async fn wait_for_pending_read(
        &self,
        offset: u64,
        buf: &mut [u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<(), Error> {
        let aligned_offset = round_down(offset, PAGE_SIZE);
        loop {
            let mut read_keys = ReadKeys::new(self);
            let result = {
                let mut inner = self.0.lock().unwrap();
                match inner.pages.get(&aligned_offset) {
                    None => {
                        // In this case, the page might have been pending a read but the
                        // read was dropped or it failed.  Or the page could have been
                        // evicted.  In that case, we must schedule a read.  This won't
                        // be as efficient as the usual path because it is only issuing
                        // a read for a single page, but it should be rare enough that
                        // it doesn't matter.
                        let mut new_pages = Vec::new();
                        let read_key = read_keys.new_read(
                            offset..offset + PAGE_SIZE,
                            &mut inner.readers,
                            &mut new_pages,
                        );
                        for page in new_pages {
                            inner.pages.insert(page);
                        }
                        Left(read_key)
                    }
                    Some(page) => {
                        let page = unsafe { page.page_mut() };
                        if page.is_reading() {
                            // If the page is pending a read, then we need to wait
                            // until the read has finished.
                            let read_key = page.read_key;
                            let read_context = inner.readers.get_mut(read_key).unwrap();
                            Right(
                                read_context
                                    .wait_event
                                    .get_or_insert_with(|| Event::new())
                                    .wait_or_dropped(),
                            )
                        } else {
                            // The page is present and not pending a read so we can
                            // just copy it.
                            buf.copy_from_slice(
                                &inner.buf[offset as usize..offset as usize + buf.len()],
                            );
                            return Ok(());
                        }
                    }
                }
            };
            // With the lock dropped, we can either issue the read or wait for another
            // read to finish...
            match result {
                Left(read_key) => {
                    return self.read_some(offset, buf, source, read_key).await;
                }
                Right(event) => {
                    let _ = event.await;
                }
            }
        }
    }
}

#[async_trait]
impl DataBuffer for MemDataBuffer {
    async fn write(
        &self,
        offset: u64,
        buf: &[u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<(), Error> {
        let aligned_start = offset - offset % PAGE_SIZE;
        let end = offset.checked_add(buf.len() as u64).ok_or(FxfsError::TooBig)?;
        let aligned_end = end - end % PAGE_SIZE;
        if aligned_start != offset || aligned_end != end {
            try_join!(
                async {
                    if aligned_start != offset {
                        // TODO(csuter): We don't need the buffer so with a bit of refactoring we
                        // could get rid of it.
                        let mut buf = [0u8; PAGE_SIZE as usize];
                        self.read(aligned_start, &mut buf, source).await?;
                    }
                    Result::<(), Error>::Ok(())
                },
                async {
                    if aligned_end != end && aligned_end != offset {
                        let mut buf = [0u8; PAGE_SIZE as usize];
                        self.read(aligned_end, &mut buf, source).await?;
                    }
                    Result::<(), Error>::Ok(())
                }
            )?;
        }
        let mut inner = self.0.lock().unwrap();
        if end > inner.size {
            inner.resize(end);
        }
        inner.buf[offset as usize..end as usize].copy_from_slice(buf);
        inner.mark_present(offset..end);
        Ok(())
    }
    fn size(&self) -> u64 {
        self.0.lock().unwrap().size
    }
    async fn resize(&self, size: u64) {
        self.0.lock().unwrap().resize(size);
    }
    fn zero(&self, range: Range<u64>) {
        let mut inner = self.0.lock().unwrap();
        inner.buf[range.start as usize..range.end as usize].fill(0u8);
        inner.mark_present(range);
    }

    fn raw_read(&self, offset: u64, buf: &mut [u8]) {
        let inner = self.0.lock().unwrap();
        buf.copy_from_slice(&inner.buf[offset as usize..offset as usize + buf.len()]);
    }

    async fn read(
        &self,
        mut offset: u64,
        mut read_buf: &mut [u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<usize, Error> {
        let content_size = self.size();
        if offset >= content_size {
            return Ok(0);
        }
        let aligned_size = round_up(self.size(), PAGE_SIZE).unwrap();
        let done = if content_size - offset < read_buf.len() as u64 {
            if aligned_size - offset < read_buf.len() as u64 {
                read_buf = &mut read_buf[0..(aligned_size - offset) as usize];
            }
            (content_size - offset) as usize
        } else {
            read_buf.len()
        };

        // A list of all the futures for any required reads.
        let reads = FuturesUnordered::new();

        // Tracks any reads that other tasks might be working on.
        let pending_reads = FuturesUnordered::new();

        // New pages that we might need.
        let mut new_pages = Vec::new();

        // Keep track of the keys for any reads we schedule.
        let mut read_keys = ReadKeys::new(self);

        {
            let mut inner = self.0.lock().unwrap();
            let Inner { pages, readers, buf, .. } = &mut *inner;
            for page in pages.range(round_down(offset, PAGE_SIZE)..offset + read_buf.len() as u64) {
                let page = unsafe { page.page_mut() };

                // Handle any gap between the last page we found and this one.
                if page.offset() > offset {
                    // Schedule a read for the gap.
                    let (head, tail) = read_buf.split_at_mut((page.offset() - offset) as usize);
                    reads.push(self.read_some(
                        offset,
                        head,
                        source,
                        read_keys.new_read(offset..page.offset(), readers, &mut new_pages),
                    ));
                    read_buf = tail;
                    offset = page.offset();
                }

                let (head, tail) =
                    read_buf.split_at_mut(std::cmp::min(PAGE_SIZE as usize, read_buf.len()));

                if page.is_reading() {
                    pending_reads.push(self.wait_for_pending_read(offset, head, source));
                } else {
                    head.copy_from_slice(&buf[offset as usize..offset as usize + head.len()]);
                }

                read_buf = tail;
                offset = page.offset() + PAGE_SIZE;
            }
            // Handle the tail.
            if !read_buf.is_empty() {
                reads.push(self.read_some(
                    offset,
                    read_buf,
                    source,
                    read_keys.new_read(
                        offset..offset + read_buf.len() as u64,
                        readers,
                        &mut new_pages,
                    ),
                ));
            }

            for page in new_pages {
                pages.insert(page);
            }
        }

        try_join!(
            async {
                reads.try_collect().await?;
                Result::<(), Error>::Ok(())
            },
            async {
                pending_reads.try_collect().await?;
                Result::<(), Error>::Ok(())
            }
        )?;

        Ok(done)
    }
}

// ReadKeys is a structure that tracks in-flight reads and cleans them up if dropped.
struct ReadKeys<'a> {
    buf: &'a MemDataBuffer,
    keys: Vec<usize>,
}

impl<'a> ReadKeys<'a> {
    fn new(buf: &'a MemDataBuffer) -> Self {
        ReadKeys { buf, keys: Vec::new() }
    }

    // Returns a new read key for a read for `range`.  It will append new pages for the read that
    // will get properly cleaned up if dropped.
    fn new_read(
        &mut self,
        range: Range<u64>,
        readers: &mut Slab<ReadContext>,
        new_pages: &mut Vec<BoxedPage>,
    ) -> usize {
        let range = round_down(range.start, PAGE_SIZE)..round_up(range.end, PAGE_SIZE).unwrap();
        let read_key = readers.insert(ReadContext { range: range.clone(), wait_event: None });
        self.keys.push(read_key);
        for offset in range.step_by(PAGE_SIZE as usize) {
            new_pages
                .push(BoxedPage(Box::new(PageCell(UnsafeCell::new(Page::new(offset, read_key))))));
        }
        read_key
    }
}

impl Drop for ReadKeys<'_> {
    fn drop(&mut self) {
        let mut inner = self.buf.0.lock().unwrap();
        for read_key in &self.keys {
            let range = inner.readers.remove(*read_key).range;
            let mut to_remove = Vec::new();
            for page in inner.pages.range(range) {
                // It's possible that the read was pre-empted so we must check that the key matches.
                if unsafe { page.page().read_key } == *read_key {
                    to_remove.push(unsafe { page.page().offset() });
                }
            }
            for offset in to_remove {
                inner.pages.remove(&offset);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{DataBuffer, MemDataBuffer, PAGE_SIZE},
        crate::{
            errors::FxfsError,
            object_handle::{ObjectHandle, ReadObjectHandle},
        },
        anyhow::Error,
        async_trait::async_trait,
        async_utils::event::Event,
        fuchsia_async as fasync,
        futures::{future::poll_fn, join, FutureExt},
        std::{
            sync::{
                atomic::{AtomicU8, Ordering},
                Arc,
            },
            task::Poll,
        },
        storage_device::{
            buffer::{Buffer, MutableBufferRef},
            fake_device::FakeDevice,
            Device,
        },
    };

    struct FakeSource {
        device: Arc<dyn Device>,
        go: Event,
        counter: AtomicU8,
    }

    impl FakeSource {
        // `device` is only used to provide allocate_buffer; reads don't go to the device.
        fn new(device: Arc<dyn Device>) -> Self {
            FakeSource { go: Event::new(), device, counter: AtomicU8::new(1) }
        }
    }

    #[async_trait]
    impl ReadObjectHandle for FakeSource {
        async fn read(&self, offset: u64, mut buf: MutableBufferRef<'_>) -> Result<usize, Error> {
            assert_eq!(offset % PAGE_SIZE, 0);
            assert_eq!(buf.len() % PAGE_SIZE as usize, 0);
            let _ = self.go.wait_or_dropped().await;
            buf.as_mut_slice().fill(self.counter.fetch_add(1, Ordering::Relaxed));
            Ok(buf.len())
        }
    }

    #[async_trait]
    impl ObjectHandle for FakeSource {
        fn object_id(&self) -> u64 {
            unreachable!();
        }

        fn get_size(&self) -> u64 {
            unreachable!();
        }

        fn block_size(&self) -> u32 {
            unreachable!();
        }

        fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
            self.device.allocate_buffer(size)
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sequential_reads() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let mut buf = [0; PAGE_SIZE as usize];

        let source = FakeSource::new(device.clone());
        source.go.signal();

        data_buf.read(0, &mut buf, &source).await.expect("read failed");
        assert_eq!(&buf, &[1; PAGE_SIZE as usize]);

        data_buf.read(0, &mut buf, &source).await.expect("read failed");
        assert_eq!(&buf, &[1; PAGE_SIZE as usize]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_parallel_reads() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let source = FakeSource::new(device.clone());
        let mut buf = [0; PAGE_SIZE as usize];
        let mut buf2 = [0; PAGE_SIZE as usize];
        join!(
            async {
                data_buf.read(PAGE_SIZE, buf.as_mut(), &source).await.expect("read failed");
            },
            async {
                data_buf.read(PAGE_SIZE, buf2.as_mut(), &source).await.expect("read failed");
            },
            async {
                fasync::Timer::new(std::time::Duration::from_millis(100)).await;
                source.go.signal();
            }
        );
        assert_eq!(&buf, &[1; PAGE_SIZE as usize]);
        assert_eq!(&buf2, &[1; PAGE_SIZE as usize]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unaligned_write() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let source = FakeSource::new(device.clone());
        source.go.signal();
        let mut buf = [0; 3 * PAGE_SIZE as usize];
        buf.fill(67);
        data_buf
            .write(PAGE_SIZE - 10, &buf[..PAGE_SIZE as usize + 20], &source)
            .await
            .expect("write failed");
        data_buf.read(0, &mut buf, &source).await.expect("read failed");

        // There are two combinations depending on which read goes first.
        let mut expected1 = [1; PAGE_SIZE as usize - 10].to_vec();
        expected1.extend(&[67; PAGE_SIZE as usize + 20]);
        expected1.extend(&[2; PAGE_SIZE as usize - 10]);
        let mut expected2 = [1; PAGE_SIZE as usize - 10].to_vec();
        expected2.extend(&[67; PAGE_SIZE as usize + 20]);
        expected2.extend(&[2; PAGE_SIZE as usize - 10]);
        assert!(&buf[..] == expected1 || &buf[..] == expected2);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_dropped_read() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let source = FakeSource::new(device.clone());
        let mut buf = [0; PAGE_SIZE as usize];
        let mut buf2 = [0; PAGE_SIZE as usize * 2];
        let mut read_fut = data_buf.read(0, buf.as_mut(), &source);
        let mut read_fut2 = data_buf.read(0, buf2.as_mut(), &source);
        poll_fn(|ctx| {
            assert!(matches!(read_fut.poll_unpin(ctx), Poll::Pending));
            source.go.signal();
            assert!(matches!(read_fut2.poll_unpin(ctx), Poll::Pending));
            Poll::Ready(())
        })
        .await;
        // If we now drop the first future, the second future should complete.
        std::mem::drop(read_fut);
        assert_eq!(read_fut2.await.expect("read failed"), PAGE_SIZE as usize * 2);
        assert_eq!(&buf2[0..PAGE_SIZE as usize], [2; PAGE_SIZE as usize]);
        assert_eq!(&buf2[PAGE_SIZE as usize..], [1; PAGE_SIZE as usize]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_with_gap() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let source = FakeSource::new(device.clone());
        source.go.signal();

        let mut buf = [0; PAGE_SIZE as usize * 2];
        assert_eq!(
            data_buf
                .read(PAGE_SIZE, &mut buf[..PAGE_SIZE as usize], &source)
                .await
                .expect("read failed"),
            PAGE_SIZE as usize
        );

        assert_eq!(&buf[..PAGE_SIZE as usize], [1; PAGE_SIZE as usize]);

        assert_eq!(
            data_buf.read(0, &mut buf, &source).await.expect("read failed"),
            PAGE_SIZE as usize * 2
        );

        assert_eq!(&buf[..PAGE_SIZE as usize], [2; PAGE_SIZE as usize]);
        assert_eq!(&buf[PAGE_SIZE as usize..], [1; PAGE_SIZE as usize]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_unaligned() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let source = FakeSource::new(device.clone());

        let mut buf = [0; 10];
        let mut buf2 = [0; 10];
        let mut read_fut = data_buf.read(10, &mut buf, &source);
        let mut read_fut2 = data_buf.read(10, &mut buf2, &source);

        poll_fn(|ctx| {
            assert!(matches!(read_fut.poll_unpin(ctx), Poll::Pending));
            source.go.signal();
            assert!(matches!(read_fut2.poll_unpin(ctx), Poll::Pending));
            Poll::Ready(())
        })
        .await;

        assert_eq!(read_fut.await.expect("read failed"), 10 as usize);
        assert_eq!(read_fut2.await.expect("read failed"), 10 as usize);

        assert_eq!(&buf, &[1; 10]);
        assert_eq!(&buf2, &[1; 10]);

        assert_eq!(data_buf.read(0, &mut buf, &source).await.expect("read failed"), 10);
        assert_eq!(&buf, &[1; 10]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_too_big() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let source = FakeSource::new(device.clone());
        let buf = [0; PAGE_SIZE as usize];

        assert!(FxfsError::TooBig
            .matches(&data_buf.write(u64::MAX, &buf, &source).await.expect_err("write succeeded")));
    }
}
