// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;
use std::ops::{Deref, DerefMut};

/// A box for owning pointers to opaque types vended from the OpenThread API.
///
/// May be abbreviated as an [`OtBox`](crate::OtBox) to avoid confusion with [`std::boxed::Box`].
///
/// Internally, an [`ot::Box`](crate::ot::Box) contains the raw pointer to the underlying
/// OpenThread object. Externally, the box appears to contain an instance
/// of a type implementing [`ot::Boxable`](crate::ot::Boxable). References to the
/// [`ot::Boxable`](crate::ot::Boxable) type are created from the underlying pointer as needed.
///
/// When an [`ot::Box`](crate::ot::Box) goes out of scope, the underlying object is finalized
/// according to the OpenThread API for that type, via
/// [`ot::Boxable::finalize`](crate::ot::Boxable::finalize).
///
/// The underlying pointer may be taken from the box without finalization
/// by calling [`ot::Box::take_ot_ptr`](crate::ot::Box::take_ot_ptr), which consumes the
/// [`ot::Box`](crate::ot::Box) and returns the pointer.
///
/// ## Safety ##
///
/// In general, the safety of this entire approach is dependent on the
/// following assumptions on the language itself:
///
/// 1. **Casting from pointers to references**: Transmuting a `*mut Self::OtType` to a `&Self` is
///    not itself undefined behavior assuming the pointer pointed to a valid object.
/// 2. **Casting from references to pointers**: Transmuting a `&Self` that was previously created
///    by assumption 1 back into a `*mut Self::OtType` will *always* yield the original pointer
///    value.
/// 3. **Behavior of Static Dispatch**: Traits implemented on `Self` and called via static
///    dispatch will have `&self` references that obey assumption #2.
/// 4. **No Spooky Stuff**: No weird pointer/reference manipulation happens behind the scenes,
///    like the spooky stuff C++ does.
#[repr(transparent)]
pub struct Box<T: Boxable>(*mut T::OtType, PhantomData<T>);

// SAFETY: Boxed values are owned values, so box itself can be considered `Send`/`Sync`
//         as long as the contained type is considered `Send`/`Sync`.
unsafe impl<T: Boxable + Send> Send for Box<T> {}
unsafe impl<T: Boxable + Sync> Sync for Box<T> {}

impl<T: Boxable + crate::ot::InstanceInterface> crate::ot::InstanceInterface for Box<T> {}

impl<T: Boxable + std::fmt::Debug> std::fmt::Debug for Box<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("OtBox").field(self.as_ref()).finish()
    }
}

impl<T: Boxable> Box<T> {
    /// Takes ownership of an OpenThread object by wrapping it in an `OtBox` instance.
    /// Unless subsequently removed by a call to [`take_ot_ptr()`], the pointed-to
    /// object will be finalized when the box goes out of scope.
    ///
    /// ## Safety ##
    ///
    /// This method is unsafe because it is effectively a deferred call
    /// to [`Boxable::ref_from_ot_ptr`], which is also unsafe. When calling,
    /// care must be taken to ensure the following is true:
    ///
    /// 1. The given pointer points to a valid instance of `T::OtType`.
    /// 2. The caller has logical ownership of the object being pointed to.
    pub unsafe fn from_ot_ptr(ptr: *mut T::OtType) -> Option<Self> {
        if ptr.is_null() {
            None
        } else {
            Some(Box(ptr, Default::default()))
        }
    }

    /// Releases ownership of the contained OpenThread object, returning it's pointer.
    pub fn take_ot_ptr(self) -> *mut T::OtType {
        let ret = self.0;
        std::mem::forget(self);
        ret
    }
}

impl<T: Boxable> AsRef<T> for Box<T> {
    fn as_ref(&self) -> &T {
        // SAFETY: `ref_from_ot_ptr` has two safety requirements on the pointer.
        //         The pointer ultimatly comes from `ot::Box::from_ot_ptr`, which
        //         has a superset of those requirements. Thus, the requirements
        //         for this call are met.
        unsafe { T::ref_from_ot_ptr(self.0).unwrap() }
    }
}

impl<T: Boxable> AsMut<T> for Box<T> {
    fn as_mut(&mut self) -> &mut T {
        // SAFETY: `mut_from_ot_ptr` has two safety requirements on the pointer.
        //         The pointer ultimatly comes from `ot::Box::from_ot_ptr`, which
        //         has a superset of those requirements. Thus, the requirements
        //         for this call are met.
        unsafe { T::mut_from_ot_ptr(self.0).unwrap() }
    }
}

impl<T: Boxable> Drop for Box<T> {
    fn drop(&mut self) {
        // SAFETY: The single safety requirement on `Boxable::finalize` is that it only
        //         be called from `Drop::drop`, which is this method.
        unsafe { self.as_mut().finalize() }
    }
}

impl<T: Boxable> Deref for Box<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        self.as_ref()
    }
}

impl<T: Boxable> DerefMut for Box<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.as_mut()
    }
}

/// Trait for OpenThread Rust types for which an owned opaque instance is kept in a [`ot::Box<>`].
///
/// ## Safety ##
///
/// The safety of implementing this trait is dependent on the following assumptions:
///
/// 1. The type `Self` is never instantiated or passed by value. It must only ever
///    used by reference (`&Self` or `&mut Self`).
/// 2. No member of `Self` is ever accessed directly or exposed publicly.
///
/// Because of the above restrictions, `Self` is usually a zero-sized type.
pub unsafe trait Boxable: Sized {
    /// The underlying implementation-opaque OpenThread type used by the standard C-API.
    type OtType: Sized;

    /// Finalizes or frees the underlying OpenThread object.
    ///
    /// This method should only be called by the `Drop::drop` method on [`ot::Box`](crate::ot::Box).
    /// It should not be called directly. This is usually the only method that
    /// needs to be implemented from this trait: the default implementations of
    /// the other methods is sufficient.
    ///
    /// ## Safety ##
    ///
    /// This method is considered unsafe to call directly because it invalidates
    /// the underlying OpenThread object. This method should only ever be called
    /// from a single place: `Drop::drop()` on `ot::Box`.
    unsafe fn finalize(&mut self);

    /// Creates a reference to a safe wrapper object from an OpenThread pointer.
    ///
    /// ## Safety ##
    ///
    /// This method is unsafe because it allows you to cast an arbitrary
    /// pointer to a reference. When calling, care must be taken to ensure
    /// the following is true:
    ///
    /// 1. The given pointer points to a valid instance of `Self::OtType`.
    /// 2. The given pointer is not `NULL`.
    unsafe fn ref_from_ot_ptr<'a>(ptr: *mut Self::OtType) -> Option<&'a Self> {
        if ptr.is_null() {
            None
        } else {
            Some(&*(ptr as *const Self))
        }
    }

    /// Creates a mutable reference to a safe wrapper object from an OpenThread pointer.
    ///
    /// ## Safety ##
    ///
    /// This method is unsafe because it allows you to cast an arbitrary
    /// pointer to a reference. When calling, care must be taken to ensure
    /// the following is true:
    ///
    /// 1. The given pointer points to a valid instance of `Self::OtType`.
    /// 2. The given pointer is not `NULL`.
    unsafe fn mut_from_ot_ptr<'a>(ptr: *mut Self::OtType) -> Option<&'a mut Self> {
        if ptr.is_null() {
            None
        } else {
            Some(&mut *(ptr as *mut Self))
        }
    }

    /// Returns the underlying OpenThread pointer for this object.
    /// The default implementation simply casts the reference to a pointer.
    fn as_ot_ptr(&self) -> *mut Self::OtType {
        (self as *const Self) as *mut Self::OtType
    }
}
