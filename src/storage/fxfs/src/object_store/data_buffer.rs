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
    futures::{pin_mut, stream::futures_unordered::FuturesUnordered, try_join, TryStreamExt},
    pin_project::{pin_project, pinned_drop},
    slab::Slab,
    std::{
        cell::UnsafeCell, cmp::Ordering, collections::BTreeSet, convert::TryInto, ops::Range,
        pin::Pin, sync::Mutex,
    },
};

// StackList holds a doubly linked list of structures on the stack.  After pushing nodes onto the
// list, the caller *must* call erase_node before or as the node is dropped.
struct StackListNodePtr<T>(*const T);

impl<T> Copy for StackListNodePtr<T> {}

impl<T> Clone for StackListNodePtr<T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<T> Default for StackListNodePtr<T> {
    fn default() -> Self {
        Self(std::ptr::null())
    }
}

unsafe impl<T> Send for StackListNodePtr<T> {}

struct StackListChain<T> {
    // Access to previous and next is safe provided we have mutable access to the list.
    prev: UnsafeCell<StackListNodePtr<T>>,
    next: UnsafeCell<StackListNodePtr<T>>,
}

impl<T> Default for StackListChain<T> {
    fn default() -> Self {
        Self {
            prev: UnsafeCell::new(StackListNodePtr::default()),
            next: UnsafeCell::new(StackListNodePtr::default()),
        }
    }
}

// The list contains just the head pointer.
struct StackList<T>(StackListNodePtr<T>);

impl<T: AsRef<StackListChain<T>>> StackList<T> {
    fn new() -> Self {
        Self(StackListNodePtr::default())
    }

    // Pushes node onto the list. After doing this, erase_node *must* be called before the node
    // is dropped (which is why this is unsafe).
    unsafe fn push_front(&mut self, node: Pin<&mut T>) {
        let node_ptr = StackListNodePtr(&*node);
        let chain = (*node_ptr.0).as_ref();
        *chain.next.get() = std::mem::replace(&mut self.0, node_ptr);
        if let Some(next) = (*chain.next.get()).0.as_ref() {
            *next.as_ref().prev.get() = node_ptr;
        }
    }

    fn erase_node(&mut self, node: &T) {
        unsafe {
            let chain = node.as_ref();
            let prev = std::mem::take(&mut *chain.prev.get());
            if let Some(next) = (*chain.next.get()).0.as_ref() {
                *next.as_ref().prev.get() = prev;
            }
            let next = std::mem::take(&mut *chain.next.get());
            if let Some(prev) = prev.0.as_ref() {
                *prev.as_ref().next.get() = next;
            } else if self.0 .0 == node {
                self.0 = next;
            }
        }
    }

    fn iter(&self) -> StackListIter<'_, T> {
        StackListIter { list: self, last_node: None }
    }
}

impl<T> Drop for StackList<T> {
    fn drop(&mut self) {
        assert!(self.0 .0.is_null());
    }
}

struct StackListIter<'a, T: AsRef<StackListChain<T>>> {
    list: &'a StackList<T>,
    last_node: Option<&'a T>,
}

impl<'a, T: AsRef<StackListChain<T>>> Iterator for StackListIter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        unsafe {
            match self.last_node {
                None => self.last_node = self.list.0 .0.as_ref(),
                Some(node) => self.last_node = (*node.as_ref().next.get()).0.as_ref(),
            }
            self.last_node
        }
    }
}

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

    // Each read has an associated ReadKeys instance which is stored on the stack.  So that we can
    // correctly handle truncation we need to be able to get to the ReadKeys instances, so they're
    // added to a doubly linked list here.
    read_keys_list: StackList<ReadKeys>,
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
        for read_keys in self.read_keys_list.iter() {
            let end_offset = read_keys.end_offset.get();
            // Safe because we have &mut self.
            unsafe {
                if *end_offset > size {
                    *end_offset = size;
                }
            }
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
            read_keys_list: StackList::new(),
        }))
    }

    // Reads from the source starting at the provided offset.
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
        // Whilst we were reading, it's possible the buffer was truncated; only copy out what we
        // can.  The read function will make sure that the correct value for the amount read is
        // returned.
        if offset < buf.len() as u64 {
            let range = offset as usize..std::cmp::min(offset as usize + out_buf.len(), buf.len());
            out_buf.copy_from_slice(&buf[range]);
        }
        readers.get(read_key).unwrap().wait_event.as_ref().map(|e| e.signal());
        Ok(())
    }

    async fn wait_for_pending_read(
        &self,
        offset: u64,
        buf: &mut [u8],
        source: &dyn ReadObjectHandle,
        keys: ReadKeysPtr,
    ) -> Result<(), Error> {
        let aligned_offset = round_down(offset, PAGE_SIZE);
        loop {
            let result = {
                let mut inner_lock = self.0.lock().unwrap();
                let inner = &mut *inner_lock; // So that we can split the borrow.
                let mut read_keys = keys.get(&mut inner.read_keys_list);
                if *read_keys.as_mut().project().end_offset.get_mut() <= offset {
                    return Ok(());
                }
                match inner.pages.get(&aligned_offset) {
                    None => {
                        // In this case, the page might have been pending a read but the
                        // read was dropped or it failed.  Or the page could have been
                        // evicted.  In that case, we must schedule a read.  This won't
                        // be as efficient as the usual path because it is only issuing
                        // a read for a single page, but it should be rare enough that
                        // it doesn't matter.
                        let pages = &mut inner.pages;
                        let read_key = read_keys.new_read(
                            offset..offset + PAGE_SIZE,
                            &mut inner.readers,
                            |p| {
                                pages.insert(p);
                            },
                        );
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
        offset: u64,
        mut read_buf: &mut [u8],
        source: &dyn ReadObjectHandle,
    ) -> Result<usize, Error> {
        // A list of all the futures for any required reads.
        let reads = FuturesUnordered::new();

        // Tracks any reads that other tasks might be working on.
        let pending_reads = FuturesUnordered::new();

        // New pages that we might need.
        let mut new_pages = Vec::new();

        // Keep track of the keys for any reads we schedule.  After read_keys has been inserted into
        // read_keys_list, care must be taken to only access read_keys whilst holding the lock on
        // inner.
        let read_keys = ReadKeys::new(self);
        pin_mut!(read_keys);

        {
            let mut inner = self.0.lock().unwrap();

            let content_size = inner.size;
            if offset >= content_size {
                return Ok(0);
            }
            let aligned_size = round_up(content_size, PAGE_SIZE).unwrap();
            let end_offset = if content_size - offset < read_buf.len() as u64 {
                if aligned_size - offset < read_buf.len() as u64 {
                    read_buf = &mut read_buf[0..(aligned_size - offset) as usize];
                }
                content_size
            } else {
                offset + read_buf.len() as u64
            };

            read_keys.as_mut().add_to_list(&mut inner.read_keys_list, end_offset);

            let Inner { pages, readers, buf, .. } = &mut *inner;
            let mut offset = offset;
            for page in pages.range(round_down(offset, PAGE_SIZE)..offset + read_buf.len() as u64) {
                let page = unsafe { page.page_mut() };

                // Handle any gap between the last page we found and this one.
                if page.offset() > offset {
                    // Schedule a read for the gap.
                    let (head, tail) = read_buf.split_at_mut((page.offset() - offset) as usize);
                    reads.push(
                        self.read_some(
                            offset,
                            head,
                            source,
                            read_keys
                                .as_mut()
                                .new_read(offset..page.offset(), readers, |p| new_pages.push(p)),
                        ),
                    );
                    read_buf = tail;
                    offset = page.offset();
                }

                let (head, tail) =
                    read_buf.split_at_mut(std::cmp::min(PAGE_SIZE as usize, read_buf.len()));

                if page.is_reading() {
                    pending_reads.push(self.wait_for_pending_read(
                        offset,
                        head,
                        source,
                        read_keys.as_mut().get_ptr(),
                    ));
                } else {
                    head.copy_from_slice(&buf[offset as usize..offset as usize + head.len()]);
                }

                read_buf = tail;
                offset += PAGE_SIZE;
            }
            // Handle the tail.
            if !read_buf.is_empty() {
                reads.push(self.read_some(
                    offset,
                    read_buf,
                    source,
                    read_keys.as_mut().new_read(offset..end_offset, readers, |p| new_pages.push(p)),
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

        let _inner = self.0.lock().unwrap();
        // Safe because we have taken the lock.
        let end_offset = unsafe { *read_keys.end_offset.get() };
        if end_offset > offset {
            Ok((end_offset - offset) as usize)
        } else {
            Ok(0)
        }
    }
}

// ReadKeys is a structure that tracks in-flight reads and cleans them up if dropped.
#[pin_project(PinnedDrop, !Unpin)]
struct ReadKeys {
    // Any reads that are taking place are chained together in a doubly linked list. The nodes
    // are pinned to the stack. The chain field holds the previous and next pointers.
    chain: StackListChain<ReadKeys>,

    // Store a pointer back to MemDataBuffer so that we can tidy up when we are dropped.  This is a
    // pointer rather than a reference so that we don't have to transmute away the lifetimes when we
    // insert and remove from the stack list.  It's safe because ReadKeys is always created on the
    // stack when we have a reference to MemDataBuffer so the reference will outlive ReadKeys and it
    // will be pinned.
    buf: *const MemDataBuffer,

    // A vector of keys which refer to elements in the readers slab.
    keys: Vec<usize>,

    // We record the end offset of the read so that if the buffer is truncated whilst we are issuing
    // reads, we can detect this and truncate the read result.  It is safe to modify this field so
    // long as the lock that guards Inner is held.
    end_offset: UnsafeCell<u64>,
}

unsafe impl Send for ReadKeys {}

impl ReadKeys {
    fn new(buf: &MemDataBuffer) -> Self {
        ReadKeys {
            chain: Default::default(),
            buf,
            keys: Vec::new(),
            end_offset: UnsafeCell::new(0),
        }
    }

    // Returns a new read key for a read for `range`.  It will append new pages for the read that
    // will get properly cleaned up if dropped.
    fn new_read(
        self: Pin<&mut Self>,
        range: Range<u64>,
        readers: &mut Slab<ReadContext>,
        mut new_page_fn: impl FnMut(BoxedPage),
    ) -> usize {
        let range = round_down(range.start, PAGE_SIZE)..round_up(range.end, PAGE_SIZE).unwrap();
        let read_key = readers.insert(ReadContext { range: range.clone(), wait_event: None });
        self.project().keys.push(read_key);
        for offset in range.step_by(PAGE_SIZE as usize) {
            new_page_fn(BoxedPage(Box::new(PageCell(UnsafeCell::new(Page::new(
                offset, read_key,
            ))))));
        }
        read_key
    }

    fn add_to_list(mut self: Pin<&mut Self>, list: &mut StackList<ReadKeys>, end_offset: u64) {
        *self.as_mut().project().end_offset.get_mut() = end_offset;
        // This is safe because we remove ourselves from the list in drop.
        unsafe {
            list.push_front(self);
        }
    }

    fn get_ptr(self: Pin<&mut Self>) -> ReadKeysPtr {
        // This is safe because we only use this pointer via the get method below.
        ReadKeysPtr(unsafe { self.get_unchecked_mut() })
    }
}

// Holds a reference to a ReadKeys instance which can be dereferenced with a mutable borrow of the
// list.
struct ReadKeysPtr(*mut ReadKeys);

unsafe impl Send for ReadKeysPtr {}

impl ReadKeysPtr {
    // Allow a dereference provided we have a mutable borrow for the list which ensures exclusive
    // access.
    fn get<'b>(&self, _list: &'b mut StackList<ReadKeys>) -> Pin<&'b mut ReadKeys> {
        // Safe because we are taking a mutable borrow on the list.
        unsafe { Pin::new_unchecked(&mut *self.0) }
    }
}

#[pinned_drop]
impl PinnedDrop for ReadKeys {
    fn drop(self: Pin<&mut Self>) {
        // This is safe because ReadKeys is kept on the stack and we always have &MemDataBuffer when
        // we create ReadKeys.
        let buf = unsafe { &*self.buf };
        let mut inner = buf.0.lock().unwrap();
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
        inner.read_keys_list.erase_node(&self);
    }
}

impl AsRef<StackListChain<ReadKeys>> for ReadKeys {
    fn as_ref(&self) -> &StackListChain<ReadKeys> {
        &self.chain
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

    #[fasync::run_singlethreaded(test)]
    async fn test_read_and_truncate() {
        let data_buf = MemDataBuffer::new(100 * PAGE_SIZE);
        let device = Arc::new(FakeDevice::new(100, PAGE_SIZE as u32));
        let source = FakeSource::new(device.clone());
        let mut buf = [0; PAGE_SIZE as usize];
        let mut buf2 = [0; PAGE_SIZE as usize * 2];
        let mut buf3 = [0; PAGE_SIZE as usize * 2];
        let mut read_fut = data_buf.read(0, buf.as_mut(), &source);
        let mut read_fut2 = data_buf.read(0, buf2.as_mut(), &source);
        let mut read_fut3 = data_buf.read(PAGE_SIZE, buf3.as_mut(), &source);

        // Poll the futures once.
        poll_fn(|ctx| {
            assert!(matches!(read_fut.poll_unpin(ctx), Poll::Pending));
            assert!(matches!(read_fut2.poll_unpin(ctx), Poll::Pending));
            assert!(matches!(read_fut3.poll_unpin(ctx), Poll::Pending));
            Poll::Ready(())
        })
        .await;

        // Truncate the buffer.
        data_buf.resize(20).await;
        data_buf.resize(PAGE_SIZE + 10).await;

        // Let the reads continue.
        source.go.signal();

        // All should return the truncated size.
        assert_eq!(read_fut.await.expect("read failed"), 20);
        assert_eq!(read_fut2.await.expect("read failed"), 20);
        assert_eq!(read_fut3.await.expect("read failed"), 0);

        // Another read should return the current size.
        assert_eq!(
            data_buf.read(PAGE_SIZE - 1, buf.as_mut(), &source).await.expect("read failed"),
            11
        );
    }
}
