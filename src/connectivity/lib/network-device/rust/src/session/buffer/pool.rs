// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia netdevice buffer pool.

use parking_lot::Mutex;
use std::collections::VecDeque;
use std::io::{Read, Seek, SeekFrom, Write};
use std::mem::{ManuallyDrop, MaybeUninit};
use std::ops::{Deref, DerefMut};
use std::ptr::NonNull;
use std::sync::Arc;
use std::{
    convert::{TryFrom, TryInto as _},
    fmt::Debug,
    iter::FromIterator,
};

use fidl_fuchsia_hardware_network as netdev;
use fuchsia_runtime::vmar_root_self;
use fuchsia_zircon as zx;
use futures::channel::oneshot::{channel, Receiver, Sender};

use super::{ChainLength, DescId, DescRef, DescRefMut, Descriptors};
use crate::error::{Error, Result};
use crate::session::{BufferLayout, Config, Pending};

/// Responsible for managing [`Buffer`]s for a [`Session`](crate::session::Session).
pub(in crate::session) struct Pool {
    /// Base address of the pool.
    // Note: This field requires us to manually implement `Sync` and `Send`.
    base: NonNull<u8>,
    /// The length of the pool in bytes.
    bytes: usize,
    /// The descriptors allocated for the pool.
    descriptors: Descriptors,
    /// Shared state for allocation.
    tx_alloc_state: Mutex<TxAllocState>,
    /// The free rx descriptors pending to be sent to driver.
    pub(in crate::session) rx_pending: Pending<Rx>,
    /// The buffer layout.
    buffer_layout: BufferLayout,
}

// `Pool` is `Send` and `Sync`, and this allows the compiler to deduce `Buffer`
// to be `Send`. These impls are safe because we can safely share `Pool` and
// `&Pool`: the implementation would never allocate the same buffer to two
// callers at the same time.
unsafe impl Send for Pool {}
unsafe impl Sync for Pool {}

/// The shared state which keeps track of available buffers and tx buffers.
struct TxAllocState {
    /// All pending tx allocation requests.
    requests: VecDeque<TxAllocReq>,
    free_list: TxFreeList,
}

/// We use a linked list to maintain the tx free descriptors - they are linked
/// through their `nxt` fields, note this differs from the chaining expected
/// by the network device protocol:
/// - You can chain more than [`netdev::MAX_DESCRIPTOR_CHAIN`] descriptors
///   together.
/// - the free-list ends when the `nxt` field is 0xff, while the normal chain
///   ends when `chain_length` becomes 0.
struct TxFreeList {
    /// The head of a linked list of available descriptors that can be allocated
    /// for tx.
    head: Option<DescId<Tx>>,
    /// How many free descriptors are there in the pool.
    len: u16,
}

impl Pool {
    /// Creates a new [`Pool`] and its backing [`zx::Vmo`]s.
    ///
    /// Returns [`Pool`] and the [`zx::Vmo`]s for descriptors and data, in that
    /// order.
    pub(in crate::session) fn new(config: Config) -> Result<(Arc<Self>, zx::Vmo, zx::Vmo)> {
        let Config { buffer_stride, num_rx_buffers, num_tx_buffers, options: _, buffer_layout } =
            config;
        let num_buffers = num_rx_buffers.get() + num_tx_buffers.get();
        let (descriptors, descriptors_vmo, tx_free, mut rx_free) =
            Descriptors::new(num_tx_buffers, num_rx_buffers, buffer_stride)?;

        // Construct the free list.
        let free_head = tx_free.into_iter().rev().fold(None, |head, mut curr| {
            descriptors.borrow_mut(&mut curr).set_nxt(head);
            Some(curr)
        });

        for rx_desc in rx_free.iter_mut() {
            descriptors.borrow_mut(rx_desc).initialize(
                ChainLength::ZERO,
                0,
                buffer_layout.length.try_into().unwrap(),
                0,
            );
        }

        let tx_alloc_state = TxAllocState {
            free_list: TxFreeList { head: free_head, len: num_tx_buffers.get() },
            requests: VecDeque::new(),
        };

        let size = buffer_stride.get() * u64::from(num_buffers);
        let data_vmo = zx::Vmo::create(size).map_err(|status| Error::Vmo("data", status))?;
        // `as` is OK because `size` is positive and smaller than isize::MAX.
        // This is following the practice of rust stdlib to ensure allocation
        // size never reaches isize::MAX.
        // https://doc.rust-lang.org/std/primitive.pointer.html#method.add-1.
        let len = isize::try_from(size).expect("VMO size larger than isize::MAX") as usize;
        // The returned address of zx_vmar_map on success must be non-zero:
        // https://fuchsia.dev/fuchsia-src/reference/syscalls/vmar_map
        let base = NonNull::new(
            vmar_root_self()
                .map(0, &data_vmo, 0, len, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE)
                .map_err(|status| Error::Map("data", status))? as *mut u8,
        )
        .unwrap();

        Ok((
            Arc::new(Pool {
                base,
                bytes: len,
                descriptors,
                tx_alloc_state: Mutex::new(tx_alloc_state),
                rx_pending: Pending::new(rx_free),
                buffer_layout,
            }),
            descriptors_vmo,
            data_vmo,
        ))
    }

    /// Allocates `num_parts` tx descriptors.
    ///
    /// It will block if there are not enough descriptors. Note that the
    /// descriptors are not initialized, you need to call [`AllocGuard::init()`]
    /// on the returned [`AllocGuard`] if you want to send it to the driver
    /// later. See [`AllocGuard<Rx>::into_tx()`] for an example where
    /// [`AllocGuard::init()`] is not needed because the tx allocation will be
    /// returned to the pool immediately and won't be sent to the driver.
    pub(in crate::session) async fn alloc_tx(
        self: &Arc<Self>,
        num_parts: ChainLength,
    ) -> AllocGuard<Tx> {
        let receiver = {
            let mut state = self.tx_alloc_state.lock();
            match state.free_list.try_alloc(num_parts, &self.descriptors) {
                Some(allocated) => {
                    return AllocGuard::new(allocated, self.clone());
                }
                None => {
                    let (request, receiver) = TxAllocReq::new(num_parts);
                    state.requests.push_back(request);
                    receiver
                }
            }
        };
        // The sender must not be dropped.
        receiver.await.unwrap()
    }

    /// Allocates a tx [`Buffer`].
    ///
    /// The returned buffer will have at least `num_bytes` as size, the method
    /// will block if there are not enough buffers.
    pub(in crate::session) async fn alloc_tx_buffer(
        self: &Arc<Self>,
        num_bytes: usize,
    ) -> Result<Buffer<Tx>> {
        let BufferLayout { min_tx_data, min_tx_head, min_tx_tail, length: buffer_length } =
            self.buffer_layout;
        if num_bytes < min_tx_data {
            return Err(Error::TxLength(num_bytes, min_tx_data));
        }
        let tx_head = usize::from(min_tx_head);
        let tx_tail = usize::from(min_tx_tail);
        let total_bytes = num_bytes + tx_head + tx_tail;
        let num_parts = (total_bytes + buffer_length - 1) / buffer_length;
        let mut alloc = self.alloc_tx(ChainLength::try_from(num_parts)?).await;
        alloc.init();
        alloc.try_into()
    }

    /// Frees rx descriptors.
    pub(in crate::session) fn free_rx(&self, descs: impl IntoIterator<Item = DescId<Rx>>) {
        self.rx_pending.extend(descs.into_iter().map(|mut desc| {
            self.descriptors.borrow_mut(&mut desc).initialize(
                ChainLength::ZERO,
                0,
                self.buffer_layout.length.try_into().unwrap(),
                0,
            );
            desc
        }));
    }

    /// Frees tx descriptors.
    ///
    /// # Panics
    ///
    /// Panics if given an empty chain.
    fn free_tx(self: &Arc<Self>, chain: Chained<DescId<Tx>>) {
        let free_impl = |free_list: &mut TxFreeList, chain: Chained<DescId<Tx>>| {
            let mut descs = chain.into_iter();
            // The following can't overflow because we can have at most u16::MAX
            // descriptors: free_len + #(to_free) + #(descs in use) <= u16::MAX,
            // Thus free_len + #(to_free) <= u16::MAX.
            free_list.len += u16::try_from(descs.len()).unwrap();
            let head = descs.next();
            let old_head = std::mem::replace(&mut free_list.head, head);
            let mut tail = descs.last();
            let mut tail_ref = self
                .descriptors
                .borrow_mut(tail.as_mut().unwrap_or(free_list.head.as_mut().unwrap()));
            tail_ref.set_nxt(old_head);
        };

        let mut state = self.tx_alloc_state.lock();
        let TxAllocState { requests, free_list } = &mut *state;
        let () = free_impl(free_list, chain);

        // After putting the chain back into the free list, we try to fulfill
        // any pending tx allocation requests.
        while let Some(req) = requests.front() {
            match free_list.try_alloc(req.size, &self.descriptors) {
                Some(descs) => {
                    // The unwrap is safe because we know requests is not empty.
                    match requests
                        .pop_front()
                        .unwrap()
                        .sender
                        .send(AllocGuard::new(descs, self.clone()))
                        .map_err(ManuallyDrop::new)
                    {
                        Ok(()) => {}
                        Err(mut alloc) => {
                            let AllocGuard { descs, pool } = alloc.deref_mut();
                            // We can't run the Drop code for AllocGuard here to
                            // return the descriptors though, because we are holding
                            // the lock on the alloc state and the lock is not
                            // reentrant, so we manually free the descriptors.
                            let () =
                                free_impl(free_list, std::mem::replace(descs, Chained::empty()));
                            // Safety: alloc is wrapped in ManuallyDrop, so alloc.pool
                            // will not be dropped twice.
                            let () = unsafe {
                                std::ptr::drop_in_place(pool);
                            };
                        }
                    }
                }
                None => {
                    if req.sender.is_canceled() {
                        let _cancelled: Option<TxAllocReq> = requests.pop_front();
                        continue;
                    } else {
                        break;
                    }
                }
            }
        }
    }

    /// Frees the completed tx descriptors chained by head to the pool.
    ///
    /// Call this function when the driver hands back a completed tx descriptor.
    pub(in crate::session) fn tx_completed(self: &Arc<Self>, head: DescId<Tx>) -> Result<()> {
        let chain = self.descriptors.chain(head).collect::<Result<Chained<_>>>()?;
        Ok(self.free_tx(chain))
    }

    /// Creates a [`Buffer<Rx>`] corresponding to the completed rx descriptors.
    ///
    /// Whenever the driver hands back a completed rx descriptor, this function
    /// can be used to create the buffer that is represented by those chained
    /// descriptors.
    pub(in crate::session) fn rx_completed(
        self: &Arc<Self>,
        head: DescId<Rx>,
    ) -> Result<Buffer<Rx>> {
        let descs = self.descriptors.chain(head).collect::<Result<Chained<_>>>()?;
        let alloc = AllocGuard::new(descs, self.clone());
        alloc.try_into()
    }
}

impl Drop for Pool {
    fn drop(&mut self) {
        unsafe {
            vmar_root_self()
                .unmap(self.base.as_ptr() as usize, self.bytes)
                .expect("failed to unmap VMO for Pool")
        }
    }
}

impl TxFreeList {
    /// Tries to allocate tx descriptors.
    ///
    /// Returns [`None`] if there are not enough descriptors.
    fn try_alloc(
        &mut self,
        num_parts: ChainLength,
        descriptors: &Descriptors,
    ) -> Option<Chained<DescId<Tx>>> {
        if u16::from(num_parts.get()) > self.len {
            return None;
        }

        let free_list = std::iter::from_fn(|| -> Option<DescId<Tx>> {
            let new_head = self.head.as_ref().and_then(|head| {
                let nxt = descriptors.borrow(head).nxt();
                nxt.map(|id| unsafe {
                    // Safety: This is the nxt field of head of the free list,
                    // it must be a tx descriptor id.
                    DescId::from_raw(id)
                })
            });
            std::mem::replace(&mut self.head, new_head)
        });
        let allocated = free_list.take(num_parts.get().into()).collect::<Chained<_>>();
        assert_eq!(allocated.len(), num_parts.into());
        self.len -= u16::from(num_parts.get());
        Some(allocated)
    }
}

/// The buffer that can be used by the [`Session`](crate::session::Session).
///
/// All [`Buffer`]s implement [`std::io::Read`] and [`Buffer<Tx>`]s implement
/// [`std::io::Write`].
pub struct Buffer<K: AllocKind> {
    /// The descriptors allocation.
    alloc: AllocGuard<K>,
    /// Underlying memory regions.
    parts: Chained<BufferPart>,
    /// The current absolute position to read/write within the [`Buffer`].
    pos: usize,
}

impl<K: AllocKind> Debug for Buffer<K> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Self { alloc, parts, pos } = self;
        f.debug_struct("Buffer")
            .field("len", &self.cap())
            .field("alloc", alloc)
            .field("parts", parts)
            .field("pos", pos)
            .finish()
    }
}

impl<K: AllocKind> Buffer<K> {
    /// Gets the capacity of the buffer in bytes as requested for allocation.
    pub fn cap(&self) -> usize {
        self.parts.iter().fold(0, |acc, part| acc + part.cap)
    }

    /// Gets the length of the buffer which is actually used.
    pub fn len(&self) -> usize {
        self.parts.iter().fold(0, |acc, part| acc + part.len)
    }

    /// Writes bytes to the buffer.
    ///
    /// Writes up to `src.len()` bytes into the buffer beginning at `offset`,
    /// returning how many bytes were written successfully. Partial write is
    /// not considered as an error.
    pub fn write_at(&mut self, offset: usize, src: &[u8]) -> Result<usize> {
        let mut part_start = 0;
        let mut total = 0;
        for part in self.parts.iter_mut() {
            if offset + total < part_start + part.cap {
                let written = part.write_at(offset + total - part_start, &src[total..])?;
                total += written;
                if total == src.len() {
                    break;
                }
            } else {
                part.len = part.cap;
            }
            part_start += part.cap;
        }
        Ok(total)
    }

    /// Reads bytes from the buffer.
    ///
    /// Reads up to `dst.len()` bytes from the buffer beginning at `offset`,
    /// returning how many bytes were read successfully. Partial read is
    /// not considered as an error.
    pub fn read_at(&self, offset: usize, dst: &mut [u8]) -> Result<usize> {
        let mut part_start = 0;
        let mut total = 0;
        for part in self.parts.iter() {
            if offset + total < part_start + part.cap {
                let read = part.read_at(offset + total - part_start, &mut dst[total..])?;
                total += read;
                if total == dst.len() {
                    break;
                }
            }
            part_start += part.cap;
        }
        Ok(total)
    }

    /// Pads the [`Buffer`] to minimum tx buffer length requirements.
    pub(in crate::session) fn pad(&mut self) -> Result<()> {
        let num_parts = self.parts.len();
        let BufferLayout { min_tx_tail, min_tx_data: mut target, min_tx_head: _, length: _ } =
            self.alloc.pool.buffer_layout;
        for (i, part) in self.parts.iter_mut().enumerate() {
            let grow_cap = if i == num_parts - 1 {
                let descriptor =
                    self.alloc.descriptors().last().expect("descriptor must not be empty");
                let data_length = descriptor.data_length();
                let tail_length = descriptor.tail_length();
                // data_length + tail_length <= buffer_length <= usize::MAX.
                let rest = usize::try_from(data_length).unwrap() + usize::from(tail_length);
                Some(
                    rest.checked_sub(usize::from(min_tx_tail))
                        .ok_or_else(|| Error::TxLength(rest, min_tx_tail.into()))?,
                )
            } else {
                None
            };
            target -= part.pad(target, grow_cap)?;
        }
        if target != 0 {
            return Err(Error::Pad(target, self.cap()));
        }
        Ok(())
    }

    /// Leaks the underlying buffer descriptors to the driver.
    ///
    /// Returns the head of the leaked allocation.
    pub(in crate::session) fn leak(mut self) -> DescId<K> {
        let descs = std::mem::replace(&mut self.alloc.descs, Chained::empty());
        descs.into_iter().next().unwrap()
    }

    /// Retrieves the frame type of the buffer.
    pub fn frame_type(&self) -> Result<netdev::FrameType> {
        self.alloc.descriptor().frame_type()
    }
}

impl Buffer<Tx> {
    /// Commits the metadata for the buffer to descriptors.
    pub(in crate::session) fn commit(&mut self) {
        for (part, mut descriptor) in self.parts.iter_mut().zip(self.alloc.descriptors_mut()) {
            // The following unwrap is safe because part.len must be smaller than
            // buffer_length, which is a u32.
            descriptor.commit(u32::try_from(part.len).unwrap())
        }
    }

    /// Sets the frame type of the buffer.
    pub fn set_frame_type(&mut self, frame_type: netdev::FrameType) {
        self.alloc.descriptor_mut().set_frame_type(frame_type)
    }

    /// Sets TxFlags of a Tx buffer.
    pub fn set_tx_flags(&mut self, flags: netdev::TxFlags) {
        self.alloc.descriptor_mut().set_tx_flags(flags)
    }
}

impl Buffer<Rx> {
    /// Turns an rx buffer into a tx one.
    pub async fn into_tx(self) -> Buffer<Tx> {
        let Buffer { alloc, parts, pos } = self;
        Buffer { alloc: alloc.into_tx().await, parts, pos }
    }

    /// Retrieves RxFlags of an Rx Buffer.
    pub fn rx_flags(&self) -> Result<netdev::RxFlags> {
        self.alloc.descriptor().rx_flags()
    }
}

impl AllocGuard<Rx> {
    /// Turns a tx allocation into an rx one.
    ///
    /// To achieve this we have to convert the same amount of descriptors from
    /// the tx pool to the rx pool to compensate for us being converted to tx
    /// descriptors from rx ones.
    async fn into_tx(mut self) -> AllocGuard<Tx> {
        let mut tx = self.pool.alloc_tx(self.descs.len).await;
        // [MaybeUninit<DescId<Tx>; 4] and [MaybeUninit<DescId<Rx>; 4] have the
        // same memory layout because DescId is repr(transparent). So it is safe
        // to transmute and swap the values between the storages. After the swap
        // the drop implementation of self will return the descriptors back to
        // rx pool.
        std::mem::swap(&mut self.descs.storage, unsafe {
            std::mem::transmute(&mut tx.descs.storage)
        });
        tx
    }
}

/// A non-empty container that has at most [`netdev::MAX_DESCRIPTOR_CHAIN`] elements.
struct Chained<T> {
    storage: [MaybeUninit<T>; netdev::MAX_DESCRIPTOR_CHAIN as usize],
    len: ChainLength,
}

impl<T> Deref for Chained<T> {
    type Target = [T];

    fn deref(&self) -> &Self::Target {
        // Safety: `self.storage[..self.len]` is already initialized.
        unsafe { std::mem::transmute(&self.storage[..self.len.into()]) }
    }
}

impl<T> DerefMut for Chained<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // Safety: `self.storage[..self.len]` is already initialized.
        unsafe { std::mem::transmute(&mut self.storage[..self.len.into()]) }
    }
}

impl<T> Drop for Chained<T> {
    fn drop(&mut self) {
        for elem in &mut self.storage[..self.len.into()] {
            // Safety: `self.storage[..self.len]` is already initialized.
            unsafe {
                std::ptr::drop_in_place(elem.as_mut_ptr());
            }
        }
    }
}

impl<T: Debug> Debug for Chained<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_list().entries(self.iter()).finish()
    }
}

impl<T> Chained<T> {
    fn empty() -> Self {
        // Create an uninitialized array of `MaybeUninit`. The `assume_init` is
        // safe because the type we are claiming to have initialized here is a
        // bunch of `MaybeUninit`s, which do not require initialization.
        // TODO(https://fxbug.dev/80114): use MaybeUninit::uninit_array once it
        // is stablized.
        // https://doc.rust-lang.org/std/mem/union.MaybeUninit.html#method.uninit_array
        Self { storage: unsafe { MaybeUninit::uninit().assume_init() }, len: ChainLength::ZERO }
    }
}

impl<T> FromIterator<T> for Chained<T> {
    /// # Panics
    ///
    /// if the iterator is empty or the iterator can yield more than
    ///  MAX_DESCRIPTOR_CHAIN elements.
    fn from_iter<I: IntoIterator<Item = T>>(elements: I) -> Self {
        let mut result = Self::empty();
        let mut len = 0u8;
        for (idx, e) in elements.into_iter().enumerate() {
            result.storage[idx] = MaybeUninit::new(e);
            len += 1;
        }
        assert!(len > 0);
        // `len` can not be larger than `MAX_DESCRIPTOR_CHAIN`, otherwise we can't
        // get here due to the bound checks on `result.storage`.
        result.len = ChainLength::try_from(len).unwrap();
        result
    }
}

impl<T> IntoIterator for Chained<T> {
    type Item = T;
    type IntoIter = ChainedIter<T>;

    fn into_iter(mut self) -> Self::IntoIter {
        let len = self.len;
        self.len = ChainLength::ZERO;
        // Safety: we have reset the length to zero, it is now safe to move out
        // the values and set them to be uninitialized.
        let storage =
            std::mem::replace(&mut self.storage, unsafe { MaybeUninit::uninit().assume_init() });
        ChainedIter { storage, len, consumed: 0 }
    }
}

struct ChainedIter<T> {
    storage: [MaybeUninit<T>; netdev::MAX_DESCRIPTOR_CHAIN as usize],
    len: ChainLength,
    consumed: u8,
}

impl<T> Iterator for ChainedIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        if self.consumed < self.len.get() {
            // Safety: it is safe now to replace that slot with an uninitialized
            // value because we will advance consumed by 1.
            let value = unsafe {
                std::mem::replace(
                    &mut self.storage[usize::from(self.consumed)],
                    MaybeUninit::uninit(),
                )
                .assume_init()
            };
            self.consumed += 1;
            Some(value)
        } else {
            None
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let len = usize::from(self.len.get() - self.consumed);
        (len, Some(len))
    }
}

impl<T> ExactSizeIterator for ChainedIter<T> {}

impl<T> Drop for ChainedIter<T> {
    fn drop(&mut self) {
        for i in self.consumed..self.len.get() {
            unsafe {
                std::ptr::drop_in_place(self.storage[usize::from(i)].as_mut_ptr());
            }
        }
    }
}

/// Guards the allocated descriptors; they will be freed when dropped.
pub(in crate::session) struct AllocGuard<K: AllocKind> {
    descs: Chained<DescId<K>>,
    pool: Arc<Pool>,
}

impl<K: AllocKind> Debug for AllocGuard<K> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Self { descs, pool: _ } = self;
        f.debug_struct("AllocGuard").field("descs", descs).finish()
    }
}

impl<K: AllocKind> AllocGuard<K> {
    fn new(descs: Chained<DescId<K>>, pool: Arc<Pool>) -> Self {
        Self { descs, pool }
    }

    /// Iterates over references to the descriptors.
    fn descriptors(&self) -> impl Iterator<Item = DescRef<'_, K>> + '_ {
        self.descs.iter().map(move |desc| self.pool.descriptors.borrow(desc))
    }

    /// Iterates over mutable references to the descriptors.
    fn descriptors_mut(&mut self) -> impl Iterator<Item = DescRefMut<'_, K>> + '_ {
        let descriptors = &self.pool.descriptors;
        self.descs.iter_mut().map(move |desc| descriptors.borrow_mut(desc))
    }

    /// Gets a reference to the head descriptor.
    fn descriptor(&self) -> DescRef<'_, K> {
        self.descriptors().next().expect("descriptors must not be empty")
    }

    /// Gets a mutable reference to the head descriptor.
    fn descriptor_mut(&mut self) -> DescRefMut<'_, K> {
        self.descriptors_mut().next().expect("descriptors must not be empty")
    }
}

impl AllocGuard<Tx> {
    /// Initializes descriptors of a tx allocation.
    fn init(&mut self) {
        let len = self.len();
        let BufferLayout { min_tx_head, min_tx_tail, length: buffer_length, min_tx_data: _ } =
            self.pool.buffer_layout;
        for (mut descriptor, clen) in self.descriptors_mut().zip((0..len).rev()) {
            let chain_length = ChainLength::try_from(clen).unwrap();
            let head_length = if clen + 1 == len { min_tx_head } else { 0 };
            let tail_length = if clen == 0 { min_tx_tail } else { 0 };
            // buffer_length is guaranteed to be larger than the sum of
            // head_length and tail_length. The check was done when the config
            // for pool was created, so the subtraction won't overflow.
            let data_length =
                u32::try_from(buffer_length - usize::from(head_length) - usize::from(tail_length))
                    .unwrap();
            descriptor.initialize(chain_length, head_length, data_length, tail_length);
        }
    }
}

impl<K: AllocKind> Drop for AllocGuard<K> {
    fn drop(&mut self) {
        if self.is_empty() {
            return;
        }
        K::free(private::Allocation(self));
    }
}

impl<K: AllocKind> Deref for AllocGuard<K> {
    type Target = [DescId<K>];

    fn deref(&self) -> &Self::Target {
        self.descs.deref()
    }
}

/// A contiguous region of the buffer; corresponding to one descriptor.
///
/// [`BufferPart`] owns the memory range [ptr, ptr+cap).
struct BufferPart {
    /// The data region starts at `ptr`.
    ptr: *mut u8,
    /// The capacity for the region is `cap`.
    cap: usize,
    /// Used to indicate how many bytes are actually in the buffer, it
    /// starts as 0 for a tx buffer and as `cap` for a rx buffer. It will
    /// be used later as `data_length` in the descriptor.
    len: usize,
}

impl BufferPart {
    /// Creates a new [`BufferPart`] that owns the memory region.
    ///
    /// # Safety
    ///
    /// The caller must make sure the memory pointed by `ptr` lives longer than
    /// `BufferPart` being constructed. Once a BufferPart is constructed, it is
    /// assumed that the memory `[ptr..ptr+cap)` is always valid to read and
    /// write.
    unsafe fn new(ptr: *mut u8, cap: usize, len: usize) -> Self {
        Self { ptr, cap, len }
    }

    /// Reads bytes from this buffer part.
    ///
    /// Reads up to `dst.len()` bytes from the region beginning at `offset`,
    /// returning how many bytes were read successfully. Partial read is
    /// not considered as an error.
    fn read_at(&self, offset: usize, dst: &mut [u8]) -> Result<usize> {
        let available = self.len.checked_sub(offset).ok_or(Error::Index(offset, self.len))?;
        let to_copy = std::cmp::min(available, dst.len());
        // Safety: both source memory region is valid for read the destination
        // memory region is valid for write.
        unsafe { std::ptr::copy_nonoverlapping(self.ptr.add(offset), dst.as_mut_ptr(), to_copy) }
        Ok(to_copy)
    }

    /// Writes bytes to this buffer part.
    ///
    /// Writes up to `src.len()` bytes into the region beginning at `offset`,
    /// returning how many bytes were written successfully. Partial write is
    /// not considered as an error.
    fn write_at(&mut self, offset: usize, src: &[u8]) -> Result<usize> {
        let available = self.cap.checked_sub(offset).ok_or(Error::Index(offset, self.cap))?;
        let to_copy = std::cmp::min(src.len(), available);
        // Safety: both source memory region is valid for read the destination
        // memory region is valid for write.
        unsafe { std::ptr::copy_nonoverlapping(src.as_ptr(), self.ptr.add(offset), to_copy) }
        self.len = std::cmp::max(self.len, offset + to_copy);
        Ok(to_copy)
    }

    /// Pads this part of buffer to have length `target`.
    ///
    /// `limit` describes the limit for this region to grow beyond capacity.
    /// `None` means the part is not allowed to grow and padding must be done
    /// within the existing capacity, `Some(limit)` means this part is allowed
    /// to extend its capacity up to the limit.
    fn pad(&mut self, target: usize, limit: Option<usize>) -> Result<usize> {
        if target <= self.len {
            return Ok(target);
        }
        if let Some(limit) = limit {
            if target > limit {
                return Err(Error::Pad(target, self.cap));
            }
            if self.cap < target {
                self.cap = target
            }
        }
        let new_len = std::cmp::min(target, self.cap);
        // Safety: This is safe because the destination memory region is valid
        // for write.
        unsafe {
            std::ptr::write_bytes(self.ptr.add(self.len), 0, new_len - self.len);
        }
        self.len = new_len;
        Ok(new_len)
    }
}

// `Buffer` needs to be `Send` in order to be useful in async code. Instead
// of marking `Buffer` as `Send` directly, `BufferPart` is `Send` already
// and we can let the compiler do the deduction.
unsafe impl Send for BufferPart {}

impl Debug for BufferPart {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let BufferPart { len, cap, ptr } = &self;
        f.debug_struct("BufferPart").field("ptr", ptr).field("len", len).field("cap", cap).finish()
    }
}

impl<K: AllocKind> TryFrom<AllocGuard<K>> for Buffer<K> {
    type Error = Error;

    fn try_from(alloc: AllocGuard<K>) -> Result<Self> {
        let AllocGuard { pool, descs: _ } = &alloc;
        let parts: Chained<BufferPart> = alloc
            .descriptors()
            .map(|descriptor| {
                // The following unwraps are safe because they are already
                // checked in `DeviceInfo::config`.
                let offset = usize::try_from(descriptor.offset()).unwrap();
                let head_length = usize::from(descriptor.head_length());
                let data_length = usize::try_from(descriptor.data_length()).unwrap();
                let len = match K::REFL {
                    AllocKindRefl::Tx => 0,
                    AllocKindRefl::Rx => data_length,
                };
                // Sanity check: make sure the layout is valid.
                assert!(
                    offset + head_length <= pool.bytes,
                    "buffer part starts beyond the end of pool"
                );
                assert!(
                    offset + head_length + data_length <= pool.bytes,
                    "buffer part ends beyond the end of pool"
                );
                // This is safe because the `AllocGuard` makes sure the
                // underlying memory is valid for the entire time when
                // `BufferPart` is alive; `add` is safe because
                // `offset + head_length is within the allocation and
                // smaller than isize::MAX.
                Ok(unsafe {
                    BufferPart::new(pool.base.as_ptr().add(offset + head_length), data_length, len)
                })
            })
            .collect::<Result<_>>()?;
        Ok(Self { alloc, parts, pos: 0 })
    }
}

impl<K: AllocKind> Read for Buffer<K> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let read = self
            .read_at(self.pos, buf)
            .map_err(|error| std::io::Error::new(std::io::ErrorKind::InvalidInput, error))?;
        self.pos += read;
        Ok(read)
    }
}

impl Write for Buffer<Tx> {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let written = self
            .write_at(self.pos, buf)
            .map_err(|error| std::io::Error::new(std::io::ErrorKind::InvalidInput, error))?;
        self.pos += written;
        Ok(written)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

impl<K: AllocKind> Seek for Buffer<K> {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        let pos = match pos {
            SeekFrom::Start(pos) => pos,
            SeekFrom::End(offset) => {
                let end = i64::try_from(self.cap()).map_err(|_err| zx::Status::OUT_OF_RANGE)?;
                u64::try_from(end.wrapping_add(offset)).unwrap()
            }
            SeekFrom::Current(offset) => {
                let current = i64::try_from(self.pos).map_err(|_err| zx::Status::OUT_OF_RANGE)?;
                u64::try_from(current.wrapping_add(offset)).unwrap()
            }
        };
        self.pos = usize::try_from(pos).map_err(|_err| zx::Status::OUT_OF_RANGE)?;
        Ok(pos)
    }
}

/// A pending tx allocation request.
struct TxAllocReq {
    sender: Sender<AllocGuard<Tx>>,
    size: ChainLength,
}

impl TxAllocReq {
    fn new(size: ChainLength) -> (Self, Receiver<AllocGuard<Tx>>) {
        let (sender, receiver) = channel();
        (TxAllocReq { sender, size }, receiver)
    }
}

/// A module for sealed traits so that the user of this crate can not implement
/// [`AllocKind`] for anything than [`Rx`] and [`Tx`].
mod private {
    use super::{AllocKind, Rx, Tx};
    pub trait Sealed: 'static + Sized {}
    impl Sealed for Rx {}
    impl Sealed for Tx {}

    // We can't leak a private type in a public trait, create an opaque private
    // new type for &mut super::AllocGuard so that we can mention it in the
    // AllocKind trait.
    pub struct Allocation<'a, K: AllocKind>(pub(super) &'a mut super::AllocGuard<K>);
}

/// An allocation can have two kinds, this trait provides a way to project a
/// type ([`Rx`] or [`Tx`]) into a value.
pub trait AllocKind: private::Sealed {
    /// The reflected value of Self.
    const REFL: AllocKindRefl;

    /// frees an allocation of the given kind.
    fn free(alloc: private::Allocation<'_, Self>);
}

/// A tag to related types for Tx allocations.
pub struct Tx;
/// A tag to related types for Rx allocations.
pub struct Rx;

/// The reflected value that allows inspection on an [`AllocKind`] type.
pub enum AllocKindRefl {
    Tx,
    Rx,
}

impl AllocKindRefl {
    pub(in crate::session) fn as_str(&self) -> &'static str {
        match self {
            AllocKindRefl::Tx => "Tx",
            AllocKindRefl::Rx => "Rx",
        }
    }
}

impl AllocKind for Tx {
    const REFL: AllocKindRefl = AllocKindRefl::Tx;

    fn free(alloc: private::Allocation<'_, Self>) {
        let private::Allocation(AllocGuard { pool, descs }) = alloc;
        pool.free_tx(std::mem::replace(descs, Chained::empty()));
    }
}

impl AllocKind for Rx {
    const REFL: AllocKindRefl = AllocKindRefl::Rx;

    fn free(alloc: private::Allocation<'_, Self>) {
        let private::Allocation(AllocGuard { pool, descs }) = alloc;
        pool.free_rx(std::mem::replace(descs, Chained::empty()));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;
    use futures::future::FutureExt;
    use matches::assert_matches;
    use test_case::test_case;

    use std::collections::HashSet;
    use std::num::{NonZeroU16, NonZeroU64, NonZeroUsize};
    use std::task::{Poll, Waker};

    const DEFAULT_MIN_TX_BUFFER_HEAD: u16 = 4;
    const DEFAULT_MIN_TX_BUFFER_TAIL: u16 = 8;
    // Safety: These are safe because none of the values are zero.
    const DEFAULT_BUFFER_LENGTH: NonZeroUsize = unsafe { NonZeroUsize::new_unchecked(64) };
    const DEFAULT_TX_BUFFERS: NonZeroU16 = unsafe { NonZeroU16::new_unchecked(8) };
    const DEFAULT_RX_BUFFERS: NonZeroU16 = unsafe { NonZeroU16::new_unchecked(8) };
    const MAX_BUFFER_BYTES: usize = DEFAULT_BUFFER_LENGTH.get()
        * netdev::MAX_DESCRIPTOR_CHAIN as usize
        - DEFAULT_MIN_TX_BUFFER_HEAD as usize
        - DEFAULT_MIN_TX_BUFFER_TAIL as usize;

    const SENTINEL_BYTE: u8 = 0xab;
    const WRITE_BYTE: u8 = 1;
    const PAD_BYTE: u8 = 0;

    impl Pool {
        fn new_test_default() -> Arc<Self> {
            let (pool, _descriptors, _data) = Pool::new(Config {
                buffer_stride: NonZeroU64::try_from(DEFAULT_BUFFER_LENGTH).unwrap(),
                num_rx_buffers: DEFAULT_RX_BUFFERS,
                num_tx_buffers: DEFAULT_TX_BUFFERS,
                options: netdev::SessionFlags::empty(),
                buffer_layout: BufferLayout {
                    length: DEFAULT_BUFFER_LENGTH.get(),
                    min_tx_head: DEFAULT_MIN_TX_BUFFER_HEAD,
                    min_tx_tail: DEFAULT_MIN_TX_BUFFER_TAIL,
                    min_tx_data: 0,
                },
            })
            .expect("failed to create default pool");
            pool
        }

        async fn alloc_tx_checked(self: &Arc<Self>, n: u8) -> AllocGuard<Tx> {
            self.alloc_tx(ChainLength::try_from(n).expect("failed to convert to chain length"))
                .await
        }

        fn alloc_tx_now_or_never(self: &Arc<Self>, n: u8) -> Option<AllocGuard<Tx>> {
            self.alloc_tx_checked(n).now_or_never()
        }

        fn alloc_tx_all(self: &Arc<Self>, n: u8) -> Vec<AllocGuard<Tx>> {
            std::iter::from_fn(|| self.alloc_tx_now_or_never(n)).collect()
        }

        fn alloc_tx_buffer_now_or_never(self: &Arc<Self>, num_bytes: usize) -> Option<Buffer<Tx>> {
            self.alloc_tx_buffer(num_bytes)
                .now_or_never()
                .transpose()
                .expect("invalid arguments for alloc_tx_buffer")
        }

        fn set_min_tx_buffer_length(self: &mut Arc<Self>, length: usize) {
            Arc::get_mut(self).unwrap().buffer_layout.min_tx_data = length;
        }

        fn fill_sentinel_bytes(&mut self) {
            // Safety: We have mut reference to Pool, so we get to modify the
            // VMO pointed by self.base.
            unsafe { std::ptr::write_bytes(self.base.as_ptr(), SENTINEL_BYTE, self.bytes) };
        }
    }

    impl Buffer<Tx> {
        // Write a byte at offset, the result buffer should be pad_size long, with
        // 0..offset being the SENTINEL_BYTE, offset being the WRITE_BYTE and the
        // rest being PAD_BYTE.
        fn check_write_and_pad(&mut self, offset: usize, pad_size: usize) {
            assert_eq!(
                self.write_at(offset, &[WRITE_BYTE][..]).expect("failed to write to self"),
                1
            );
            self.pad().expect("failed to pad");
            assert_eq!(self.len(), pad_size);
            // An arbitrary value that is not SENTINAL/WRITE/PAD_BYTE so that
            // we can make sure the write really happened.
            const INIT_BYTE: u8 = 42;
            let mut read_buf = vec![INIT_BYTE; pad_size];
            assert_eq!(
                self.read_at(0, &mut read_buf[..]).expect("failed to read from self"),
                pad_size
            );
            for (idx, byte) in read_buf.iter().enumerate() {
                if idx < offset {
                    assert_eq!(*byte, SENTINEL_BYTE);
                } else if idx == offset {
                    assert_eq!(*byte, WRITE_BYTE);
                } else {
                    assert_eq!(*byte, PAD_BYTE);
                }
            }
        }
    }

    impl<T: PartialEq> PartialEq for Chained<T> {
        fn eq(&self, other: &Self) -> bool {
            if self.len != other.len {
                return false;
            }
            self.iter().zip(other.iter()).all(|(l, r)| l == r)
        }
    }

    #[test]
    fn test_alloc_tx_distinct() {
        let pool = Pool::new_test_default();
        let allocated = pool.alloc_tx_all(1);
        assert_eq!(allocated.len(), DEFAULT_TX_BUFFERS.get().into());
        let distinct = allocated
            .iter()
            .map(|alloc| {
                assert_eq!(alloc.descs.len(), 1);
                alloc.descs[0].get()
            })
            .collect::<HashSet<u16>>();
        assert_eq!(allocated.len(), distinct.len());
    }

    #[test]
    fn test_alloc_tx_free_len() {
        let pool = Pool::new_test_default();
        {
            let allocated = pool.alloc_tx_all(2);
            assert_eq!(
                allocated.iter().fold(0, |acc, a| { acc + a.descs.len() }),
                DEFAULT_TX_BUFFERS.get().into()
            );
            assert_eq!(pool.tx_alloc_state.lock().free_list.len, 0);
        }
        assert_eq!(pool.tx_alloc_state.lock().free_list.len, DEFAULT_TX_BUFFERS.get());
    }

    #[test]
    fn test_alloc_tx_chain() {
        let pool = Pool::new_test_default();
        let allocated = pool.alloc_tx_all(3);
        assert_eq!(allocated.len(), usize::from(DEFAULT_TX_BUFFERS.get()) / 3);
        assert_matches!(pool.alloc_tx_now_or_never(3), None);
        assert_matches!(pool.alloc_tx_now_or_never(2), Some(a) if a.descs.len() == 2);
    }

    #[test]
    fn test_alloc_tx_after_free() {
        let pool = Pool::new_test_default();
        let mut allocated = pool.alloc_tx_all(1);
        assert_matches!(pool.alloc_tx_now_or_never(2), None);
        {
            let _drained = allocated.drain(..2);
        }
        assert_matches!(pool.alloc_tx_now_or_never(2), Some(a) if a.descs.len() == 2);
    }

    #[test]
    fn test_blocking_alloc_tx() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let pool = Pool::new_test_default();
        let mut allocated = pool.alloc_tx_all(1);
        let alloc_fut = pool.alloc_tx_checked(1);
        futures::pin_mut!(alloc_fut);
        // The allocation should block.
        assert_matches!(executor.run_until_stalled(&mut alloc_fut), Poll::Pending);
        // And the allocation request should be queued.
        assert!(!pool.tx_alloc_state.lock().requests.is_empty());
        let freed = allocated
            .pop()
            .expect("no fulfulled allocations")
            .iter()
            .map(|x| x.get())
            .collect::<Chained<_>>();
        let same_as_freed =
            |descs: &Chained<DescId<Tx>>| descs.iter().map(|x| x.get()).eq(freed.iter().copied());
        // Now the task should be able to continue.
        assert_matches!(
            &executor.run_until_stalled(&mut alloc_fut),
            Poll::Ready(AllocGuard{ descs, pool: _ }) if same_as_freed(descs)
        );
        // And the queued request should now be removed.
        assert!(pool.tx_alloc_state.lock().requests.is_empty());
    }

    #[test]
    fn test_blocking_alloc_tx_cancel_before_free() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let pool = Pool::new_test_default();
        let mut allocated = pool.alloc_tx_all(1);
        {
            let alloc_fut = pool.alloc_tx_checked(1);
            futures::pin_mut!(alloc_fut);
            assert_matches!(executor.run_until_stalled(&mut alloc_fut), Poll::Pending);
            assert!(!pool.tx_alloc_state.lock().requests.is_empty());
        }
        assert_matches!(allocated.pop(), Some(_));
        let state = pool.tx_alloc_state.lock();
        assert_eq!(state.free_list.len, 1);
        assert!(state.requests.is_empty());
    }

    #[test]
    fn test_blocking_alloc_tx_cancel_after_free() {
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let pool = Pool::new_test_default();
        let mut allocated = pool.alloc_tx_all(1);
        {
            let alloc_fut = pool.alloc_tx_checked(1);
            futures::pin_mut!(alloc_fut);
            assert_matches!(executor.run_until_stalled(&mut alloc_fut), Poll::Pending);
            assert!(!pool.tx_alloc_state.lock().requests.is_empty());
            assert_matches!(allocated.pop(), Some(_));
        }
        let state = pool.tx_alloc_state.lock();
        assert_eq!(state.free_list.len, 1);
        assert!(state.requests.is_empty());
    }

    #[test]
    fn test_multiple_blocking_alloc_tx_fulfill_order() {
        const TASKS_TOTAL: usize = 3;
        let mut executor = fasync::TestExecutor::new().expect("failed to create executor");
        let pool = Pool::new_test_default();
        let mut allocated = pool.alloc_tx_all(1);
        let mut alloc_futs = (1..=TASKS_TOTAL)
            .rev()
            .map(|x| {
                let pool = pool.clone();
                (x, Box::pin(async move { pool.alloc_tx_checked(x.try_into().unwrap()).await }))
            })
            .collect::<Vec<_>>();

        for (idx, (req_size, task)) in alloc_futs.iter_mut().enumerate() {
            assert_matches!(executor.run_until_stalled(task), Poll::Pending);
            // assert that the tasks are sorted decreasing on the requested size.
            assert_eq!(idx + *req_size, TASKS_TOTAL);
        }
        {
            let state = pool.tx_alloc_state.lock();
            // The first pending request was introduced by `alloc_tx_all`.
            assert_eq!(state.requests.len(), TASKS_TOTAL + 1);
            let mut requests = state.requests.iter();
            // It should already be cancelled because the requesting future is
            // already dropped.
            assert!(requests.next().unwrap().sender.is_canceled());
            // The rest of the requests must not be cancelled.
            assert!(requests.all(|req| !req.sender.is_canceled()))
        }

        let mut to_free = Vec::new();
        for free_size in (1..=TASKS_TOTAL).rev() {
            let (_req_size, mut task) = alloc_futs.remove(0);
            for _ in 1..free_size {
                assert_matches!(allocated.pop(), Some(_));
                assert_matches!(executor.run_until_stalled(&mut task), Poll::Pending);
            }
            assert_matches!(allocated.pop(), Some(_));
            match executor.run_until_stalled(&mut task) {
                Poll::Ready(alloc) => {
                    assert_eq!(alloc.len(), free_size);
                    // Don't return the allocation to the pool now.
                    to_free.push(alloc);
                }
                Poll::Pending => panic!("The request should be fulfilled"),
            }
            // The rest of requests can not be fulfilled.
            for (_req_size, task) in alloc_futs.iter_mut() {
                assert_matches!(executor.run_until_stalled(task), Poll::Pending);
            }
        }
        assert!(pool.tx_alloc_state.lock().requests.is_empty());
    }

    #[test]
    fn test_singleton_tx_layout() {
        let pool = Pool::new_test_default();
        let buffers = std::iter::from_fn(|| {
            let data_len = u32::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()
                - u32::from(DEFAULT_MIN_TX_BUFFER_HEAD)
                - u32::from(DEFAULT_MIN_TX_BUFFER_TAIL);
            pool.alloc_tx_buffer_now_or_never(usize::try_from(data_len).unwrap()).map(|buffer| {
                assert_eq!(buffer.alloc.descriptors().count(), 1);
                let offset = u64::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()
                    * u64::from(buffer.alloc[0].get());
                {
                    let descriptor = buffer.alloc.descriptor();
                    assert_matches!(descriptor.chain_length(), Ok(ChainLength::ZERO));
                    assert_eq!(descriptor.head_length(), DEFAULT_MIN_TX_BUFFER_HEAD);
                    assert_eq!(descriptor.tail_length(), DEFAULT_MIN_TX_BUFFER_TAIL);
                    assert_eq!(descriptor.data_length(), data_len);
                    assert_eq!(descriptor.offset(), offset);
                }

                assert_eq!(buffer.parts.len(), 1);
                let BufferPart { ptr, len, cap } = buffer.parts[0];
                assert_eq!(len, 0);
                assert_eq!(
                    // Using wrapping_add because we will never dereference the
                    // resulting pointer and it saves us an unsafe block.
                    pool.base.as_ptr().wrapping_add(
                        usize::try_from(offset).unwrap() + usize::from(DEFAULT_MIN_TX_BUFFER_HEAD),
                    ),
                    ptr
                );
                assert_eq!(data_len, u32::try_from(cap).unwrap());
                buffer
            })
        })
        .collect::<Vec<_>>();
        assert_eq!(buffers.len(), usize::from(DEFAULT_TX_BUFFERS.get()));
    }

    #[test]
    fn test_chained_tx_layout() {
        let pool = Pool::new_test_default();
        let alloc_len = 4 * DEFAULT_BUFFER_LENGTH.get()
            - usize::from(DEFAULT_MIN_TX_BUFFER_HEAD)
            - usize::from(DEFAULT_MIN_TX_BUFFER_TAIL);
        let buffers = std::iter::from_fn(|| {
            pool.alloc_tx_buffer_now_or_never(alloc_len).map(|buffer| {
                assert_eq!(buffer.parts.len(), 4);
                for (idx, descriptor) in buffer.alloc.descriptors().enumerate() {
                    let chain_length = ChainLength::try_from(buffer.alloc.len() - idx - 1).unwrap();
                    let head_length = if idx == 0 { DEFAULT_MIN_TX_BUFFER_HEAD } else { 0 };
                    let tail_length = if chain_length == ChainLength::ZERO {
                        DEFAULT_MIN_TX_BUFFER_TAIL
                    } else {
                        0
                    };
                    let data_len = u32::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()
                        - u32::from(head_length)
                        - u32::from(tail_length);
                    let offset = u64::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()
                        * u64::from(buffer.alloc[idx].get());
                    assert_eq!(descriptor.chain_length().unwrap(), chain_length);
                    assert_eq!(descriptor.head_length(), head_length);
                    assert_eq!(descriptor.tail_length(), tail_length);
                    assert_eq!(descriptor.offset(), offset);
                    assert_eq!(descriptor.data_length(), data_len);
                    if chain_length != ChainLength::ZERO {
                        assert_eq!(descriptor.nxt(), Some(buffer.alloc[idx + 1].get()));
                    }

                    let BufferPart { ptr, cap, len } = buffer.parts[idx];
                    assert_eq!(len, 0);
                    assert_eq!(
                        // Using wrapping_add because we will never dereference
                        // the resulting ptr and it saves us an unsafe block.
                        pool.base.as_ptr().wrapping_add(
                            usize::try_from(offset).unwrap() + usize::from(head_length),
                        ),
                        ptr
                    );
                    assert_eq!(data_len, u32::try_from(cap).unwrap());
                }
                buffer
            })
        })
        .collect::<Vec<_>>();
        assert_eq!(buffers.len(), usize::from(DEFAULT_TX_BUFFERS.get()) / 4);
    }

    #[test]
    fn test_rx_distinct() {
        let pool = Pool::new_test_default();
        let mut guard = pool.rx_pending.inner.lock();
        let (descs, _): &mut (Vec<_>, Option<Waker>) = &mut *guard;
        assert_eq!(descs.len(), usize::from(DEFAULT_RX_BUFFERS.get()));
        let distinct = descs.iter().map(|desc| desc.get()).collect::<HashSet<u16>>();
        assert_eq!(descs.len(), distinct.len());
    }

    #[test]
    fn test_alloc_rx_layout() {
        let pool = Pool::new_test_default();
        let mut guard = pool.rx_pending.inner.lock();
        let (descs, _): &mut (Vec<_>, Option<Waker>) = &mut *guard;
        assert_eq!(descs.len(), usize::from(DEFAULT_RX_BUFFERS.get()));
        for desc in descs.iter() {
            let descriptor = pool.descriptors.borrow(desc);
            let offset =
                u64::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap() * u64::from(desc.get());
            assert_matches!(descriptor.chain_length(), Ok(ChainLength::ZERO));
            assert_eq!(descriptor.head_length(), 0);
            assert_eq!(descriptor.tail_length(), 0);
            assert_eq!(descriptor.offset(), offset);
            assert_eq!(
                descriptor.data_length(),
                u32::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()
            );
        }
    }

    #[test]
    fn test_buffer_read_at_write_at() {
        let pool = Pool::new_test_default();
        let alloc_bytes = DEFAULT_BUFFER_LENGTH.get();
        let mut buffer =
            pool.alloc_tx_buffer_now_or_never(alloc_bytes).expect("failed to allocate");
        // Because we have to accommodate the space for head and tail, there
        // would be 2 parts instead of 1.
        assert_eq!(buffer.parts.len(), 2);
        assert_eq!(
            buffer.cap(),
            alloc_bytes * 2
                - usize::from(DEFAULT_MIN_TX_BUFFER_HEAD)
                - usize::from(DEFAULT_MIN_TX_BUFFER_TAIL)
        );
        let write_buf = (0..u8::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()).collect::<Vec<_>>();
        assert_eq!(
            buffer.write_at(0, &write_buf[..]).expect("failed to write into buffer"),
            write_buf.len()
        );
        let mut read_buf = [0xff; DEFAULT_BUFFER_LENGTH.get()];
        assert_eq!(
            buffer.read_at(0, &mut read_buf[..]).expect("failed to read from buffer"),
            read_buf.len()
        );
        for (idx, byte) in read_buf.iter().enumerate() {
            assert_eq!(*byte, write_buf[idx]);
        }
    }

    #[test]
    fn test_buffer_read_write_seek() {
        let pool = Pool::new_test_default();
        let alloc_bytes = DEFAULT_BUFFER_LENGTH.get();
        let mut buffer =
            pool.alloc_tx_buffer_now_or_never(alloc_bytes).expect("failed to allocate");
        // Because we have to accommodate the space for head and tail, there
        // would be 2 parts instead of 1.
        assert_eq!(buffer.parts.len(), 2);
        assert_eq!(
            buffer.cap(),
            alloc_bytes * 2
                - usize::from(DEFAULT_MIN_TX_BUFFER_HEAD)
                - usize::from(DEFAULT_MIN_TX_BUFFER_TAIL)
        );
        let write_buf = (0..u8::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()).collect::<Vec<_>>();
        assert_eq!(
            buffer.write(&write_buf[..]).expect("failed to write into buffer"),
            write_buf.len()
        );
        const SEEK_FROM_END: usize = 64;
        const READ_LEN: usize = 12;
        assert_eq!(
            buffer.seek(SeekFrom::End(-i64::try_from(SEEK_FROM_END).unwrap())).unwrap(),
            u64::try_from(buffer.cap() - SEEK_FROM_END).unwrap()
        );
        let mut read_buf = [0xff; READ_LEN];
        assert_eq!(
            buffer.read(&mut read_buf[..]).expect("failed to read from buffer"),
            read_buf.len()
        );
        for (idx, byte) in read_buf.iter().enumerate() {
            assert_eq!(*byte, write_buf[idx + SEEK_FROM_END - READ_LEN]);
        }
    }

    #[test_case(32; "single buffer part")]
    #[test_case(MAX_BUFFER_BYTES; "multiple buffer parts")]
    fn test_buffer_pad(pad_size: usize) {
        let mut pool = Pool::new_test_default();
        pool.set_min_tx_buffer_length(pad_size);
        for offset in 0..pad_size {
            Arc::get_mut(&mut pool)
                .expect("there are multiple owners of the underlying VMO")
                .fill_sentinel_bytes();
            let mut buffer =
                pool.alloc_tx_buffer_now_or_never(pad_size).expect("failed to allocate buffer");
            buffer.check_write_and_pad(offset, pad_size);
        }
    }

    #[test]
    fn test_buffer_pad_grow() {
        const BUFFER_PARTS: u8 = 3;
        let mut pool = Pool::new_test_default();
        let pad_size = u32::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap()
            * u32::from(BUFFER_PARTS)
            - u32::from(DEFAULT_MIN_TX_BUFFER_HEAD)
            - u32::from(DEFAULT_MIN_TX_BUFFER_TAIL);
        pool.set_min_tx_buffer_length(pad_size.try_into().unwrap());
        for offset in 0..pad_size - u32::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap() {
            Arc::get_mut(&mut pool)
                .expect("there are multiple owners of the underlying VMO")
                .fill_sentinel_bytes();
            let mut alloc =
                pool.alloc_tx_now_or_never(BUFFER_PARTS).expect("failed to alloc descriptors");
            alloc.init();
            let mut buffer = Buffer::try_from(alloc).unwrap();
            buffer.check_write_and_pad(offset.try_into().unwrap(), pad_size.try_into().unwrap());
        }
    }

    #[test_case(  0; "writes at the beginning")]
    #[test_case( 15; "writes in the first part")]
    #[test_case( 75; "writes in the second part")]
    #[test_case(135; "writes in the third part")]
    #[test_case(195; "writes in the last part")]
    fn test_buffer_used(write_offset: usize) {
        let pool = Pool::new_test_default();
        let mut buffer =
            pool.alloc_tx_buffer_now_or_never(MAX_BUFFER_BYTES).expect("failed to allocate buffer");
        let expected_caps = (0..netdev::MAX_DESCRIPTOR_CHAIN).map(|i| {
            if i == 0 {
                DEFAULT_BUFFER_LENGTH.get() - usize::from(DEFAULT_MIN_TX_BUFFER_HEAD)
            } else if i < netdev::MAX_DESCRIPTOR_CHAIN - 1 {
                DEFAULT_BUFFER_LENGTH.get()
            } else {
                DEFAULT_BUFFER_LENGTH.get() - usize::from(DEFAULT_MIN_TX_BUFFER_TAIL)
            }
        });
        assert_eq!(buffer.alloc.len(), netdev::MAX_DESCRIPTOR_CHAIN.into());
        assert_eq!(
            buffer.write_at(write_offset, &[WRITE_BYTE][..]).expect("failed to write to buffer"),
            1
        );
        // The accumulator is Some if we haven't found the part where the byte
        // was written, None if we've already found it.
        assert_eq!(
            buffer.parts.iter().zip(expected_caps).fold(
                Some(write_offset),
                |offset, (part, expected_cap)| {
                    // The cap must match the expectation.
                    assert_eq!(part.cap, expected_cap);

                    match offset {
                        Some(offset) => {
                            if offset >= expected_cap {
                                // The part should have used all the capacity.
                                assert_eq!(part.len, part.cap);
                                Some(offset - part.len)
                            } else {
                                // The part should end right after our byte.
                                assert_eq!(part.len, offset + 1);
                                let mut buf = [0];
                                // Verify that the byte is indeed written.
                                assert_matches!(part.read_at(offset, &mut buf), Ok(1));
                                assert_eq!(buf[0], WRITE_BYTE);
                                None
                            }
                        }
                        None => {
                            // We should have never written in this part.
                            assert_eq!(part.len, 0);
                            None
                        }
                    }
                }
            ),
            None
        )
    }

    #[test]
    fn test_buffer_commit() {
        let pool = Pool::new_test_default();
        for offset in 0..MAX_BUFFER_BYTES {
            let mut buffer = pool
                .alloc_tx_buffer_now_or_never(MAX_BUFFER_BYTES)
                .expect("failed to allocate buffer");
            assert_eq!(buffer.write_at(offset, &[1][..]).expect("failed to write to buffer"), 1);
            buffer.commit();
            for (part, descriptor) in buffer.parts.iter().zip(buffer.alloc.descriptors()) {
                let head_length = descriptor.head_length();
                let tail_length = descriptor.tail_length();
                let data_length = descriptor.data_length();
                assert_eq!(u32::try_from(part.len).unwrap(), data_length);
                assert_eq!(
                    u32::from(head_length + tail_length) + data_length,
                    u32::try_from(DEFAULT_BUFFER_LENGTH.get()).unwrap(),
                );
            }
        }
    }
}
