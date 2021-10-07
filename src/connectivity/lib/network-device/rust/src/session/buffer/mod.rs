// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fuchsia netdevice buffer management.

pub(super) mod pool;
mod sys;

use std::convert::TryFrom;
use std::iter;
use std::num::{NonZeroU16, NonZeroU64};
use std::ops::{Deref, DerefMut};
use std::ptr::NonNull;
use std::sync::atomic::{AtomicU8, Ordering};

use fidl_fuchsia_hardware_network as netdev;
use fuchsia_runtime::vmar_root_self;
use fuchsia_zircon::{self as zx, sys::ZX_PAGE_SIZE};
use static_assertions::const_assert_eq;

use crate::error::{Error, Result};
use types::{ChainLength, DESCID_NO_NEXT};

pub use pool::{AllocKind, Buffer, Rx, Tx};
/// Network device descriptor version.
pub(super) use sys::NETWORK_DEVICE_DESCRIPTOR_VERSION;
pub(super) use types::DescId;
/// Network device descriptor length.
pub(super) const NETWORK_DEVICE_DESCRIPTOR_LENGTH: usize =
    std::mem::size_of::<sys::buffer_descriptor>();

// Ensure that the descriptor length is always a multiple of 8.
const_assert_eq!(NETWORK_DEVICE_DESCRIPTOR_LENGTH % std::mem::size_of::<u64>(), 0);
// Ensure the alignment for BufferDescriptor allows accessing from page boundary.
const_assert_eq!(ZX_PAGE_SIZE as usize % std::mem::align_of::<Descriptor<Tx>>(), 0);
const_assert_eq!(ZX_PAGE_SIZE as usize % std::mem::align_of::<Descriptor<Rx>>(), 0);

/// A network device descriptor.
#[repr(transparent)]
struct Descriptor<K: AllocKind>(sys::buffer_descriptor, std::marker::PhantomData<K>);

impl<K: AllocKind> Descriptor<K> {
    fn frame_type(&self) -> Result<netdev::FrameType> {
        let Self(this, _marker) = self;
        let prim = this.frame_type;
        netdev::FrameType::from_primitive(prim).ok_or(Error::FrameType(prim))
    }

    fn chain_length(&self) -> Result<ChainLength> {
        let Self(this, _marker) = self;
        ChainLength::try_from(this.chain_length)
    }

    fn nxt(&self) -> Option<u16> {
        let Self(this, _marker) = self;
        if this.nxt == DESCID_NO_NEXT {
            None
        } else {
            Some(this.nxt)
        }
    }

    fn set_nxt(&mut self, desc: Option<DescId<K>>) {
        let Self(this, _marker) = self;
        this.nxt = desc.as_ref().map(DescId::get).unwrap_or(DESCID_NO_NEXT);
    }

    fn offset(&self) -> u64 {
        let Self(this, _marker) = self;
        this.offset
    }

    fn set_offset(&mut self, offset: u64) {
        let Self(this, _marker) = self;
        this.offset = offset;
    }

    fn head_length(&self) -> u16 {
        let Self(this, _marker) = self;
        this.head_length
    }

    fn data_length(&self) -> u32 {
        let Self(this, _marker) = self;
        this.data_length
    }

    fn tail_length(&self) -> u16 {
        let Self(this, _marker) = self;
        this.tail_length
    }

    /// Initializes a descriptor with the given layout.
    fn initialize(&mut self, chain_len: ChainLength, head_len: u16, data_len: u32, tail_len: u16) {
        let Self(
            sys::buffer_descriptor {
                frame_type,
                chain_length,
                // We shouldn't touch this field as it is always managed by the
                // allocation routines.
                nxt: _,
                info_type,
                port_id,
                // We shouldn't touch this field as it is reserved.
                _reserved: _,
                // We shouldn't touch this field as it is managed by DescRef{Mut}.
                client_opaque_data: _,
                // No need to initialize this because it is already initialized when
                // `Descriptors` was created and it should always stay unchanged.
                offset: _,
                head_length,
                tail_length,
                data_length,
                inbound_flags,
                return_flags,
            },
            _marker,
        ) = self;
        *frame_type = 0;
        *chain_length = chain_len.get();
        *info_type = 0;
        *port_id = 0;
        *head_length = head_len;
        *tail_length = tail_len;
        *data_length = data_len;
        *inbound_flags = 0;
        *return_flags = 0;
    }
}

impl Descriptor<Rx> {
    fn rx_flags(&self) -> Result<netdev::RxFlags> {
        let Self(this, _marker) = self;
        let bits = this.inbound_flags;
        netdev::RxFlags::from_bits(bits).ok_or(Error::RxFlags(bits))
    }
}

impl Descriptor<Tx> {
    fn set_tx_flags(&mut self, flags: netdev::TxFlags) {
        let Self(this, _marker) = self;
        let bits = flags.bits();
        this.return_flags = bits;
    }

    fn set_frame_type(&mut self, frame_type: netdev::FrameType) {
        let Self(this, _marker) = self;
        this.frame_type = frame_type.into_primitive();
    }

    /// # Panics
    ///
    /// * `used` is larger than the capacity of of the buffer.
    /// * `used` is so small that the resulting `tail` for the buffer is not
    ///   representable by a u16.
    fn commit(&mut self, used: u32) {
        let Self(this, _marker) = self;
        // The following addition can't overflow because
        // data_length + tail_length <= buffer_length <= u32::MAX.
        let total = this.data_length + u32::from(this.tail_length);
        let tail = total.checked_sub(used).unwrap();
        this.data_length = used;
        this.tail_length = u16::try_from(tail).unwrap();
    }
}

/// [`Descriptors`] is a slice of [`sys::buffer_descriptor`]s at mapped address.
///
/// A [`Descriptors`] owns a reference to its backing VMO, ensuring that its
// refcount will not reach 0 until the `Descriptors` has been dropped.
struct Descriptors {
    ptr: NonNull<sys::buffer_descriptor>,
    count: u16,
}

impl Descriptors {
    /// Creates a new [`Descriptors`].
    ///
    /// Also returns the backing [`zx::Vmo`] and the available [`DescId`]s.
    ///
    /// # Panics
    ///
    /// * `buffer_stride * total` > u64::MAX.
    fn new(
        num_tx: NonZeroU16,
        num_rx: NonZeroU16,
        buffer_stride: NonZeroU64,
    ) -> Result<(Self, zx::Vmo, Vec<DescId<Tx>>, Vec<DescId<Rx>>)> {
        let total = num_tx.get() + num_rx.get();
        let size = u64::try_from(NETWORK_DEVICE_DESCRIPTOR_LENGTH * usize::from(total))
            .expect("vmo_size overflows u64");
        let vmo = zx::Vmo::create(size).map_err(|status| Error::Vmo("descriptors", status))?;
        // The unwrap is safe because it is guaranteed that the base address
        // returned will be non-zero.
        // https://fuchsia.dev/fuchsia-src/reference/syscalls/vmar_map
        let ptr = NonNull::new(
            vmar_root_self()
                .map(
                    0,
                    &vmo,
                    0,
                    usize::try_from(size).unwrap(),
                    zx::VmarFlags::PERM_WRITE | zx::VmarFlags::PERM_READ,
                )
                .map_err(|status| Error::Map("descriptors", status))?
                as *mut sys::buffer_descriptor,
        )
        .unwrap();

        // Safety: It is required that we don't have two `DescId`s with the same
        // value. Below we create `total` DescId's and each of them will have
        // a different value.
        let mut tx =
            (0..num_tx.get()).map(|x| unsafe { DescId::<Tx>::from_raw(x) }).collect::<Vec<_>>();
        let mut rx =
            (num_tx.get()..total).map(|x| unsafe { DescId::<Rx>::from_raw(x) }).collect::<Vec<_>>();
        let descriptors = Self { ptr, count: total };
        fn init_offset<K: AllocKind>(
            descriptors: &Descriptors,
            desc: &mut DescId<K>,
            buffer_stride: NonZeroU64,
        ) {
            let offset = buffer_stride.get().checked_mul(u64::from(desc.get())).unwrap();
            descriptors.borrow_mut(desc).set_offset(offset);
        }
        tx.iter_mut().for_each(|desc| init_offset(&descriptors, desc, buffer_stride));
        rx.iter_mut().for_each(|desc| init_offset(&descriptors, desc, buffer_stride));
        Ok((descriptors, vmo, tx, rx))
    }

    /// Gets an immutable reference to the [`Descriptor`] represented by the [`DescId`].
    ///
    /// See [`ref_state`] and [`DescId`] for details.
    ///
    /// # Panics
    ///
    /// Panics if the descriptor ID is larger than the total number of descriptors.
    fn borrow<'a, 'b: 'a, K: AllocKind>(&'b self, id: &'a DescId<K>) -> DescRef<'a, K> {
        assert!(
            id.get() < self.count,
            "descriptor index out of range: {} >= {}",
            id.get(),
            self.count
        );
        unsafe { DescRef::new(self.ptr.as_ptr().add(id.get().into())) }
    }

    /// Gets a mutable reference to the [`Descriptor`] represented by the [`DescId`].
    ///
    /// See [`ref_state`] and [`DescId`] for details.
    ///
    /// # Panics
    ///
    /// Panics if the descriptor ID is larger than the total number of descriptors.
    fn borrow_mut<'a, 'b: 'a, K: AllocKind>(&'b self, id: &'a mut DescId<K>) -> DescRefMut<'a, K> {
        assert!(
            id.get() < self.count,
            "descriptor index out of range: {} >= {}",
            id.get(),
            self.count
        );
        unsafe { DescRefMut::new(self.ptr.as_ptr().add(id.get().into())) }
    }

    /// Chains the descriptors returned by the device.
    ///
    /// The iteration will go on as long as `chain_length` is not 0. The iteration
    /// will stop if `chain_length` is invalid (larger than [`netdev::MAX_DESCRIPTOR_CHAIN`]).
    fn chain<K: AllocKind>(&self, head: DescId<K>) -> impl Iterator<Item = Result<DescId<K>>> + '_ {
        iter::successors(
            Some(Ok(head)),
            move |curr: &Result<DescId<K>>| -> Option<Result<DescId<K>>> {
                match curr {
                    Err(_err) => None,
                    Ok(curr) => {
                        let descriptor = self.borrow(curr);
                        match descriptor.chain_length() {
                            Err(e) => Some(Err(e)),
                            Ok(len) => {
                                if len == ChainLength::ZERO {
                                    None
                                } else {
                                    // Safety: non-zero chain length means we can read the
                                    // the nxt field and we trust the device to give us a
                                    // valid descriptor, which should not have the same value
                                    // to any descriptor that we own.
                                    descriptor.nxt().map(|id| Ok(unsafe { DescId::from_raw(id) }))
                                }
                            }
                        }
                    }
                }
            },
        )
    }
}

// Descriptors is safe to be sent among threads.
unsafe impl Send for Descriptors {}
// Descriptors is also Sync because the refcount is backed by an atomic integer.
unsafe impl Sync for Descriptors {}

impl Drop for Descriptors {
    fn drop(&mut self) {
        // descriptor should have a small size and count is max 512 for now,
        // this can't overflow even on a 16-bit platform.
        let len = NETWORK_DEVICE_DESCRIPTOR_LENGTH * usize::from(self.count);
        let page_size = usize::try_from(zx::sys::ZX_PAGE_SIZE).unwrap();
        let aligned = (len + page_size - 1) / page_size * page_size;
        unsafe {
            vmar_root_self()
                .unmap(self.ptr.as_ptr() as usize, aligned)
                .expect("failed to unmap VMO")
        }
    }
}

/// Gets the reference count of the [`Descriptor`].
///
/// # Safety
///
/// ptr must be valid for the entire 'a lifetime; No one else should ever access
/// client_opaque_data field in this crate.
///
/// Note that the reference counting isn't necessary if all [`DescId`]s are
/// different because the rust borrow checker can guarantee we can't have two
/// conflicting references to the same descriptor at the same time. Though the
/// proof would be hard and impossible to express within rust. Also we have to
/// trust the driver to hand us sensible [`DescId`]s. The dynamic reference
/// counting is an extra layer of check to catch program errors.
///
/// The reference count is stored in an [`AtomicU8`]. u8 should be enough
/// because [`DescRef`] is not exported and we won't reborrow a descriptor
/// repeatedly in this crate. In fact, there really should be at most one
/// reference to a descriptor at a time most of the time in this crate, but we
/// do the counting anyway to respect that it is possible to create multiple
/// shared references to avoid surprises. The counter is incremented by 1 every
/// time a shared reference is created and u8::MAX is stored for an exclusive
/// reference. When creating a shared reference we check if the counter is
/// smaller than u8::MAX-1, when creating an exclusive reference we check
/// if the counter is 0.
unsafe fn ref_count<'a>(ptr: *const sys::buffer_descriptor) -> &'a AtomicU8 {
    // Safety: No one else in this crate can access ref_cnt. The intermediate
    // &u8 reference we create is correctly aligned (alignment = 1) and
    // dereferenceable (given ptr is a valid pointer).
    const_assert_eq!(std::mem::align_of::<AtomicU8>(), std::mem::align_of::<u8>());
    &*(&((*ptr).client_opaque_data[0]) as *const u8 as *const AtomicU8)
}

/// This value signals there currently is no references to the descriptor.
const DESC_REF_UNUSED: u8 = 0;
/// This value signals the descriptor is exclusively borrowed. Anything between
/// [`DESC_REF_UNUSED`] and [`DESC_REF_EXCLUSIVE`] means there are multiple
/// shared references to the descriptor.
const DESC_REF_EXCLUSIVE: u8 = u8::MAX;

/// A shared reference to a [`Descriptor`].
struct DescRef<'a, K: AllocKind> {
    ptr: &'a Descriptor<K>,
}

impl<K: AllocKind> DescRef<'_, K> {
    /// Creates a new shared reference.
    ///
    /// # Safety
    ///
    /// The caller must make sure the pointer is correctly aligned and points to
    /// valid memory. The underlying memory pointed by ptr must not not be freed
    /// within the lifetime. The caller must also make sure there is no active
    /// exclusive borrow during the lifetime of the returned [`DescRef`].
    ///
    /// # Panics
    ///
    /// Panics if there are too many shared references already (254 max) or
    /// there's an exclusive reference.
    unsafe fn new(ptr: *const sys::buffer_descriptor) -> Self {
        let ref_cnt = ref_count(ptr);
        let prev = ref_cnt.fetch_add(1, Ordering::AcqRel);
        if prev == DESC_REF_EXCLUSIVE {
            panic!("trying to create a shared reference when there is already a mutable reference");
        }
        if prev + 1 == DESC_REF_EXCLUSIVE {
            panic!("there are too many shared references")
        }
        Self { ptr: &*(ptr as *const Descriptor<K>) }
    }
}

impl<K: AllocKind> Drop for DescRef<'_, K> {
    fn drop(&mut self) {
        let ref_cnt = unsafe { ref_count(&self.ptr.0 as *const _) };
        let prev = ref_cnt.fetch_sub(1, Ordering::AcqRel);
        assert!(prev != DESC_REF_EXCLUSIVE && prev != DESC_REF_UNUSED);
    }
}

impl<K: AllocKind> Deref for DescRef<'_, K> {
    type Target = Descriptor<K>;

    fn deref(&self) -> &Self::Target {
        self.ptr
    }
}

/// An exclusive reference to the descriptor.
struct DescRefMut<'a, K: AllocKind> {
    ptr: &'a mut Descriptor<K>,
}

impl<K: AllocKind> DescRefMut<'_, K> {
    /// Creates a new exclusive reference.
    ///
    /// # Safety
    ///
    /// The caller must make sure the pointer is correctly aligned and points to
    /// valid memory. The underlying memory pointed by ptr must not not be freed
    /// within the lifetime. The caller must also make sure there is no other
    /// active borrows during the lifetime of the returned [`DescRefMut`].
    ///
    /// # Panics
    ///
    /// Panics if the descriptor is borrowed.
    unsafe fn new(ptr: *mut sys::buffer_descriptor) -> Self {
        let ref_cnt = ref_count(ptr);
        if let Err(prev) = ref_cnt.compare_exchange(
            DESC_REF_UNUSED,
            DESC_REF_EXCLUSIVE,
            Ordering::AcqRel,
            Ordering::Acquire,
        ) {
            panic!(
                "trying to create an exclusive reference when there are other references: {}",
                prev
            );
        }
        Self { ptr: &mut *(ptr as *mut Descriptor<K>) }
    }
}

impl<K: AllocKind> Drop for DescRefMut<'_, K> {
    fn drop(&mut self) {
        let ref_cnt = unsafe { ref_count(&self.ptr.0 as *const _) };
        if let Err(prev) = ref_cnt.compare_exchange(
            DESC_REF_EXCLUSIVE,
            DESC_REF_UNUSED,
            Ordering::AcqRel,
            Ordering::Acquire,
        ) {
            panic!(
                "we have a mutable reference while the descriptor is not exclusively borrowed: {}",
                prev
            );
        }
    }
}

impl<K: AllocKind> Deref for DescRefMut<'_, K> {
    type Target = Descriptor<K>;

    fn deref(&self) -> &Self::Target {
        self.ptr
    }
}

impl<K: AllocKind> DerefMut for DescRefMut<'_, K> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut *self.ptr
    }
}

/// A module to encapsulate the witness types so that users cannot create them
/// with struct literal syntax.
mod types {
    use super::{netdev, AllocKind, Error, Result};
    use fuchsia_async::FifoEntry;
    use std::convert::{TryFrom, TryInto as _};
    use std::fmt::Debug;

    /// The identifier of a descriptor.
    ///
    /// It is considered as the owner of the underlying [`Descriptor`].
    /// No two [`DescId`] with same value should co-exist at the same time at any
    /// point of the program execution. Just as in normal rust an object should
    /// not have two owners at the same time, except that rustc can't check this
    /// for us, so creating a [`DescId`] is unsafe, it is the programmers job to
    /// make sure [`DescId`]s don't alias, i.e., have different values.
    ///
    /// Also since DESCID_NO_NEXT(u16::MAX) is used to signal the end of a free
    /// list, there should be no [`DescId`] holding that value.
    #[derive(PartialEq, Eq)]
    #[repr(transparent)]
    pub(in crate::session) struct DescId<K: AllocKind>(u16, std::marker::PhantomData<K>);

    impl<K: AllocKind> Debug for DescId<K> {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            let Self(id, _marker) = self;
            f.debug_tuple(K::REFL.as_str()).field(id).finish()
        }
    }

    /// This value signals the end of the free list. We might be able to get rid
    /// of this but given the current restriction of zircon fifos, it's not a
    /// big deal to scrafice one value in the value space, we can't have that
    /// many descriptors anyway.
    pub(super) const DESCID_NO_NEXT: u16 = u16::MAX;

    // `DescId` is a `u16` which can be represented by arbitrary bit patterns.
    // It is safe to mark it as `FifoEntry`.
    unsafe impl<K: AllocKind> FifoEntry for DescId<K> {}

    impl<K: AllocKind> DescId<K> {
        // Safety: The caller needs to make sure there is no other DescId's having
        // the same value as `id` at the same time.
        pub(super) unsafe fn from_raw(id: u16) -> Self {
            assert_ne!(id, DESCID_NO_NEXT);
            Self(id, std::marker::PhantomData)
        }

        pub(super) fn get(&self) -> u16 {
            let Self(id, _marker) = self;
            *id
        }
    }

    /// A witness type that the wrapped length is less than `MAX_DESCRIPTOR_CHAIN`.
    #[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd)]
    pub(in crate::session) struct ChainLength(u8);

    impl TryFrom<u8> for ChainLength {
        type Error = Error;

        fn try_from(value: u8) -> Result<Self> {
            if value > netdev::MAX_DESCRIPTOR_CHAIN {
                return Err(Error::LargeChain(value.into()));
            }
            Ok(ChainLength(value))
        }
    }

    impl TryFrom<usize> for ChainLength {
        type Error = Error;

        fn try_from(value: usize) -> Result<Self> {
            let value = u8::try_from(value).map_err(|_err| Error::LargeChain(value))?;
            value.try_into()
        }
    }

    impl From<ChainLength> for usize {
        fn from(ChainLength(len): ChainLength) -> Self {
            len.into()
        }
    }

    impl ChainLength {
        pub(super) const ZERO: Self = Self(0);

        pub(super) fn get(&self) -> u8 {
            let ChainLength(len) = self;
            *len
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    // Safety: These are safe because none of the values are zero.
    const TX_BUFFERS: NonZeroU16 = unsafe { NonZeroU16::new_unchecked(1) };
    const RX_BUFFERS: NonZeroU16 = unsafe { NonZeroU16::new_unchecked(2) };
    const BUFFER_STRIDE: NonZeroU64 = unsafe { NonZeroU64::new_unchecked(4) };

    #[test]
    fn test_get_descriptor_after_vmo_write() {
        let (descriptors, vmo, tx, rx) =
            Descriptors::new(TX_BUFFERS, RX_BUFFERS, BUFFER_STRIDE).expect("create descriptors");
        vmo.write(&[netdev::FrameType::Ethernet.into_primitive()][..], 0).expect("vmo write");
        assert_eq!(tx.len(), TX_BUFFERS.get().into());
        assert_eq!(rx.len(), RX_BUFFERS.get().into());
        assert_eq!(
            descriptors.borrow(&tx[0]).frame_type().expect("failed to get frame type"),
            netdev::FrameType::Ethernet
        );
    }

    #[test]
    fn test_init_descriptor() {
        const HEAD_LEN: u16 = 1;
        const DATA_LEN: u32 = 2;
        const TAIL_LEN: u16 = 3;
        let (descriptors, _vmo, mut tx, _rx) =
            Descriptors::new(TX_BUFFERS, RX_BUFFERS, BUFFER_STRIDE).expect("create descriptors");
        {
            let mut descriptor = descriptors.borrow_mut(&mut tx[0]);
            descriptor.initialize(ChainLength::ZERO, HEAD_LEN, DATA_LEN, TAIL_LEN);
        }

        let got = descriptors.borrow(&mut tx[0]);
        assert_eq!(got.chain_length().unwrap(), ChainLength::ZERO);
        assert_eq!(got.offset(), 0);
        assert_eq!(got.head_length(), HEAD_LEN);
        assert_eq!(got.data_length(), DATA_LEN);
        assert_eq!(got.tail_length(), TAIL_LEN);
    }

    #[test]
    fn test_chain_length() {
        for raw in 0..=netdev::MAX_DESCRIPTOR_CHAIN {
            let got = ChainLength::try_from(raw)
                .expect("the conversion should succeed with length <= MAX_DESCRIPTOR_CHAIN");
            assert_eq!(got.get(), raw);
        }

        for raw in netdev::MAX_DESCRIPTOR_CHAIN + 1..u8::MAX {
            assert_matches!(ChainLength::try_from(raw).expect_err("the conversion should fail with length > MAX_DESCRIPTOR_CHAIN"), Error::LargeChain(len) if len == raw.into());
        }
    }
}
