//! Contains utility functions and traits to convert between vectors of `u16` bits and `f16` or
//! `bf16` vectors.
//!
//! The utility [`HalfBitsVecExt`] sealed extension trait is implemented for `Vec<u16>` vectors,
//! while the utility [`HalfFloatVecExt`] sealed extension trait is implemented for both `Vec<f16>`
//! and `Vec<bf16>` vectors. These traits provide efficient conversions and reinterpret casting of
//! larger buffers of floating point values, and are automatically included in the [`prelude`]
//! module.
//!
//! This module is only available with the `std` or `alloc` feature.
//!
//! [`HalfBitsVecExt`]: trait.HalfBitsVecExt.html
//! [`HalfFloatVecExt`]: trait.HalfFloatVecExt.html
//! [`prelude`]: ../prelude/index.html

#![cfg(any(feature = "alloc", feature = "std"))]

use super::{bf16, f16, slice::HalfFloatSliceExt};
#[cfg(all(feature = "alloc", not(feature = "std")))]
use alloc::vec::Vec;
use core::mem;

/// Extensions to `Vec<f16>` and `Vec<bf16>` to support reinterpret operations.
///
/// This trait is sealed and cannot be implemented outside of this crate.
pub trait HalfFloatVecExt: private::SealedHalfFloatVec {
    /// Reinterpret a vector of [`f16`](../struct.f16.html) or [`bf16`](../struct.bf16.html)
    /// numbers as a vector of `u16` bits.
    ///
    /// This is a zero-copy operation. The reinterpreted vector has the same memory location as
    /// `self`.
    ///
    /// # Examples
    ///
    /// ```rust
    /// # use half::prelude::*;
    /// let float_buffer = vec![f16::from_f32(1.), f16::from_f32(2.), f16::from_f32(3.)];
    /// let int_buffer = float_buffer.reinterpret_into();
    ///
    /// assert_eq!(int_buffer, [f16::from_f32(1.).to_bits(), f16::from_f32(2.).to_bits(), f16::from_f32(3.).to_bits()]);
    /// ```
    fn reinterpret_into(self) -> Vec<u16>;

    /// Convert all of the elements of a `[f32]` slice into a new [`f16`](../struct.f16.html) or
    /// [`bf16`](../struct.bf16.html) vector.
    ///
    /// The conversion operation is vectorized over the slice, meaning the conversion may be more
    /// efficient than converting individual elements on some hardware that supports SIMD
    /// conversions. See [crate documentation](../index.html) for more information on hardware
    /// conversion support.
    ///
    /// # Examples
    /// ```rust
    /// # use half::prelude::*;
    /// let float_values = [1., 2., 3., 4.];
    /// let vec: Vec<f16> = Vec::from_f32_slice(&float_values);
    ///
    /// assert_eq!(vec, vec![f16::from_f32(1.), f16::from_f32(2.), f16::from_f32(3.), f16::from_f32(4.)]);
    /// ```
    fn from_f32_slice(slice: &[f32]) -> Self;

    /// Convert all of the elements of a `[f64]` slice into a new [`f16`](../struct.f16.html) or
    /// [`bf16`](../struct.bf16.html) vector.
    ///
    /// The conversion operation is vectorized over the slice, meaning the conversion may be more
    /// efficient than converting individual elements on some hardware that supports SIMD
    /// conversions. See [crate documentation](../index.html) for more information on hardware
    /// conversion support.
    ///
    /// # Examples
    /// ```rust
    /// # use half::prelude::*;
    /// let float_values = [1., 2., 3., 4.];
    /// let vec: Vec<f16> = Vec::from_f64_slice(&float_values);
    ///
    /// assert_eq!(vec, vec![f16::from_f64(1.), f16::from_f64(2.), f16::from_f64(3.), f16::from_f64(4.)]);
    /// ```
    fn from_f64_slice(slice: &[f64]) -> Self;
}

/// Extensions to `Vec<u16>` to support reinterpret operations.
///
/// This trait is sealed and cannot be implemented outside of this crate.
pub trait HalfBitsVecExt: private::SealedHalfBitsVec {
    /// Reinterpret a vector of `u16` bits as a vector of [`f16`](../struct.f16.html) or
    /// [`bf16`](../struct.bf16.html) numbers.
    ///
    /// `H` is the type to cast to, and must be either the [`f16`](../struct.f16.html) or
    /// [`bf16`](../struct.bf16.html) type.
    ///
    /// This is a zero-copy operation. The reinterpreted vector has the same memory location as
    /// `self`.
    ///
    /// # Examples
    ///
    /// ```rust
    /// # use half::prelude::*;
    /// let int_buffer = vec![f16::from_f32(1.).to_bits(), f16::from_f32(2.).to_bits(), f16::from_f32(3.).to_bits()];
    /// let float_buffer = int_buffer.reinterpret_into::<f16>();
    ///
    /// assert_eq!(float_buffer, [f16::from_f32(1.), f16::from_f32(2.), f16::from_f32(3.)]);
    /// ```
    fn reinterpret_into<H>(self) -> Vec<H>
    where
        H: crate::private::SealedHalf;
}

mod private {
    use crate::{bf16, f16};
    #[cfg(all(feature = "alloc", not(feature = "std")))]
    use alloc::vec::Vec;

    pub trait SealedHalfFloatVec {}
    impl SealedHalfFloatVec for Vec<f16> {}
    impl SealedHalfFloatVec for Vec<bf16> {}

    pub trait SealedHalfBitsVec {}
    impl SealedHalfBitsVec for Vec<u16> {}
}

impl HalfFloatVecExt for Vec<f16> {
    #[inline]
    fn reinterpret_into(mut self) -> Vec<u16> {
        // An f16 array has same length and capacity as u16 array
        let length = self.len();
        let capacity = self.capacity();

        // Actually reinterpret the contents of the Vec<f16> as u16,
        // knowing that structs are represented as only their members in memory,
        // which is the u16 part of `f16(u16)`
        let pointer = self.as_mut_ptr() as *mut u16;

        // Prevent running a destructor on the old Vec<u16>, so the pointer won't be deleted
        mem::forget(self);

        // Finally construct a new Vec<f16> from the raw pointer
        // SAFETY: We are reconstructing full length and capacity of original vector,
        // using its original pointer, and the size of elements are identical.
        unsafe { Vec::from_raw_parts(pointer, length, capacity) }
    }

    fn from_f32_slice(slice: &[f32]) -> Self {
        let mut vec = Vec::with_capacity(slice.len());
        // SAFETY: convert will initialize every value in the vector without reading them,
        // so this is safe to do instead of double initialize from resize, and we're setting it to
        // same value as capacity.
        unsafe { vec.set_len(slice.len()) };
        vec.convert_from_f32_slice(&slice);
        vec
    }

    fn from_f64_slice(slice: &[f64]) -> Self {
        let mut vec = Vec::with_capacity(slice.len());
        // SAFETY: convert will initialize every value in the vector without reading them,
        // so this is safe to do instead of double initialize from resize, and we're setting it to
        // same value as capacity.
        unsafe { vec.set_len(slice.len()) };
        vec.convert_from_f64_slice(&slice);
        vec
    }
}

impl HalfFloatVecExt for Vec<bf16> {
    #[inline]
    fn reinterpret_into(mut self) -> Vec<u16> {
        // An f16 array has same length and capacity as u16 array
        let length = self.len();
        let capacity = self.capacity();

        // Actually reinterpret the contents of the Vec<f16> as u16,
        // knowing that structs are represented as only their members in memory,
        // which is the u16 part of `f16(u16)`
        let pointer = self.as_mut_ptr() as *mut u16;

        // Prevent running a destructor on the old Vec<u16>, so the pointer won't be deleted
        mem::forget(self);

        // Finally construct a new Vec<f16> from the raw pointer
        // SAFETY: We are reconstructing full length and capacity of original vector,
        // using its original pointer, and the size of elements are identical.
        unsafe { Vec::from_raw_parts(pointer, length, capacity) }
    }

    fn from_f32_slice(slice: &[f32]) -> Self {
        let mut vec = Vec::with_capacity(slice.len());
        // SAFETY: convert will initialize every value in the vector without reading them,
        // so this is safe to do instead of double initialize from resize, and we're setting it to
        // same value as capacity.
        unsafe { vec.set_len(slice.len()) };
        vec.convert_from_f32_slice(&slice);
        vec
    }

    fn from_f64_slice(slice: &[f64]) -> Self {
        let mut vec = Vec::with_capacity(slice.len());
        // SAFETY: convert will initialize every value in the vector without reading them,
        // so this is safe to do instead of double initialize from resize, and we're setting it to
        // same value as capacity.
        unsafe { vec.set_len(slice.len()) };
        vec.convert_from_f64_slice(&slice);
        vec
    }
}

impl HalfBitsVecExt for Vec<u16> {
    // This is safe because all traits are sealed
    #[inline]
    fn reinterpret_into<H>(mut self) -> Vec<H>
    where
        H: crate::private::SealedHalf,
    {
        // An f16 array has same length and capacity as u16 array
        let length = self.len();
        let capacity = self.capacity();

        // Actually reinterpret the contents of the Vec<u16> as f16,
        // knowing that structs are represented as only their members in memory,
        // which is the u16 part of `f16(u16)`
        let pointer = self.as_mut_ptr() as *mut H;

        // Prevent running a destructor on the old Vec<u16>, so the pointer won't be deleted
        mem::forget(self);

        // Finally construct a new Vec<f16> from the raw pointer
        // SAFETY: We are reconstructing full length and capacity of original vector,
        // using its original pointer, and the size of elements are identical.
        unsafe { Vec::from_raw_parts(pointer, length, capacity) }
    }
}

/// Converts a vector of `u16` elements into a vector of [`f16`](../struct.f16.html) elements.
///
/// This function merely reinterprets the contents of the vector, so it's a zero-copy operation.
#[deprecated(
    since = "1.4.0",
    note = "use [`HalfBitsVecExt::reinterpret_into`](trait.HalfBitsVecExt.html#tymethod.reinterpret_into) instead"
)]
#[inline]
pub fn from_bits(bits: Vec<u16>) -> Vec<f16> {
    bits.reinterpret_into()
}

/// Converts a vector of [`f16`](../struct.f16.html) elements into a vector of `u16` elements.
///
/// This function merely reinterprets the contents of the vector, so it's a zero-copy operation.
#[deprecated(
    since = "1.4.0",
    note = "use [`HalfFloatVecExt::reinterpret_into`](trait.HalfFloatVecExt.html#tymethod.reinterpret_into) instead"
)]
#[inline]
pub fn to_bits(numbers: Vec<f16>) -> Vec<u16> {
    numbers.reinterpret_into()
}

#[cfg(test)]
mod test {
    use super::{HalfBitsVecExt, HalfFloatVecExt};
    use crate::{bf16, f16};
    #[cfg(all(feature = "alloc", not(feature = "std")))]
    use alloc::vec;

    #[test]
    fn test_vec_conversions_f16() {
        let numbers = vec![f16::E, f16::PI, f16::EPSILON, f16::FRAC_1_SQRT_2];
        let bits = vec![
            f16::E.to_bits(),
            f16::PI.to_bits(),
            f16::EPSILON.to_bits(),
            f16::FRAC_1_SQRT_2.to_bits(),
        ];
        let bits_cloned = bits.clone();

        // Convert from bits to numbers
        let from_bits = bits.reinterpret_into::<f16>();
        assert_eq!(&from_bits[..], &numbers[..]);

        // Convert from numbers back to bits
        let to_bits = from_bits.reinterpret_into();
        assert_eq!(&to_bits[..], &bits_cloned[..]);
    }

    #[test]
    fn test_vec_conversions_bf16() {
        let numbers = vec![bf16::E, bf16::PI, bf16::EPSILON, bf16::FRAC_1_SQRT_2];
        let bits = vec![
            bf16::E.to_bits(),
            bf16::PI.to_bits(),
            bf16::EPSILON.to_bits(),
            bf16::FRAC_1_SQRT_2.to_bits(),
        ];
        let bits_cloned = bits.clone();

        // Convert from bits to numbers
        let from_bits = bits.reinterpret_into::<bf16>();
        assert_eq!(&from_bits[..], &numbers[..]);

        // Convert from numbers back to bits
        let to_bits = from_bits.reinterpret_into();
        assert_eq!(&to_bits[..], &bits_cloned[..]);
    }
}
