#![cfg_attr(not(feature = "std"), no_std)]
#![cfg_attr(feature = "nightly", feature(core_intrinsics, try_from))]

#[cfg(not(feature = "std"))]
mod std {
    pub use core::*;
}

use std::any::Any as StdAny;
use std::any::TypeId;
#[cfg(feature = "nightly")]
use std::convert::TryFrom;
#[cfg(feature = "std")]
use std::error::Error;
#[cfg(feature = "nightly")]
use std::intrinsics;
use std::fmt::{self, Debug, Display};
use std::mem;

// ++++++++++++++++++++ Any ++++++++++++++++++++

#[cfg(feature = "nightly")]
fn type_name<T: StdAny + ?Sized>() -> &'static str { unsafe { intrinsics::type_name::<T>() } }
#[cfg(not(feature = "nightly"))]
fn type_name<T: StdAny + ?Sized>() -> &'static str { "[ONLY ON NIGHTLY]" }

pub trait Any: StdAny {
    /// TODO: once 1.33.0 is the minimum supported compiler version, remove
    /// Any::type_id_compat and use StdAny::type_id instead.
    /// https://github.com/rust-lang/rust/issues/27745
    fn type_id_compat(&self) -> TypeId { TypeId::of::<Self>() }
    #[doc(hidden)]
    fn type_name(&self) -> &'static str { type_name::<Self>() }
}

impl<T> Any for T where T: StdAny + ?Sized {}

// ++++++++++++++++++++ TypeMismatch ++++++++++++++++++++

#[derive(Debug, Clone, Copy)]
pub struct TypeMismatch {
    expected: &'static str,
    found: &'static str,
}

impl TypeMismatch {
    pub fn new<T, O>(found_obj: &O) -> Self
        where T: Any + ?Sized, O: Any + ?Sized
    {
        TypeMismatch {
            expected: type_name::<T>(),
            found: found_obj.type_name(),
        }
    }
}

impl Display for TypeMismatch {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        write!(fmt, "Type mismatch: Expected '{}', found '{}'!", self.expected, self.found)
    }
}

#[cfg(feature = "std")]
impl Error for TypeMismatch {
    fn description(&self) -> &str { "Type mismatch" }
}

// ++++++++++++++++++++ DowncastError ++++++++++++++++++++

pub struct DowncastError<O> {
    mismatch: TypeMismatch,
    object: O,
}

impl<O> DowncastError<O> {
    pub fn new(mismatch: TypeMismatch, object: O) -> Self {
        Self {
            mismatch: mismatch,
            object: object,
        }
    }
    pub fn type_mismatch(&self) -> TypeMismatch { self.mismatch }
    pub fn into_object(self) -> O { self.object }
}

impl<O> Debug for DowncastError<O> {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        fmt.debug_struct("DowncastError")
            .field("mismatch", &self.mismatch)
            .finish()
    }
}

impl<O> Display for DowncastError<O> {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.mismatch, fmt)
    }
}

#[cfg(feature = "std")]
impl<O> Error for DowncastError<O> {
    fn description(&self) -> &str { self.mismatch.description() }
}

// ++++++++++++++++++++ Downcast ++++++++++++++++++++

#[derive(Clone, Copy)]
struct TraitObject {
    pub data: *mut (),
    pub vtable: *mut (),
}

#[inline]
fn to_trait_object<T: ?Sized>(obj: &T) -> TraitObject {
    assert_eq!(mem::size_of::<&T>(), mem::size_of::<TraitObject>());
    unsafe { *((&obj) as *const &T as *const TraitObject) }
}

pub trait Downcast<T>: Any
    where T: Any
{
    fn is_type(&self) -> bool { self.type_id_compat() == TypeId::of::<T>() }

    unsafe fn downcast_ref_unchecked(&self) -> &T { &*(to_trait_object(self).data as *mut T) }

    fn downcast_ref(&self) -> Result<&T, TypeMismatch> {
        if self.is_type() {
            Ok(unsafe { self.downcast_ref_unchecked() })
        } else {
            Err(TypeMismatch::new::<T, Self>(self))
        }
    }

    unsafe fn downcast_mut_unchecked(&mut self) -> &mut T {
        &mut *(to_trait_object(self).data as *mut T)
    }

    fn downcast_mut(&mut self) -> Result<&mut T, TypeMismatch> {
        if self.is_type() {
            Ok(unsafe { self.downcast_mut_unchecked() })
        } else {
            Err(TypeMismatch::new::<T, Self>(self))
        }
    }

    #[cfg(feature = "std")]
    unsafe fn downcast_unchecked(self: Box<Self>) -> Box<T> {
        let ret: Box<T> = Box::from_raw(to_trait_object(&*self).data as *mut T);
        mem::forget(self);
        ret
    }

    #[cfg(feature = "std")]
    fn downcast(self: Box<Self>) -> Result<Box<T>, DowncastError<Box<Self>>> {
        if self.is_type() {
            Ok(unsafe { self.downcast_unchecked() })
        } else {
            let mismatch = TypeMismatch::new::<T, Self>(&*self);
            Err(DowncastError::new(mismatch, self))
        }
    }
}

// ++++++++++++++++++++ macros ++++++++++++++++++++

#[doc(hidden)]
pub mod _std {
    pub use std::*;
}

/// Implements [`Downcast`](trait.Downcast.html) for your trait-object-type.
///
/// ```ignore
/// impl_downcast!(Foo);
/// impl_downcast!(<B> Foo<B> where B: Bar);
/// impl_downcast!(<B> Foo<Bar = B>);
/// ```
///
/// expands to
///
/// ```ignore
/// impl<T> Downcast<T> for Foo
///     where T: Any
/// {}
///
/// impl<T, B> Downcast<T> for Foo<B>
///     where T: Any, B: Bar
/// {}
///
/// impl<T, B> Downcast<T> for Foo<Bar = B>
///     where T: Any
/// {}
/// ```
#[macro_export]
macro_rules! impl_downcast {
    (<$($params:ident),+ $(,)*> $base:ty $(where $($bounds:tt)+)*) => {
        impl<_T: $crate::Any, $($params),+> $crate::Downcast<_T> for $base
            where _T: $crate::Any, $($params: 'static,)* $($($bounds)+)*
        {}
    };
    ($base:ty) => {
        impl<_T: $crate::Any> $crate::Downcast<_T> for $base
            where _T: $crate::Any
        {}
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! downcast_methods_core {
    (@items) => {
        #[allow(unused)]
        pub fn is<_T>(&self) -> bool
            where _T: $crate::Any, Self: $crate::Downcast<_T>
        {
            $crate::Downcast::<_T>::is_type(self)
        }

        #[allow(unused)]
        pub unsafe fn downcast_ref_unchecked<_T>(&self) -> &_T
            where _T: $crate::Any, Self: $crate::Downcast<_T>
        {
            $crate::Downcast::<_T>::downcast_ref_unchecked(self)
        }

        #[allow(unused)]
        pub fn downcast_ref<_T>(&self) -> $crate::_std::result::Result<&_T, $crate::TypeMismatch>
            where _T: $crate::Any, Self: $crate::Downcast<_T>
        {
            $crate::Downcast::<_T>::downcast_ref(self)
        }

        #[allow(unused)]
        pub unsafe fn downcast_mut_unchecked<_T>(&mut self) -> &mut _T
            where _T: $crate::Any, Self: $crate::Downcast<_T>
        {
            $crate::Downcast::<_T>::downcast_mut_unchecked(self)
        }

        #[allow(unused)]
        pub fn downcast_mut<_T>(&mut self) -> $crate::_std::result::Result<&mut _T, $crate::TypeMismatch>
            where _T: $crate::Any, Self: $crate::Downcast<_T>
        {
            $crate::Downcast::<_T>::downcast_mut(self)
        }
    };
    (<$($params:ident),+ $(,)*> $base:ty $(where $($bounds:tt)+)*) => {
        impl<$($params),+> $base
            where $($params: 'static,)* $($($bounds)+)*
        {
            downcast_methods_core!(@items);
        }
    };
    ($base:ty) => {
        impl $base {
            downcast_methods_core!(@items);
        }
    };
}

#[doc(hidden)]
#[macro_export]
macro_rules! downcast_methods_std {
    (@items) => {
        downcast_methods_core!(@items);

        #[allow(unused)]
        pub unsafe fn downcast_unchecked<_T>(self: $crate::_std::boxed::Box<Self>) -> $crate::_std::boxed::Box<_T>
            where _T: $crate::Any, Self: $crate::Downcast<_T>
        {
            $crate::Downcast::<_T>::downcast_unchecked(self)
        }

        #[allow(unused)]
        pub fn downcast<_T>(self: $crate::_std::boxed::Box<Self>) -> $crate::_std::result::Result<$crate::_std::boxed::Box<_T>, $crate::DowncastError<Box<Self>>>
            where _T: $crate::Any, Self: $crate::Downcast<_T>
        {
            $crate::Downcast::<_T>::downcast(self)
        }
    };
    (<$($params:ident),+ $(,)*> $base:ty $(where $($bounds:tt)+)*) => {
        impl<$($params),+> $base
            $(where $($bounds)+)*
        {
            downcast_methods_std!(@items);
        }
    };
    ($base:ty) => {
        impl $base {
            downcast_methods_std!(@items);
        }
    };
}

/// Generate `downcast`-methods for your trait-object-type.
///
/// ```ignore
/// downcast_methods!(Foo);
/// downcast_methods!(<B> Foo<B> where B: Bar);
/// downcast_methods!(<B> Foo<Bar = B>);
/// ```
///
/// ```ignore
/// /* 1st */ impl Foo {
/// /* 2nd */ impl<B> Foo<B> where B: Bar {
/// /* 3nd */ impl<B> Foo<Bar = B> {
///
///     pub fn is<T>(&self) -> bool
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub unsafe fn downcast_ref_unchecked<T>(&self) -> &T
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub fn downcast_ref<T>(&self) -> Result<&T, TypeMismatch>
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub unsafe fn downcast_mut_unchecked<T>(&mut self) -> &mut T
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub fn downcast_mut<T>(&mut self) -> Result<&mut T, TypeMismatch>
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub unsafe fn downcast_unchecked<T>(self: Box<Self>) -> Box<T>
///         where T: Any, Self: Downcast<T>
///     { ... }
/// }
/// ```
#[cfg(not(feature = "std"))]
#[macro_export]
macro_rules! downcast_methods {
    ($($tt:tt)+) => { downcast_methods_core!($($tt)+); }
}

/// Generate `downcast`-methods for your trait-object-type.
///
/// ```ignore
/// downcast_methods!(Foo);
/// downcast_methods!(<B> Foo<B> where B: Bar);
/// downcast_methods!(<B> Foo<Bar = B>);
/// ```
///
/// ```ignore
/// /* 1st */ impl Foo {
/// /* 2nd */ impl<B> Foo<B> where B: Bar {
/// /* 3nd */ impl<B> Foo<Bar = B> {
///
///     pub fn is<T>(&self) -> bool
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub unsafe fn downcast_ref_unchecked<T>(&self) -> &T
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub fn downcast_ref<T>(&self) -> Result<&T, TypeMismatch>
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub unsafe fn downcast_mut_unchecked<T>(&mut self) -> &mut T
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub fn downcast_mut<T>(&mut self) -> Result<&mut T, TypeMismatch>
///         where T: Any, Self: Downcast<T>
///     { ... }
///
///     pub unsafe fn downcast_unchecked<T>(self: Box<Self>) -> Box<T>
///         where T: Any, Self: Downcast<T>
///     { ... }
///
/// pub fn downcast<T>(self: Box<Self>) -> Result<Box<T>,
/// DowncastError<Box<T>>>
///         where T: Any, Self: Downcast<T>
///     { ... }
/// }
/// ```
#[cfg(feature = "std")]
#[macro_export]
macro_rules! downcast_methods {
    ($($tt:tt)+) => { downcast_methods_std!($($tt)+); }
}

/// Implements [`Downcast`](trait.downcast.html) and generates
/// `downcast`-methods for your trait-object-type.
///
/// See [`impl_downcast`](macro.impl_downcast.html),
/// [`downcast_methods`](macro.downcast_methods.html).
#[macro_export]
macro_rules! downcast {
    ($($tt:tt)+) => {
        impl_downcast!($($tt)+);
        downcast_methods!($($tt)+);
    }
}

// NOTE: We only implement the trait, because implementing the methods won't
// be possible when we replace downcast::Any by std::any::Any.
mod any_impls {
    use super::Any;

    impl_downcast!(Any);
    impl_downcast!((Any + Send));
    impl_downcast!((Any + Sync));
    impl_downcast!((Any + Send + Sync));
}
