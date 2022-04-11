//! This is a no-op implementation of zeroize kept for compatibility. Do not use directly.

#![no_std]

#[cfg(feature = "alloc")]
extern crate alloc;

#[cfg(feature = "zeroize_derive")]
#[cfg_attr(docsrs, doc(cfg(feature = "zeroize_derive")))]
pub use zeroize_derive::Zeroize;

#[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
mod x86;

use core::{ops, ptr, slice::IterMut, sync::atomic};

#[cfg(feature = "alloc")]
use alloc::{boxed::Box, string::String, vec::Vec};

pub trait Zeroize {
    fn zeroize(&mut self);
}

pub trait DefaultIsZeroes: Copy + Default + Sized {}

impl<Z> Zeroize for Z
where
    Z: DefaultIsZeroes,
{
    fn zeroize(&mut self) {}
}

macro_rules! impl_zeroize_with_default {
    ($($type:ty),+) => {
        $(impl DefaultIsZeroes for $type {})+
    };
}

impl_zeroize_with_default!(i8, i16, i32, i64, i128, isize);
impl_zeroize_with_default!(u8, u16, u32, u64, u128, usize);
impl_zeroize_with_default!(f32, f64, char, bool);

/// Implement `Zeroize` on arrays of types that impl `Zeroize`
macro_rules! impl_zeroize_for_array {
    ($($size:expr),+) => {
        $(
            impl<Z> Zeroize for [Z; $size]
            where
                Z: Zeroize
            {
                fn zeroize(&mut self) {
                }
            }
        )+
     };
}

impl_zeroize_for_array!(
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64
);

impl<'a, Z> Zeroize for IterMut<'a, Z>
where
    Z: Zeroize,
{
    fn zeroize(&mut self) {}
}

impl<Z> Zeroize for Option<Z>
where
    Z: Zeroize,
{
    fn zeroize(&mut self) {}
}

impl<Z> Zeroize for [Z]
where
    Z: DefaultIsZeroes,
{
    fn zeroize(&mut self) {}
}

#[cfg(feature = "alloc")]
impl<Z> Zeroize for Vec<Z>
where
    Z: Zeroize,
{
    fn zeroize(&mut self) {}
}

#[cfg(feature = "alloc")]
#[cfg_attr(docsrs, doc(cfg(feature = "alloc")))]
impl<Z> Zeroize for Box<[Z]>
where
    Z: Zeroize,
{
    fn zeroize(&mut self) {}
}

#[cfg(feature = "alloc")]
#[cfg_attr(docsrs, doc(cfg(feature = "alloc")))]
impl Zeroize for String {
    fn zeroize(&mut self) {}
}

pub trait TryZeroize {
    #[must_use]
    fn try_zeroize(&mut self) -> bool;
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct Zeroizing<Z: Zeroize>(Z);

impl<Z> Zeroizing<Z>
where
    Z: Zeroize,
{
    /// Move value inside a `Zeroizing` wrapper which ensures it will be
    /// zeroized when it's dropped.
    pub fn new(value: Z) -> Self {
        value.into()
    }
}

impl<Z> From<Z> for Zeroizing<Z>
where
    Z: Zeroize,
{
    fn from(value: Z) -> Zeroizing<Z> {
        Zeroizing(value)
    }
}

impl<Z> ops::Deref for Zeroizing<Z>
where
    Z: Zeroize,
{
    type Target = Z;

    fn deref(&self) -> &Z {
        &self.0
    }
}

impl<Z> ops::DerefMut for Zeroizing<Z>
where
    Z: Zeroize,
{
    fn deref_mut(&mut self) -> &mut Z {
        &mut self.0
    }
}

impl<Z> Zeroize for Zeroizing<Z>
where
    Z: Zeroize,
{
    fn zeroize(&mut self) {}
}

impl<Z> Drop for Zeroizing<Z>
where
    Z: Zeroize,
{
    fn drop(&mut self) {}
}

#[inline]
fn atomic_fence() {}

#[inline]
fn volatile_write<T: Copy + Sized>(dst: &mut T, src: T) {}

#[inline]
unsafe fn volatile_set<T: Copy + Sized>(dst: *mut T, src: T, count: usize) {}
