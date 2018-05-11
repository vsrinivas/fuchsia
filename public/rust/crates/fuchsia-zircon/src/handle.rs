use {Port, Rights, Signals, Status, Time, WaitAsyncOpts, ok, sys};
use std::marker::PhantomData;
use std::mem::{self, ManuallyDrop};

/// An object representing a Zircon
/// [handle](https://fuchsia.googlesource.com/zircon/+/master/docs/handles.md).
///
/// Internally, it is represented as a 32-bit integer, but this wrapper enforces
/// strict ownership semantics. The `Drop` implementation closes the handle.
///
/// This type represents the most general reference to a kernel object, and can
/// be interconverted to and from more specific types. Those conversions are not
/// enforced in the type system; attempting to use them will result in errors
/// returned by the kernel. These conversions don't change the underlying
/// representation, but do change the type and thus what operations are available.
#[derive(Debug, Eq, PartialEq, Hash)]
#[repr(transparent)]
pub struct Handle(sys::zx_handle_t);

impl AsHandleRef for Handle {
    fn as_handle_ref(&self) -> HandleRef {
        Unowned {
            inner: ManuallyDrop::new(Handle(self.0)),
            marker: PhantomData,
        }
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

/// A borrowed value of type `T`.
///
/// This is primarily used for working with borrowed values of `HandleBased` types.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Unowned<'a, T: 'a> {
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
        Unowned {
            inner: ManuallyDrop::new(T::from(Handle::from_raw(handle))),
            marker: PhantomData,
        }
    }

    pub fn raw_handle(&self) -> sys::zx_handle_t {
        self.inner.raw_handle()
    }

    pub fn duplicate(&self, rights: Rights) -> Result<Handle, Status> {
        let mut out = 0;
        let status = unsafe {
            sys::zx_handle_duplicate(
                self.raw_handle(), rights.bits(), &mut out)
        };
        ok(status).map(|()| Handle(out))
    }

    pub fn signal(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_object_signal(
                self.raw_handle(), clear_mask.bits(), set_mask.bits())
        };
        ok(status)
    }

    pub fn wait(&self, signals: Signals, deadline: Time) -> Result<Signals, Status> {
        let mut pending = Signals::empty().bits();
        let status = unsafe {
            sys::zx_object_wait_one(
                self.raw_handle(), signals.bits(), deadline.nanos(), &mut pending)
        };
        ok(status).map(|()| Signals::from_bits_truncate(pending))
    }

    pub fn wait_async(&self, port: &Port, key: u64, signals: Signals, options: WaitAsyncOpts)
        -> Result<(), Status>
    {
        let status = unsafe {
            sys::zx_object_wait_async(
                self.raw_handle(), port.raw_handle(), key, signals.bits(), options as u32)
        };
        ok(status)
    }
}

/// A trait to get a reference to the underlying handle of an object.
pub trait AsHandleRef {
    /// Get a reference to the handle. One important use of such a reference is
    /// for `object_wait_many`.
    fn as_handle_ref(&self) -> HandleRef;

    /// Interpret the reference as a raw handle (an integer type). Two distinct
    /// handles will have different raw values (so it can perhaps be used as a
    /// key in a data structure).
    fn raw_handle(&self) -> sys::zx_handle_t {
        self.as_handle_ref().inner.0
    }

    /// Set and clear userspace-accessible signal bits on an object. Wraps the
    /// [zx_object_signal](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/object_signal.md)
    /// syscall.
    fn signal_handle(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        self.as_handle_ref().signal(clear_mask, set_mask)
    }

    /// Waits on a handle. Wraps the
    /// [zx_object_wait_one](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/object_wait_one.md)
    /// syscall.
    fn wait_handle(&self, signals: Signals, deadline: Time) -> Result<Signals, Status> {
        self.as_handle_ref().wait(signals, deadline)
    }

    /// Causes packet delivery on the given port when the object changes state and matches signals.
    /// [zx_object_wait_async](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/object_wait_async.md)
    /// syscall.
    fn wait_async_handle(&self, port: &Port, key: u64, signals: Signals, options: WaitAsyncOpts)
        -> Result<(), Status>
    {
        self.as_handle_ref().wait_async(port, key, signals, options)
    }
}

impl<'a, T: HandleBased> AsHandleRef for Unowned<'a, T> {
    fn as_handle_ref(&self) -> HandleRef {
        Unowned {
            inner: ManuallyDrop::new(Handle(self.raw_handle())),
            marker: PhantomData,
        }
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
    /// [zx_handle_duplicate](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/handle_duplicate.md)
    /// syscall.
    fn duplicate_handle(&self, rights: Rights) -> Result<Self, Status> {
        self.as_handle_ref().duplicate(rights).map(|handle| Self::from(handle))
    }

    /// Create a replacement for a handle, possibly reducing the rights available. This invalidates
    /// the original handle. Wraps the
    /// [zx_handle_replace](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/handle_replace.md)
    /// syscall.
    fn replace_handle(self, rights: Rights) -> Result<Self, Status> {
        <Self as Into<Handle>>::into(self)
            .replace(rights).map(|handle| Self::from(handle))
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
}

/// A trait implemented by all handles for objects which have a peer.
pub trait Peered: HandleBased {
    /// Set and clear userspace-accessible signal bits on the object's peer. Wraps the
    /// [zx_object_signal_peer](https://fuchsia.googlesource.com/zircon/+/master/docs/syscalls/object_signal.md)
    /// syscall.
    fn signal_peer(&self, clear_mask: Signals, set_mask: Signals) -> Result<(), Status> {
        let handle = self.raw_handle();
        let status = unsafe {
            sys::zx_object_signal_peer(handle, clear_mask.bits(), set_mask.bits())
        };
        ok(status)
    }
}

/// A trait implemented by all handles for objects which can have a cookie attached.
pub trait Cookied: HandleBased {
    /// Get the cookie attached to this object, if any. Wraps the
    /// [zx_object_get_cookie](https://fuchsia.googlesource.com/zircon/+/HEAD/docs/syscalls/object_get_cookie.md)
    /// syscall.
    fn get_cookie(&self, scope: &HandleRef) -> Result<u64, Status> {
        let mut cookie = 0;
        let status = unsafe {
            sys::zx_object_get_cookie(
                self.raw_handle(), scope.raw_handle(), &mut cookie)
        };
        ok(status).map(|()| cookie)
    }

    /// Attach an opaque cookie to this object with the given scope. The cookie may be read or
    /// changed in future only with the same scope. Wraps the
    /// [zx_object_set_cookie](https://fuchsia.googlesource.com/zircon/+/HEAD/docs/syscalls/object_set_cookie.md)
    /// syscall.
    fn set_cookie(&self, scope: &HandleRef, cookie: u64) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_object_set_cookie(
                self.raw_handle(), scope.raw_handle(), cookie)
        };
        ok(status)
    }
}
