// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon handles.
//!
use crate::{
    object_get_info, object_get_property, object_set_property, ok, ObjectQuery, Port, Property,
    PropertyQuery, PropertyQueryGet, PropertyQuerySet, Rights, Signals, Status, Time, Topic,
    WaitAsyncOpts,
};

use fuchsia_zircon_sys as sys;
use std::ffi::{CStr, CString};
use std::marker::PhantomData;
use std::mem::{self, ManuallyDrop};

#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Koid(sys::zx_koid_t);

impl Koid {
    pub fn raw_koid(&self) -> sys::zx_koid_t {
        self.0
    }
}

/// An object representing a Zircon
/// [handle](https://fuchsia.dev/fuchsia-src/concepts/objects/handles).
///
/// Internally, it is represented as a 32-bit integer, but this wrapper enforces
/// strict ownership semantics. The `Drop` implementation closes the handle.
///
/// This type represents the most general reference to a kernel object, and can
/// be interconverted to and from more specific types. Those conversions are not
/// enforced in the type system; attempting to use them will result in errors
/// returned by the kernel. These conversions don't change the underlying
/// representation, but do change the type and thus what operations are available.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Handle(sys::zx_handle_t);

impl AsHandleRef for Handle {
    fn as_handle_ref(&self) -> HandleRef<'_> {
        Unowned { inner: ManuallyDrop::new(Handle(self.0)), marker: PhantomData }
    }
}

impl HandleBased for Handle {}

impl Drop for Handle {
    fn drop(&mut self) {
        if self.0 != sys::ZX_HANDLE_INVALID {
            unsafe { sys::zx_handle_close(self.0) };
        }
    }
}

impl Handle {
    /// Initialize a handle backed by ZX_HANDLE_INVALID, the only safe non-handle.
    pub fn invalid() -> Handle {
        Handle(sys::ZX_HANDLE_INVALID)
    }

    /// If a raw handle is obtained from some other source, this method converts
    /// it into a type-safe owned handle.
    pub unsafe fn from_raw(raw: sys::zx_handle_t) -> Handle {
        Handle(raw)
    }

    pub fn is_invalid(&self) -> bool {
        self.0 == sys::ZX_HANDLE_INVALID
    }

    pub fn replace(self, rights: Rights) -> Result<Handle, Status> {
        let handle = self.0;
        let mut out = 0;
        let status = unsafe { sys::zx_handle_replace(handle, rights.bits(), &mut out) };
        ok(status).map(|()| Handle(out))
    }
}

struct NameProperty();
unsafe impl PropertyQuery for NameProperty {
    const PROPERTY: Property = Property::NAME;
    type PropTy = [u8; sys::ZX_MAX_NAME_LEN];
}
unsafe impl PropertyQueryGet for NameProperty {}
unsafe impl PropertyQuerySet for NameProperty {}

/// A borrowed value of type `T`.
///
/// This is primarily used for working with borrowed values of `HandleBased` types.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Unowned<'a, T> {
    inner: ManuallyDrop<T>,
    marker: PhantomData<&'a T>,
}

impl<'a, T> ::std::ops::Deref for Unowned<'a, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &*self.inner
    }
}

pub type HandleRef<'a> = Unowned<'a, Handle>;

impl<'a, T: HandleBased> Unowned<'a, T> {
    /// Create a `HandleRef` from a raw handle. Use this method when you are given a raw handle but
    /// should not take ownership of it. Examples include process-global handles like the root
    /// VMAR. This method should be called with an explicitly provided lifetime that must not
    /// outlive the lifetime during which the handle is owned by the current process. It is unsafe
    /// because most of the time, it is better to use a `Handle` to prevent leaking resources.
    pub unsafe fn from_raw_handle(handle: sys::zx_handle_t) -> Self {
        Unowned { inner: ManuallyDrop::new(T::from(Handle::from_raw(handle))), marker: PhantomData }
    }

    pub fn raw_handle(&self) -> sys::zx_handle_t {
        self.inner.raw_handle()
    }

    pub fn duplicate(&self, rights: Rights) -> Result<T, Status> {
        let mut out = 0;
        let status =
            unsafe { sys::zx_handle_duplicate(self.raw_handle(), rights.bits(), &mut out) };
        ok(status).map(|()| T::from(Handle(out)))
    }

    pub fn signal(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        let status =
            unsafe { sys::zx_object_signal(self.raw_handle(), clear_mask.bits(), set_mask.bits()) };
        ok(status)
    }

    pub fn wait(&self, signals: Signals, deadline: Time) -> Result<Signals, Status> {
        let mut pending = Signals::empty().bits();
        let status = unsafe {
            sys::zx_object_wait_one(
                self.raw_handle(),
                signals.bits(),
                deadline.into_nanos(),
                &mut pending,
            )
        };
        ok(status).map(|()| Signals::from_bits_truncate(pending))
    }

    pub fn wait_async(
        &self,
        port: &Port,
        key: u64,
        signals: Signals,
        options: WaitAsyncOpts,
    ) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_object_wait_async(
                self.raw_handle(),
                port.raw_handle(),
                key,
                signals.bits(),
                options.bits(),
            )
        };
        ok(status)
    }
}

/// A trait to get a reference to the underlying handle of an object.
pub trait AsHandleRef {
    /// Get a reference to the handle. One important use of such a reference is
    /// for `object_wait_many`.
    fn as_handle_ref(&self) -> HandleRef<'_>;

    /// Interpret the reference as a raw handle (an integer type). Two distinct
    /// handles will have different raw values (so it can perhaps be used as a
    /// key in a data structure).
    fn raw_handle(&self) -> sys::zx_handle_t {
        self.as_handle_ref().inner.0
    }

    /// Set and clear userspace-accessible signal bits on an object. Wraps the
    /// [zx_object_signal](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_signal.md)
    /// syscall.
    fn signal_handle(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        self.as_handle_ref().signal(clear_mask, set_mask)
    }

    /// Waits on a handle. Wraps the
    /// [zx_object_wait_one](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_wait_one.md)
    /// syscall.
    fn wait_handle(&self, signals: Signals, deadline: Time) -> Result<Signals, Status> {
        self.as_handle_ref().wait(signals, deadline)
    }

    /// Causes packet delivery on the given port when the object changes state and matches signals.
    /// [zx_object_wait_async](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_wait_async.md)
    /// syscall.
    fn wait_async_handle(
        &self,
        port: &Port,
        key: u64,
        signals: Signals,
        options: WaitAsyncOpts,
    ) -> Result<(), Status> {
        self.as_handle_ref().wait_async(port, key, signals, options)
    }

    /// Get the [Property::NAME] property for this object.
    ///
    /// Wraps a call to the
    /// [zx_object_get_property](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_property.md)
    /// syscall for the ZX_PROP_NAME property.
    fn get_name(&self) -> Result<CString, Status> {
        let buf = object_get_property::<NameProperty>(self.as_handle_ref())?;
        let nul_pos = buf.iter().position(|&x| x == b'\0').ok_or(Status::INTERNAL)?;
        // We already checked for nul bytes, so simply unwrap.
        Ok(CString::new(&buf[..nul_pos]).unwrap())
    }

    /// Set the [Property::NAME] property for this object.
    ///
    /// The name's length must be less than [sys::ZX_MAX_NAME_LEN], i.e.
    /// name.[to_bytes_with_nul()](CStr::to_bytes_with_nul()).len() <= [sys::ZX_MAX_NAME_LEN], or
    /// Err([Status::INVALID_ARGS]) will be returned.
    ///
    /// Wraps a call to the
    /// [zx_object_get_property](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_property.md)
    /// syscall for the ZX_PROP_NAME property.
    fn set_name(&self, name: &CStr) -> Result<(), Status> {
        let bytes = name.to_bytes_with_nul();
        if bytes.len() > sys::ZX_MAX_NAME_LEN {
            return Err(Status::INVALID_ARGS);
        }

        let mut buf = [0u8; sys::ZX_MAX_NAME_LEN];
        buf[..bytes.len()].copy_from_slice(bytes);
        object_set_property::<NameProperty>(self.as_handle_ref(), &buf)
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_HANDLE_BASIC topic.
    fn basic_info(&self) -> Result<HandleBasicInfo, Status> {
        let mut info = sys::zx_info_handle_basic_t::default();
        object_get_info::<HandleBasicInfoQuery>(
            self.as_handle_ref(),
            std::slice::from_mut(&mut info),
        )
        .map(|_| HandleBasicInfo::from(info))
    }

    /// Returns the koid (kernel object ID) for this handle.
    fn get_koid(&self) -> Result<Koid, Status> {
        self.basic_info().map(|info| info.koid)
    }
}

impl<'a, T: HandleBased> AsHandleRef for Unowned<'a, T> {
    fn as_handle_ref(&self) -> HandleRef<'_> {
        Unowned { inner: ManuallyDrop::new(Handle(self.raw_handle())), marker: PhantomData }
    }
}

/// A trait implemented by all handle-based types.
///
/// Note: it is reasonable for user-defined objects wrapping a handle to implement
/// this trait. For example, a specific interface in some protocol might be
/// represented as a newtype of `Channel`, and implement the `as_handle_ref`
/// method and the `From<Handle>` trait to facilitate conversion from and to the
/// interface.
pub trait HandleBased: AsHandleRef + From<Handle> + Into<Handle> {
    /// Duplicate a handle, possibly reducing the rights available. Wraps the
    /// [zx_handle_duplicate](https://fuchsia.dev/fuchsia-src/reference/syscalls/handle_duplicate.md)
    /// syscall.
    fn duplicate_handle(&self, rights: Rights) -> Result<Self, Status> {
        self.as_handle_ref().duplicate(rights).map(|handle| Self::from(handle))
    }

    /// Create a replacement for a handle, possibly reducing the rights available. This invalidates
    /// the original handle. Wraps the
    /// [zx_handle_replace](https://fuchsia.dev/fuchsia-src/reference/syscalls/handle_replace.md)
    /// syscall.
    fn replace_handle(self, rights: Rights) -> Result<Self, Status> {
        <Self as Into<Handle>>::into(self).replace(rights).map(|handle| Self::from(handle))
    }

    /// Converts the value into its inner handle.
    ///
    /// This is a convenience function which simply forwards to the `Into` trait.
    fn into_handle(self) -> Handle {
        self.into()
    }

    /// Converts the handle into it's raw representation.
    ///
    /// The caller takes ownership over the raw handle, and must close or transfer it to avoid a handle leak.
    fn into_raw(self) -> sys::zx_handle_t {
        let h = self.into_handle();
        let r = h.0;
        mem::forget(h);
        r
    }

    /// Creates an instance of this type from a handle.
    ///
    /// This is a convenience function which simply forwards to the `From` trait.
    fn from_handle(handle: Handle) -> Self {
        Self::from(handle)
    }

    /// Creates an instance of another handle-based type from this value's inner handle.
    fn into_handle_based<H: HandleBased>(self) -> H {
        H::from_handle(self.into_handle())
    }

    /// Creates an instance of this type from the inner handle of another
    /// handle-based type.
    fn from_handle_based<H: HandleBased>(h: H) -> Self {
        Self::from_handle(h.into_handle())
    }

    fn is_invalid_handle(&self) -> bool {
        self.as_handle_ref().is_invalid()
    }
}

/// A trait implemented by all handles for objects which have a peer.
pub trait Peered: HandleBased {
    /// Set and clear userspace-accessible signal bits on the object's peer. Wraps the
    /// [zx_object_signal_peer](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_signal.md)
    /// syscall.
    fn signal_peer(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        let handle = self.raw_handle();
        let status =
            unsafe { sys::zx_object_signal_peer(handle, clear_mask.bits(), set_mask.bits()) };
        ok(status)
    }
}

/// Zircon object types.
#[derive(Copy, Clone, Ord, PartialOrd, Eq, PartialEq, Hash)]
#[repr(transparent)]
pub struct ObjectType(sys::zx_obj_type_t);

assoc_values!(ObjectType, [
    NONE            = sys::ZX_OBJ_TYPE_NONE;
    PROCESS         = sys::ZX_OBJ_TYPE_PROCESS;
    THREAD          = sys::ZX_OBJ_TYPE_THREAD;
    VMO             = sys::ZX_OBJ_TYPE_VMO;
    CHANNEL         = sys::ZX_OBJ_TYPE_CHANNEL;
    EVENT           = sys::ZX_OBJ_TYPE_EVENT;
    PORT            = sys::ZX_OBJ_TYPE_PORT;
    INTERRUPT       = sys::ZX_OBJ_TYPE_INTERRUPT;
    PCI_DEVICE      = sys::ZX_OBJ_TYPE_PCI_DEVICE;
    LOG             = sys::ZX_OBJ_TYPE_LOG;
    SOCKET          = sys::ZX_OBJ_TYPE_SOCKET;
    STREAM          = sys::ZX_OBJ_TYPE_STREAM;
    RESOURCE        = sys::ZX_OBJ_TYPE_RESOURCE;
    EVENTPAIR       = sys::ZX_OBJ_TYPE_EVENTPAIR;
    JOB             = sys::ZX_OBJ_TYPE_JOB;
    VMAR            = sys::ZX_OBJ_TYPE_VMAR;
    FIFO            = sys::ZX_OBJ_TYPE_FIFO;
    GUEST           = sys::ZX_OBJ_TYPE_GUEST;
    VCPU            = sys::ZX_OBJ_TYPE_VCPU;
    TIMER           = sys::ZX_OBJ_TYPE_TIMER;
    IOMMU           = sys::ZX_OBJ_TYPE_IOMMU;
    BTI             = sys::ZX_OBJ_TYPE_BTI;
    PROFILE         = sys::ZX_OBJ_TYPE_PROFILE;
    PMT             = sys::ZX_OBJ_TYPE_PMT;
    SUSPEND_TOKEN   = sys::ZX_OBJ_TYPE_SUSPEND_TOKEN;
    PAGER           = sys::ZX_OBJ_TYPE_PAGER;
    EXCEPTION       = sys::ZX_OBJ_TYPE_EXCEPTION;
]);

/// Basic information about a handle.
///
/// Wrapper for data returned from [Handle::basic_info()].
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct HandleBasicInfo {
    pub koid: Koid,
    pub rights: Rights,
    pub object_type: ObjectType,
    pub related_koid: Koid,
    pub reserved: u32,
}

impl Default for HandleBasicInfo {
    fn default() -> Self {
        Self::from(sys::zx_info_handle_basic_t::default())
    }
}

impl From<sys::zx_info_handle_basic_t> for HandleBasicInfo {
    fn from(info: sys::zx_info_handle_basic_t) -> Self {
        let sys::zx_info_handle_basic_t { koid, rights, type_, related_koid, reserved } = info;

        // Note lossy conversion of Rights and HandleProperty here if either of those types are out
        // of date or incomplete.
        HandleBasicInfo {
            koid: Koid(koid),
            rights: Rights::from_bits_truncate(rights),
            object_type: ObjectType(type_),
            related_koid: Koid(related_koid),
            reserved: reserved,
        }
    }
}

// zx_info_handle_basic_t is able to be safely replaced with a byte representation and is a PoD
// type.
struct HandleBasicInfoQuery;
unsafe impl ObjectQuery for HandleBasicInfoQuery {
    const TOPIC: Topic = Topic::HANDLE_BASIC;
    type InfoTy = sys::zx_info_handle_basic_t;
}

#[cfg(test)]
mod tests {
    use super::*;
    // The unit tests are built with a different crate name, but fuchsia_runtime returns a "real"
    // fuchsia_zircon::Vmar that we need to use.
    use fuchsia_zircon::{AsHandleRef, Channel, HandleBased, ObjectType, Rights, Status, Vmo};

    #[test]
    fn set_get_name() {
        // We need some concrete object to exercise the AsHandleRef<'_> set/get_name functions.
        let vmo = Vmo::create(1).unwrap();
        let short_name = CStr::from_bytes_with_nul(b"v\0").unwrap();
        assert!(vmo.set_name(short_name).is_ok());
        assert_eq!(vmo.get_name(), Ok(short_name.to_owned()));
    }

    #[test]
    fn set_get_max_len_name() {
        let vmo = Vmo::create(1).unwrap();
        let max_len_name = CStr::from_bytes_with_nul(b"a_great_maximum_length_vmo_name\0").unwrap(); // 32 bytes
        assert!(vmo.set_name(max_len_name).is_ok());
        assert_eq!(vmo.get_name(), Ok(max_len_name.to_owned()));
    }

    #[test]
    fn set_get_too_long_name() {
        let vmo = Vmo::create(1).unwrap();
        let too_long_name =
            CStr::from_bytes_with_nul(b"bad_really_too_too_long_vmo_name\0").unwrap(); // 33 bytes
        assert_eq!(vmo.set_name(too_long_name), Err(Status::INVALID_ARGS));
    }

    #[test]
    fn basic_info_channel() {
        let (side1, side2) = Channel::create().unwrap();
        let info1 = side1.basic_info().expect("side1 basic_info failed");
        let info2 = side2.basic_info().expect("side2 basic_info failed");

        assert_eq!(info1.koid, info2.related_koid);
        assert_eq!(info2.koid, info1.related_koid);

        for info in &[info1, info2] {
            assert!(info.koid.raw_koid() >= sys::ZX_KOID_FIRST);
            assert_eq!(info.object_type, ObjectType::CHANNEL);
            assert!(info.rights.contains(Rights::READ | Rights::WRITE | Rights::WAIT));
        }

        let side1_repl = side1.replace_handle(Rights::READ).expect("side1 replace_handle failed");
        let info1_repl = side1_repl.basic_info().expect("side1_repl basic_info failed");
        assert_eq!(info1_repl.koid, info1.koid);
        assert_eq!(info1_repl.rights, Rights::READ);
    }

    #[test]
    fn basic_info_vmar() {
        // VMARs aren't waitable.
        let root_vmar = fuchsia_runtime::vmar_root_self();
        let info = root_vmar.basic_info().expect("vmar basic_info failed");
        assert_eq!(info.object_type, ObjectType::VMAR);
        assert!(!info.rights.contains(Rights::WAIT));
    }
}
