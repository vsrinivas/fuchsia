// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::ops::Deref;

use zerocopy::ByteSlice;

use crate::BufferView;

/// A packet that can be created from a raw form.
///
/// `FromRaw` provides a common interface for packets that can be created from
/// an "unchecked" form - that is, that are parsed raw without any higher-order
/// validation.
///
/// The type parameter `R` is the raw type that the `FromRaw` type can be
/// converted from, given some arguments of type `A`.
pub trait FromRaw<R, A>: Sized {
    /// The type of error that may happen during validation.
    type Error;

    /// Attempts to create `Self` from the raw form in `raw` with `args`.
    fn try_from_raw_with(raw: R, args: A) -> Result<Self, Self::Error>;

    /// Attempts to create `Self` from the raw form in `raw`.
    fn try_from_raw(raw: R) -> Result<Self, <Self as FromRaw<R, A>>::Error>
    where
        Self: FromRaw<R, (), Error = <Self as FromRaw<R, A>>::Error>,
    {
        Self::try_from_raw_with(raw, ())
    }
}

/// A type that encapsulates the result of a complete or incomplete parsing
/// operation.
///
/// The type parameters `C` and `I` are the types for a "complete" and
/// "incomplete" parsing result, respectively.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum MaybeParsed<C, I> {
    Complete(C),
    Incomplete(I),
}

impl<T> MaybeParsed<T, T> {
    /// Creates a `MaybeParsed` instance with `bytes` observing a minimum
    /// length `min_len`.
    ///
    /// Returns [`MaybeParsed::Complete`] if `bytes` is at least `min_len` long,
    /// otherwise returns [`MaybeParsed::Incomplete`]. In both cases, `bytes`
    /// is moved into one of the two `MaybeParsed` variants.
    pub fn new_with_min_len(bytes: T, min_len: usize) -> Self
    where
        T: ByteSlice,
    {
        if bytes.len() >= min_len {
            MaybeParsed::Complete(bytes)
        } else {
            MaybeParsed::Incomplete(bytes)
        }
    }

    /// Consumes this `MaybeParsed` and returns its contained value if both the
    /// `Complete` and `Incomplete` variants contain the same type.
    pub fn into_inner(self) -> T {
        match self {
            MaybeParsed::Complete(c) => c,
            MaybeParsed::Incomplete(i) => i,
        }
    }
}

impl<C, I> MaybeParsed<C, I> {
    /// Creates a `MaybeParsed` instance taking `n` bytes from the front of
    /// `buf` and mapping with `map`.
    ///
    /// If `buf` contains at least `n` bytes, then `n` bytes are consumed from
    /// the beginning of `buf`, those `n` bytes are passed to `map`, and the
    /// result is returned as [`MaybeParsed::Complete`]. Otherwise, all bytes
    /// are consumed from `buf` and returned as [`MaybeParsed::Incomplete`].
    pub fn take_from_buffer_with<BV: BufferView<I>, F>(buf: &mut BV, n: usize, map: F) -> Self
    where
        F: FnOnce(I) -> C,
        I: ByteSlice,
    {
        if let Some(v) = buf.take_front(n) {
            MaybeParsed::Complete(map(v))
        } else {
            MaybeParsed::Incomplete(buf.take_rest_front())
        }
    }

    /// Maps a [`MaybeParsed::Complete`] variant to another type.
    ///
    /// If `self` is [`MaybeParsed::Incomplete`], it is left as-is.
    pub fn map<M, F>(self, f: F) -> MaybeParsed<M, I>
    where
        F: FnOnce(C) -> M,
    {
        match self {
            MaybeParsed::Incomplete(v) => MaybeParsed::Incomplete(v),
            MaybeParsed::Complete(v) => MaybeParsed::Complete(f(v)),
        }
    }

    /// Maps a [`MaybeParsed::Incomplete`] variant to another type.
    ///
    /// If `self` is [`MaybeParsed::Complete`], it is left as-is.
    pub fn map_incomplete<M, F>(self, f: F) -> MaybeParsed<C, M>
    where
        F: FnOnce(I) -> M,
    {
        match self {
            MaybeParsed::Incomplete(v) => MaybeParsed::Incomplete(f(v)),
            MaybeParsed::Complete(v) => MaybeParsed::Complete(v),
        }
    }

    /// Converts from `&MaybeParsed<C, I>` to `MaybeParsed<&C, &I>`.
    pub fn as_ref(&self) -> MaybeParsed<&C, &I> {
        match self {
            MaybeParsed::Incomplete(v) => MaybeParsed::Incomplete(v),
            MaybeParsed::Complete(v) => MaybeParsed::Complete(v),
        }
    }

    /// Transforms `self` into a [`Result`], mapping the [`Complete`] variant
    /// into [`Ok`].
    ///
    /// [`Complete`]: Self::Complete
    /// [`Ok`]: Result::Ok
    pub fn complete(self) -> Result<C, I> {
        match self {
            MaybeParsed::Complete(v) => Ok(v),
            MaybeParsed::Incomplete(v) => Err(v),
        }
    }

    /// Transforms `self` into a [`Result`], mapping the [`Incomplete`] variant
    /// into [`Ok`].
    ///
    /// [`Incomplete`]: Self::Incomplete
    /// [`Ok`]: Result::Ok
    pub fn incomplete(self) -> Result<I, C> {
        match self {
            MaybeParsed::Complete(v) => Err(v),
            MaybeParsed::Incomplete(v) => Ok(v),
        }
    }

    /// Transforms this `MaybeIncomplete` into a [`Result`] where the
    /// [`Complete`] variant becomes [`Ok`] and the [`Incomplete`] variant is
    /// passed through `f` and mapped to [`Err`].
    ///
    /// [`Complete`]: Self::Complete
    /// [`Incomplete`]: Self::Incomplete
    /// [`Ok`]: Result::Ok
    /// [`Err`]: Result::Err
    pub fn ok_or_else<F, E>(self, f: F) -> Result<C, E>
    where
        F: FnOnce(I) -> E,
    {
        match self {
            MaybeParsed::Complete(v) => Ok(v),
            MaybeParsed::Incomplete(v) => Err(f(v)),
        }
    }
}

impl<C, I> MaybeParsed<C, I>
where
    C: Deref<Target = [u8]>,
    I: Deref<Target = [u8]>,
{
    /// Returns the length in bytes of the contained data.
    pub fn len(&self) -> usize {
        match self {
            MaybeParsed::Incomplete(v) => v.deref().len(),
            MaybeParsed::Complete(v) => v.deref().len(),
        }
    }

    /// Returns whether the contained data is empty - zero bytes long.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    impl<T> MaybeParsed<T, T> {
        /// Creates a `MaybeParsed` instance taking `n` bytes from the front of
        /// `buff`.
        ///
        /// Returns [`MaybeParsed::Complete`] with `n` bytes if `buff` contains at
        /// least `n` bytes. Otherwise returns [`MaybeParsed::Incomplete`] greedily
        /// taking all the remaining bytes from `buff`
        #[cfg(test)]
        pub fn take_from_buffer<BV: BufferView<T>>(buff: &mut BV, n: usize) -> Self
        where
            T: ByteSlice,
        {
            if let Some(v) = buff.take_front(n) {
                MaybeParsed::Complete(v)
            } else {
                MaybeParsed::Incomplete(buff.take_rest_front())
            }
        }
    }

    #[test]
    fn test_maybe_parsed_take_from_buffer() {
        let buff = [1_u8, 2, 3, 4];
        let mut bv = &mut &buff[..];
        assert_eq!(MaybeParsed::take_from_buffer(&mut bv, 2), MaybeParsed::Complete(&buff[..2]));
        assert_eq!(MaybeParsed::take_from_buffer(&mut bv, 3), MaybeParsed::Incomplete(&buff[2..]));
    }

    #[test]
    fn test_maybe_parsed_min_len() {
        let buff = [1_u8, 2, 3, 4];
        assert_eq!(MaybeParsed::new_with_min_len(&buff[..], 3), MaybeParsed::Complete(&buff[..]));
        assert_eq!(MaybeParsed::new_with_min_len(&buff[..], 5), MaybeParsed::Incomplete(&buff[..]));
    }

    #[test]
    fn test_maybe_parsed_take_from_buffer_with() {
        let buff = [1_u8, 2, 3, 4];
        let mut bv = &mut &buff[..];
        assert_eq!(
            MaybeParsed::take_from_buffer_with(&mut bv, 2, |x| Some(usize::from(x[0] + x[1]))),
            MaybeParsed::Complete(Some(3)),
        );
        assert_eq!(
            MaybeParsed::take_from_buffer_with(&mut bv, 3, |_| panic!("map shouldn't be called")),
            MaybeParsed::Incomplete(&buff[2..]),
        );
    }

    #[test]
    fn test_maybe_parsed_map() {
        assert_eq!(
            MaybeParsed::<&str, ()>::Complete("hello").map(|x| format!("{} you", x)),
            MaybeParsed::Complete("hello you".to_string()),
        );
        assert_eq!(
            MaybeParsed::<(), &str>::Incomplete("hello").map(|_| panic!("map shouldn't be called")),
            MaybeParsed::Incomplete("hello"),
        );
    }

    #[test]
    fn test_maybe_parsed_len() {
        let buff = [1_u8, 2, 3, 4];
        let mp1 = MaybeParsed::new_with_min_len(&buff[..], 2);
        let mp2 = MaybeParsed::new_with_min_len(&buff[..], 10);
        assert_eq!(mp1.len(), 4);
        assert_eq!(mp2.len(), 4);
    }
}
