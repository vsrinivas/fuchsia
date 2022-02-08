// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Trait used to indicate that the implementing type can be efficiently
/// converted into a reference to the original OpenThread type identified by
/// `Self::OtType`.
///
/// Types which implement this trait may be opaque, but, unlike [`Boxable`], these
/// types are not necessarily opaque and not necessarily ownable. If a type is
/// not opaque then it will also implement the related trait [`Transparent`],
/// allowing it to be used by value.
///
/// ## SAFETY ##
///
/// This trait is unsafe because it is making an assertion about the
/// data's representation that must be verified by code review.
///
/// In order to safely implement this trait you must verify that the implementing
/// type is guaranteed to always be fundamentally identical in its in-memory
/// representation to `<Self as OtCastable>::OtType`.
///
/// For example, this would be the case if `Self` is `#[repr(transparent)]` and
/// has only a single member of `<Self as OtCastable>::OtType`.
pub unsafe trait OtCastable: Sized {
    /// Original OpenThread Type.
    type OtType: Sized;

    /// Returns a reference to the original OpenThread type [`Self::OtType`].
    fn as_ot_ref(&self) -> &Self::OtType {
        unsafe { &*self.as_ot_ptr() }
    }

    /// Returns a mutable reference to the original OpenThread type [`Self::OtType`].
    fn as_ot_mut(&mut self) -> &mut Self::OtType {
        unsafe { &mut *self.as_ot_mut_ptr() }
    }

    /// Returns a pointer to the underlying [`Self::OtType`] instance.
    fn as_ot_ptr(&self) -> *const Self::OtType;

    /// Returns a mutable pointer to the underlying [`Self::OtType`] instance.
    fn as_ot_mut_ptr(&mut self) -> *mut Self::OtType;

    /// Creates a reference from a pointer to an [`Self::OtType`].
    ///
    /// ## Safety ##
    ///
    /// This method is unsafe because unchecked conversion of pointers
    /// to references is generally unsafe. The following assumptions
    /// need to be verified to avoid undefined behavior:
    ///
    /// 1. `ptr` MUST NOT be `NULL`.
    /// 2. `ptr` MUST point to a valid instance of [`Self::OtType`].
    unsafe fn ref_from_ot_ptr<'a>(ptr: *const Self::OtType) -> Option<&'a Self>;

    /// Creates a mut reference from a mut pointer to an [`Self::OtType`].
    ///
    /// ## Safety ##
    ///
    /// This method is unsafe because unchecked conversion of pointers
    /// to references is generally unsafe. The following assumptions
    /// need to be verified to avoid undefined behavior:
    ///
    /// 1. `ptr` MUST NOT be `NULL`.
    /// 2. `ptr` MUST point to a valid instance of [`Self::OtType`].
    unsafe fn mut_from_ot_mut_ptr<'a>(ptr: *mut Self::OtType) -> Option<&'a mut Self>;

    /// Casts a reference to the original OpenThread type to a reference to `Self`.
    fn ref_from_ot_ref(x: &Self::OtType) -> &Self {
        unsafe { Self::ref_from_ot_ptr(x as *const Self::OtType) }.unwrap()
    }
}

/// Trait used to indicate that the implementing type can be used by value
/// and converted to/from the associated OpenThread type by value.
///
/// Unlike types that implement the trait [`Boxable`], types implementing
/// this trait may be passed and used by value.
pub trait Transparent: OtCastable + Clone {
    /// Creates a new instance from an instance of [`Self::OtType`].
    fn from_ot(x: Self::OtType) -> Self;

    /// Converts this type into an instance of [`Self::OtType`].
    fn into_ot(self) -> Self::OtType;
}

#[doc(hidden)]
#[macro_export]
macro_rules! impl_ot_castable {
    (opaque from_only $wrapper:ty,$inner:ty) => {
        impl<'a> From<&'a $inner> for &'a $wrapper {
            fn from(x: &'a $inner) -> Self {
                <$wrapper>::ref_from_ot_ref(x)
            }
        }

        impl<'a> From<&'a $wrapper> for &'a $inner {
            fn from(x: &'a $wrapper) -> Self {
                x.as_ot_ref()
            }
        }
    };

    (from_only $wrapper:ty,$inner:ty) => {
        impl_ot_castable!(opaque from_only $wrapper, $inner);

        impl From<$inner> for $wrapper {
            fn from(x: $inner) -> Self {
                Self(x)
            }
        }

        impl From<$wrapper> for $inner {
            fn from(x: $wrapper) -> Self {
                x.0
            }
        }
    };

    ($wrapper:ty,$inner:ty) => {
        impl_ot_castable!(lifetime $wrapper, $inner);

        impl_ot_castable!(from_only $wrapper, $inner);
    };

    (opaque $wrapper:ty,$inner:ty) => {
        impl_ot_castable!(opaque lifetime $wrapper, $inner);

        impl_ot_castable!(opaque from_only $wrapper, $inner);
    };

    (lifetime $wrapper:ty,$inner:ty) => {
        impl_ot_castable!(lifetime $wrapper, $inner,);
    };

    (opaque lifetime $wrapper:ty,$inner:ty) => {
        impl_ot_castable!(opaque lifetime $wrapper, $inner,);
    };

    (opaque lifetime $wrapper:ty,$inner:ty, $($other:expr),*) => {
        // Note that we don't implement the PointerSafe trait on
        // types with lifetimes.
        unsafe impl $crate::ot::OtCastable for $wrapper {
            type OtType = $inner;

            fn as_ot_ptr(&self) -> *const Self::OtType {
                &self.0 as *const Self::OtType
            }

            fn as_ot_mut_ptr(&mut self) -> *mut Self::OtType {
                &mut self.0 as *mut Self::OtType
            }

            unsafe fn ref_from_ot_ptr<'a>(ptr: *const Self::OtType) -> Option<&'a Self> {
                static_assertions::assert_eq_size!($wrapper, $inner);
                static_assertions::assert_eq_align!($wrapper, $inner);
                if ptr.is_null() {
                    None
                } else {
                    Some(&*(ptr as *const Self))
                }
            }

            unsafe fn mut_from_ot_mut_ptr<'a>(ptr: *mut Self::OtType) -> Option<&'a mut Self> {
                static_assertions::assert_eq_size!($wrapper, $inner);
                static_assertions::assert_eq_align!($wrapper, $inner);
                if ptr.is_null() {
                    None
                } else {
                    Some(&mut *(ptr as *mut Self))
                }
            }
        }
    };

    (lifetime $wrapper:ty,$inner:ty, $($other:expr),*) => {
        impl_ot_castable!(opaque lifetime $wrapper, $inner, $($other),*);
        // Note that we don't implement the PointerSafe trait on
        // types with lifetimes.
        impl $crate::ot::Transparent for $wrapper {
            fn from_ot(x: Self::OtType) -> Self {
                Self(x,$($other),*)
            }

            fn into_ot(self) -> Self::OtType {
                self.0
            }
        }
    };
}
