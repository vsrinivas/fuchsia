// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Serialization.

use std::cmp;
use std::fmt::{self, Debug, Formatter};
use std::ops::{Range, RangeBounds};

use never::Never;

use crate::{
    canonicalize_range, take_back, take_back_mut, take_front, take_front_mut, Buffer, BufferMut,
    BufferView, BufferViewMut, ParsablePacket, ParseBuffer, ParseBufferMut, SerializeBuffer,
};

/// Either of two buffers.
///
/// An `Either` wraps one of two different buffer types. It implements all of
/// the relevant traits by calling the corresponding methods on the wrapped
/// buffer.
#[derive(Copy, Clone, Debug)]
pub enum Either<A, B> {
    A(A),
    B(B),
}

impl<A> Either<A, A> {
    /// Return the inner value held by this `Either` when both
    /// possible values `Either::A` and `Either::B` have the
    /// same inner types.
    pub fn into_inner(self) -> A {
        match self {
            Either::A(x) => x,
            Either::B(x) => x,
        }
    }
}

macro_rules! call_method_on_either {
    ($val:expr, $method:ident, $($args:expr),*) => {
        match $val {
            Either::A(a) => a.$method($($args),*),
            Either::B(b) => b.$method($($args),*),
        }
    };
    ($val:expr, $method:ident) => {
        call_method_on_either!($val, $method,)
    };
}

// NOTE(joshlf): We override the default implementations of all methods for
// Either. Many of the default implementations make multiple calls to other
// Buffer methods, each of which performs a match statement to figure out which
// Either variant is present. We assume that doing this match once is more
// performant than doing it multiple times.

impl<A, B> ParseBuffer for Either<A, B>
where
    A: ParseBuffer,
    B: ParseBuffer,
{
    fn shrink<R: RangeBounds<usize>>(&mut self, range: R) {
        call_method_on_either!(self, shrink, range)
    }
    fn len(&self) -> usize {
        call_method_on_either!(self, len)
    }
    fn is_empty(&self) -> bool {
        call_method_on_either!(self, is_empty)
    }
    fn shrink_front(&mut self, n: usize) {
        call_method_on_either!(self, shrink_front, n)
    }
    fn shrink_back(&mut self, n: usize) {
        call_method_on_either!(self, shrink_back, n)
    }
    fn parse<'a, P: ParsablePacket<&'a [u8], ()>>(&'a mut self) -> Result<P, P::Error> {
        call_method_on_either!(self, parse)
    }
    fn parse_with<'a, ParseArgs, P: ParsablePacket<&'a [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        call_method_on_either!(self, parse_with, args)
    }
    fn as_buf(&self) -> Buf<&[u8]> {
        call_method_on_either!(self, as_buf)
    }
}

impl<A, B> ParseBufferMut for Either<A, B>
where
    A: ParseBufferMut,
    B: ParseBufferMut,
{
    fn parse_mut<'a, P: ParsablePacket<&'a mut [u8], ()>>(&'a mut self) -> Result<P, P::Error> {
        call_method_on_either!(self, parse_mut)
    }
    fn parse_with_mut<'a, ParseArgs, P: ParsablePacket<&'a mut [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        call_method_on_either!(self, parse_with_mut, args)
    }
    fn as_buf_mut(&mut self) -> Buf<&mut [u8]> {
        call_method_on_either!(self, as_buf_mut)
    }
}

impl<A, B> Buffer for Either<A, B>
where
    A: Buffer,
    B: Buffer,
{
    fn capacity(&self) -> usize {
        call_method_on_either!(self, capacity)
    }
    fn prefix_len(&self) -> usize {
        call_method_on_either!(self, prefix_len)
    }
    fn suffix_len(&self) -> usize {
        call_method_on_either!(self, suffix_len)
    }
    fn grow_front(&mut self, n: usize) {
        call_method_on_either!(self, grow_front, n)
    }
    fn grow_back(&mut self, n: usize) {
        call_method_on_either!(self, grow_back, n)
    }
    fn reset(&mut self) {
        call_method_on_either!(self, reset)
    }
}

impl<A, B> BufferMut for Either<A, B>
where
    A: BufferMut,
    B: BufferMut,
{
    fn reset_zero(&mut self) {
        call_method_on_either!(self, reset_zero)
    }
    fn serialize<BB: PacketBuilder>(&mut self, builder: BB) {
        call_method_on_either!(self, serialize, builder)
    }
}

impl<A: AsRef<[u8]>, B: AsRef<[u8]>> AsRef<[u8]> for Either<A, B> {
    fn as_ref(&self) -> &[u8] {
        call_method_on_either!(self, as_ref)
    }
}

impl<A: AsMut<[u8]>, B: AsMut<[u8]>> AsMut<[u8]> for Either<A, B> {
    fn as_mut(&mut self) -> &mut [u8] {
        call_method_on_either!(self, as_mut)
    }
}

impl<A, B> AsRef<Either<A, B>> for Either<A, B> {
    fn as_ref(&self) -> &Either<A, B> {
        self
    }
}

impl<A, B> AsMut<Either<A, B>> for Either<A, B> {
    fn as_mut(&mut self) -> &mut Either<A, B> {
        self
    }
}

/// A byte slice wrapper providing [`Buffer`] functionality.
///
/// A `Buf` wraps a byte slice (a type which implements `AsRef<[u8]>` or
/// `AsMut<[u8]>`) and implements `Buffer` and `BufferMut` by keeping track of
/// prefix, body, and suffix offsets within the byte slice.
#[derive(Clone, Debug)]
pub struct Buf<B> {
    buf: B,
    range: Range<usize>,
}

impl<B: AsRef<[u8]>> Buf<B> {
    /// Constructs a new `Buf`.
    ///
    /// `new` constructs a new `Buf` from a buffer and a range. The bytes within
    /// the range will be the body, the bytes before the range will be the
    /// prefix, and the bytes after the range will be the suffix.
    ///
    /// # Panics
    ///
    /// `new` panics if `range` is out of bounds of `buf`, or if it is
    /// nonsensical (the end precedes the start).
    pub fn new<R: RangeBounds<usize>>(buf: B, range: R) -> Buf<B> {
        let len = buf.as_ref().len();
        Buf { buf, range: canonicalize_range(len, &range) }
    }

    /// Constructs a [`BufView`] which will be a [`BufferView`] into this `Buf`.
    pub fn buffer_view(&mut self) -> BufView<'_> {
        BufView { buf: &self.buf.as_ref()[self.range.clone()], range: &mut self.range }
    }
}

impl<B: AsRef<[u8]> + AsMut<[u8]>> Buf<B> {
    /// Constructs a [`BufViewMut`] which will be a [`BufferViewMut`] into this `Buf`.
    pub fn buffer_view_mut(&mut self) -> BufViewMut<'_> {
        BufViewMut { buf: &mut self.buf.as_mut()[self.range.clone()], range: &mut self.range }
    }
}

impl<B: AsRef<[u8]>> ParseBuffer for Buf<B> {
    fn shrink<R: RangeBounds<usize>>(&mut self, range: R) {
        let len = self.len();
        let mut range = canonicalize_range(len, &range);
        range.start += self.range.start;
        range.end += self.range.start;
        self.range = range;
    }

    fn len(&self) -> usize {
        self.range.end - self.range.start
    }
    fn shrink_front(&mut self, n: usize) {
        assert!(n <= self.len());
        self.range.start += n;
    }
    fn shrink_back(&mut self, n: usize) {
        assert!(n <= self.len());
        self.range.end -= n;
    }
    fn parse_with<'a, ParseArgs, P: ParsablePacket<&'a [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse(self.buffer_view(), args)
    }
    fn as_buf(&self) -> Buf<&[u8]> {
        // TODO(joshlf): Once we have impl specialization, can we specialize this at all?
        Buf::new(self.buf.as_ref(), self.range.clone())
    }
}

impl<B: AsRef<[u8]> + AsMut<[u8]>> ParseBufferMut for Buf<B> {
    fn parse_with_mut<'a, ParseArgs, P: ParsablePacket<&'a mut [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse_mut(self.buffer_view_mut(), args)
    }
    fn as_buf_mut(&mut self) -> Buf<&mut [u8]> {
        Buf::new(self.buf.as_mut(), self.range.clone())
    }
}

impl<B: AsRef<[u8]>> Buffer for Buf<B> {
    fn capacity(&self) -> usize {
        self.buf.as_ref().len()
    }
    fn prefix_len(&self) -> usize {
        self.range.start
    }
    fn suffix_len(&self) -> usize {
        self.buf.as_ref().len() - self.range.end
    }
    fn grow_front(&mut self, n: usize) {
        assert!(n <= self.range.start);
        self.range.start -= n;
    }
    fn grow_back(&mut self, n: usize) {
        assert!(n <= self.buf.as_ref().len() - self.range.end);
        self.range.end += n;
    }
}

impl<B: AsRef<[u8]> + AsMut<[u8]>> BufferMut for Buf<B> {}

impl<B: AsRef<[u8]>> AsRef<[u8]> for Buf<B> {
    fn as_ref(&self) -> &[u8] {
        &self.buf.as_ref()[self.range.clone()]
    }
}

impl<B: AsMut<[u8]>> AsMut<[u8]> for Buf<B> {
    fn as_mut(&mut self) -> &mut [u8] {
        &mut self.buf.as_mut()[self.range.clone()]
    }
}

/// A [`BufferView`] into a [`Buf`].
///
/// A `BufView` is constructed by [`Buf::buffer_view`], and implements
/// `BufferView`, providing a view into the `Buf` from which it was constructed.
pub struct BufView<'a> {
    buf: &'a [u8],
    range: &'a mut Range<usize>,
}

impl<'a> BufferView<&'a [u8]> for BufView<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.start += n;
        Some(take_front(&mut self.buf, n))
    }

    fn take_back(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.end -= n;
        Some(take_back(&mut self.buf, n))
    }

    fn into_rest(self) -> &'a [u8] {
        self.buf
    }
}

impl<'a> AsRef<[u8]> for BufView<'a> {
    fn as_ref(&self) -> &[u8] {
        self.buf
    }
}

/// A [`BufferViewMut`] into a [`Buf`].
///
/// A `BufViewMut` is constructed by [`Buf::buffer_view_mut`], and implements
/// `BufferViewMut`, providing a mutable view into the `Buf` from which it was
/// constructed.
pub struct BufViewMut<'a> {
    buf: &'a mut [u8],
    range: &'a mut Range<usize>,
}

impl<'a> BufferView<&'a mut [u8]> for BufViewMut<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a mut [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.start += n;
        Some(take_front_mut(&mut self.buf, n))
    }

    fn take_back(&mut self, n: usize) -> Option<&'a mut [u8]> {
        if self.len() < n {
            return None;
        }
        self.range.end -= n;
        Some(take_back_mut(&mut self.buf, n))
    }

    fn into_rest(self) -> &'a mut [u8] {
        self.buf
    }
}

impl<'a> BufferViewMut<&'a mut [u8]> for BufViewMut<'a> {}

impl<'a> AsRef<[u8]> for BufViewMut<'a> {
    fn as_ref(&self) -> &[u8] {
        self.buf
    }
}

impl<'a> AsMut<[u8]> for BufViewMut<'a> {
    fn as_mut(&mut self) -> &mut [u8] {
        self.buf
    }
}

/// A builder capable of serializing packets into an existing
/// buffer, and which encapsulate other packets.
///
/// A `PacketBuilder` describes a packet, and is capable of serializing that
/// packet into an existing buffer via the `serialize` method.
///
/// Note that `PacketBuilder` does not describe an entire nested sequence of
/// packets - such as a TCP segment encapsulated in an IP packet - but instead
/// describes only a single packet - such as the IP packet in the previous
/// example. Given a buffer with a packet already serialized in its body, the
/// `PacketBuilder` is responsible for encapsulating that packet by serializing
/// its header and footer before and after the existing packet.
pub trait PacketBuilder {
    /// The number of bytes in this packet's header.
    ///
    /// # Panics
    ///
    /// Code which operates on a `PacketBuilder` may assume that `header_len() +
    /// footer_len()` will not overflow `usize`. If it does, that code is
    /// allowed to panic or produce incorrect results. That code may NOT produce
    /// memory unsafety.
    fn header_len(&self) -> usize;

    /// The minimum size of body required by this packet.
    ///
    /// # Panics
    ///
    /// Code which operates on a `PacketBuilder` may assume that `max_body_len()
    /// >= min_body_len()`. If that doesn't hold, that code is allowed to panic
    /// or produce incorrect results. That code may NOT produce memory unsafety.
    fn min_body_len(&self) -> usize;

    /// The maximum size of a body allowed by this packet.
    ///
    /// If there is no maximum body len, this returns `std::usize::MAX`.
    ///
    /// # Panics
    ///
    /// Code which operates on a `PacketBuilder` may assume that `max_body_len()
    /// >= min_body_len()`. If that doesn't hold, that code is allowed to panic
    /// or produce incorrect results. That code may NOT produce memory unsafety.
    fn max_body_len(&self) -> usize;

    /// The number of bytes in this packet's footer.
    ///
    /// # Panics
    ///
    /// Code which operates on a `PacketBuilder` may assume that `header_len() +
    /// footer_len()` will not overflow `usize`. If it does, that code is
    /// allowed to panic or produce incorrect results. That code may NOT produce
    /// memory unsafety.
    fn footer_len(&self) -> usize;

    /// Serializes this packet into an existing buffer.
    ///
    /// `serialize` is called with a [`SerializeBuffer`] which provides access
    /// to the parts of a buffer corresponding to the header, body, and footer
    /// of this packet. The header and footer bytes will be uninitialized, and
    /// will have the number of bytes specified by the `header_len` and
    /// `footer_len` methods. The body will be initialized with the contents of
    /// the packet to be encapsulated, and will have at least `min_body_len`
    /// bytes (padding may have been added in order to satisfy this minimum) and
    /// no more than `max_body_len` bytes. `serialize` is responsible for
    /// serializing the header and footer into the appropriate sections of the
    /// buffer.
    ///
    /// # Security
    ///
    /// All of the bytes of the header and footer should be initialized, even if
    /// only to zero, in order to avoid leaking the contents of packets
    /// previously stored in the same buffer.
    fn serialize(self, buffer: SerializeBuffer<'_>);
}

/// A builder capable of serializing packets into an existing buffer, and which
/// do not encapsulate other packets.
///
/// An `InnerPacketBuilder` describes a packet, and is capable of serializing
/// that packet into an existing buffer via the `serialize` method. Unlike the
/// [`PacketBuilder`] trait, it describes a packet which does not encapsulate
/// other packets.
///
/// # Notable implementations
///
/// `InnerPacketBuilder` is implemented for `&[u8]`, `&mut [u8]`, and `Vec<u8>`
/// by treating the contents of the slice/`Vec` as the contents of the packet to
/// be serialized.
pub trait InnerPacketBuilder {
    /// The number of bytes consumed by this packet.
    fn bytes_len(&self) -> usize;

    /// Serializes this packet into an existing buffer.
    ///
    /// `serialize` is called with a buffer of length `self.bytes()`, and is
    /// responsible for serializing the packet into the buffer.
    ///
    /// # Security
    ///
    /// All of the bytes of the buffer should be initialized, even if only to
    /// zero, in order to avoid leaking the contents of packets previously
    /// stored in the same buffer.
    fn serialize(self, buffer: &mut [u8]);
}

impl<'a> InnerPacketBuilder for &'a [u8] {
    fn bytes_len(&self) -> usize {
        self.len()
    }
    fn serialize(self, buffer: &mut [u8]) {
        buffer.copy_from_slice(self);
    }
}
impl<'a> InnerPacketBuilder for &'a mut [u8] {
    fn bytes_len(&self) -> usize {
        self.len()
    }
    fn serialize(self, buffer: &mut [u8]) {
        buffer.copy_from_slice(self);
    }
}
impl<'a> InnerPacketBuilder for Vec<u8> {
    fn bytes_len(&self) -> usize {
        self.len()
    }
    fn serialize(self, buffer: &mut [u8]) {
        buffer.copy_from_slice(self.as_slice());
    }
}

/// An error that could be due to an exceeded maximum transmission unit (MTU).
///
/// `MtuError<E>` is an error that can either be the error `E` or an MTU-related
/// error.
#[derive(Copy, Clone, Debug)]
pub enum MtuError<E> {
    Err(E),
    Mtu,
}

impl<E> MtuError<E> {
    /// Is this an MTU error?
    ///
    /// `is_mtu` returns true if `self` is `MtuError::Mtu`.
    pub fn is_mtu(&self) -> bool {
        match self {
            MtuError::Mtu => true,
            _ => false,
        }
    }
}

impl<E> From<E> for MtuError<E> {
    fn from(err: E) -> MtuError<E> {
        MtuError::Err(err)
    }
}

/// Constraints passed to [`Serializer::serialize`].
///
/// `SerializeConstraints` describes the prefix length, minimum body length, and
/// suffix length required of the buffer returned from a call to `serialize`.
#[derive(Copy, Clone, Debug)]
pub struct SerializeConstraints {
    pub prefix_len: usize,
    pub min_body_len: usize,
    pub suffix_len: usize,
}

pub trait Serializer: Sized {
    /// The type of buffers returned from serialization methods on this trait.
    type Buffer: BufferMut;

    // TODO(joshlf): Once the generic_associated_types feature is stabilized,
    // change this to:
    //
    // type Error where MtuError<Self::InnerError>: From<Self::Error>;

    /// The type of errors returned from the `serialize` and `serialize_outer`
    /// methods on this trait.
    type Error: Into<MtuError<Self::InnerError>>;

    /// The inner error type of `Self::Error`.
    ///
    /// If `Self::Error` is `MtuError<E>`, then `InnerError` is `E`. Otherwise,
    /// `InnerError` is equal to `Self::Error`. `serialize_mtu` and
    /// `serialize_mtu_outer` return an error type of `MtuError<InnerError>`. This
    /// allows us to return `MtuError` errors from these methods while avoiding
    /// unnecessarily nested error types like `MtuError<MtuError<MtuError<E>>>`, which
    /// is what we'd get if we naively used a single associated error type and
    /// had the MTU methods return `MtuError<Self::Error>`.
    type InnerError;

    /// Serialize this `Serializer`, producing a buffer.
    ///
    /// `serialize` accepts a set of constraints, and produces a buffer which
    /// satisfies them. In particular, the returned buffer must:
    /// - Have at least `c.prefix_len` bytes of prefix
    /// - Have at least `c.min_body_len` bytes of body, initialized to the
    ///   contents of the packet described by this `Serializer`
    /// - Have at least `c.suffix_len` bytes of suffix
    fn serialize(self, c: SerializeConstraints) -> Result<Self::Buffer, (Self::Error, Self)>;

    /// Serialize this `Serializer` as the outermost packet.
    ///
    /// `serialize_outer` is like `serialize`, except that it is called when
    /// this `Serializer` describes the outermost packet, not encapsulated in
    /// any other packets. It is equivalent to calling `serialize` with a
    /// `SerializationConstraints` of all zeros.
    fn serialize_outer(self) -> Result<Self::Buffer, (Self::Error, Self)> {
        self.serialize(SerializeConstraints { prefix_len: 0, min_body_len: 0, suffix_len: 0 })
    }

    /// Serialize this `Serializer` with a maximum transmission unit (MTU)
    /// constraint.
    ///
    /// `serialize_mtu` is like `serialize`, except that it accepts an MTU
    /// constraint. If the total length of the buffer that would be produced
    /// (including prefix and suffix length) would exceed the MTU, then an error
    /// is returned instead of a buffer.
    fn serialize_mtu(
        self,
        mtu: usize,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<Self::InnerError>, Self)>;

    /// Serialize this `Serializer` with a maximum transmission unit (MTU)
    /// constraint as the outermost packet.
    ///
    /// `serialize_mtu_outer` is like `serialize_mtu`, except that it is called
    /// when this `Serializer` describes the outermost packet, not encapsulated
    /// in any other packets. It is equivalent to calling `serialize_mtu` with a
    /// `SerializationConstraints` of all zeros.
    fn serialize_mtu_outer(
        self,
        mtu: usize,
    ) -> Result<Self::Buffer, (MtuError<Self::InnerError>, Self)> {
        self.serialize_mtu(
            mtu,
            SerializeConstraints { prefix_len: 0, min_body_len: 0, suffix_len: 0 },
        )
    }

    /// Encapsulate this `Serializer` in another packet, producing a new
    /// `Serializer`.
    ///
    /// `encapsulate` consumes this `Serializer` and a [`PacketBuilder`], and
    /// produces a new `Serializer` which describes encapsulating this one in
    /// the packet described by `builder`. Calling `serialize` on the returned
    /// `Serializer` will do the following:
    /// - Call `serialize` on this `Serializer`, producing a buffer
    /// - Serialize `builder` into the buffer as the next layer of the packet
    /// - Return the buffer
    fn encapsulate<B: PacketBuilder>(self, builder: B) -> EncapsulatingSerializer<B, Self> {
        EncapsulatingSerializer { builder, inner: self }
    }

    /// Create a new `Serializer` which will enforce a maximum transmission unit
    /// (MTU).
    ///
    /// `with_mtu` consumes this `Serializer` and an MTU, and produces a new
    /// `Serializer` which will enforce the given MTU on all serialization
    /// requests. Note that the given MTU will be enforced at this layer -
    /// serialization requests will be rejected if the body produced by the
    /// request at this layer would exceed the MTU. It has no effect on headers
    /// or footers added by encapsulating layers outside of this one.
    fn with_mtu(self, mtu: usize) -> MtuSerializer<Self> {
        MtuSerializer { mtu, inner: self }
    }
}

// TODO(joshlf): Once impl specialization is stable, make this a default impl,
// and add a specializing impl for 'B: Buffer'.

fn inner_packet_builder_serializer_total_len_body_len_body_range<B: InnerPacketBuilder>(
    builder: &B,
    c: SerializeConstraints,
) -> (usize, usize, Range<usize>) {
    let body_len = cmp::max(c.min_body_len, builder.bytes_len());
    let total_len = c.prefix_len + body_len + c.suffix_len;
    let body_range = c.prefix_len..(c.prefix_len + builder.bytes_len());
    (total_len, body_len, body_range)
}

impl<B: InnerPacketBuilder> Serializer for B {
    type Buffer = Buf<Vec<u8>>;
    type Error = Never;
    type InnerError = Never;

    fn serialize_mtu(
        self,
        mtu: usize,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<Never>, B)> {
        let (total_len, body_len, body_range) =
            inner_packet_builder_serializer_total_len_body_len_body_range(&self, c);

        if body_len <= mtu {
            let mut buf = vec![0; total_len];
            <Self as InnerPacketBuilder>::serialize(self, &mut buf[body_range.clone()]);
            Ok(Buf::new(buf, body_range))
        } else {
            Err((MtuError::Mtu, self))
        }
    }

    fn serialize(self, c: SerializeConstraints) -> Result<Buf<Vec<u8>>, (Self::Error, B)> {
        let (total_len, _, body_range) =
            inner_packet_builder_serializer_total_len_body_len_body_range(&self, c);

        let mut buf = vec![0; total_len];
        <Self as InnerPacketBuilder>::serialize(self, &mut buf[body_range.clone()]);
        Ok(Buf::new(buf, body_range))
    }
}

/// A [`Serializer`] for inner packets, wrapping an [`InnerPacketBuilder`] and a
/// buffer.
///
/// An `InnerSerializer` implements `Serializer` for an `InnerPacketBuilder`. It
/// stores a buffer for the fast path and, in case that buffer does not satisfy
/// the constraints passed to [`Serializer::serialize`], it stores a function
/// capable of producing a buffer which does.
#[derive(Copy, Clone)]
pub struct InnerSerializer<B, Buf, F> {
    builder: B,
    buf: Buf,
    get_buf: F,
}

impl<B, Buf, F> InnerSerializer<B, Buf, F> {
    /// Constructs a new `InnerSerializer`.
    ///
    /// `new` accepts an [`InnerPacketBuilder`], a buffer, and a function. When
    /// `serialize` is called, two things happen:
    /// - A buffer is produced. If the existing buffer satisfies the constraints
    ///   passed to `serialize`, it is used. Otherwise, `get_buf` is used to
    ///   produce a buffer which satisfies the constraints.
    /// - Once a buffer has been produced, the `InnerPacketBuilder` is
    ///   serialized into it, and it is returned.
    ///
    /// `get_buf` is a function which accepts a `usize` and produces a buffer of
    /// that length.
    pub fn new(builder: B, buffer: Buf, get_buf: F) -> InnerSerializer<B, Buf, F> {
        InnerSerializer { builder, buf: buffer, get_buf }
    }
}

// NOTE(joshlf): This impl block may look a bit confusing. What's happening is
// that we can't write down the type of the closure that we're passing to
// InnerSerializer::new, so we have to have new_vec return an 'impl FnOnce...'.
// However, we can't write 'impl<B, Buf> InnerSerializer<B, Buf, impl
// FnOnce...>' because that's not valid syntax. Instead, we pick a dummy
// variable for the third type parameter. Note that it doesn't have to be the
// same type as the return value from new_vec. Thus, when you write something
// like:
//
// let x = InnerSerializer::new_vec();
//
// It's equivalent to:
//
// let x: InnerSerializer<_, _, _> = InnerSerializer::<_, _, ()>::new_vec();
//
// The type on the left is different from the type on the right.

impl<B, Buf> InnerSerializer<B, Buf, ()> {
    /// Constructs a new `InnerSerializer` which allocates a new `Vec` as a
    /// fallback path.
    ///
    /// `new_vec` is like `new`, except that its `get_buf` function is
    /// automatically set to allocate a new `Vec` on the heap.
    pub fn new_vec(
        builder: B,
        buffer: Buf,
    ) -> InnerSerializer<B, Buf, impl FnOnce(usize) -> crate::Buf<Vec<u8>> + Copy + Clone> {
        InnerSerializer { builder, buf: buffer, get_buf: |n| crate::Buf::new(vec![0; n], ..) }
    }
}

impl<B, Buf, O, F> InnerSerializer<B, Buf, F>
where
    B: InnerPacketBuilder,
    Buf: BufferMut,
    O: BufferMut,
    F: FnOnce(usize) -> O,
{
    // Serialize by either using the existing `buf` if possible, and using
    // `get_buf` to allocate a new buffer otherwise. The returned buffer has a
    // prefix of at least `prefix_len`, a total length of at least `total_len`,
    // and a body length of `builder.bytes_len()`.
    fn serialize_inner(
        builder: B,
        mut buf: Buf,
        get_buf: F,
        prefix_len: usize,
        total_len: usize,
    ) -> Either<Buf, O> {
        let mut buf = if buf.capacity() >= total_len {
            buf.reset();
            Either::A(buf)
        } else {
            Either::B(get_buf(total_len))
        };
        buf.shrink(prefix_len..(prefix_len + builder.bytes_len()));
        builder.serialize(buf.as_mut());
        buf
    }
}

impl<B, Buf, O, F> Serializer for InnerSerializer<B, Buf, F>
where
    B: InnerPacketBuilder,
    Buf: BufferMut,
    O: BufferMut,
    F: FnOnce(usize) -> O,
{
    type Buffer = Either<Buf, O>;
    type Error = Never;
    type InnerError = Never;

    fn serialize_mtu(
        self,
        mtu: usize,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<Never>, Self)> {
        let InnerSerializer { builder, buf, get_buf } = self;
        let body_len = cmp::max(c.min_body_len, builder.bytes_len());
        let total_len = c.prefix_len + body_len + c.suffix_len;

        if body_len <= mtu {
            Ok(Self::serialize_inner(builder, buf, get_buf, c.prefix_len, total_len))
        } else {
            Err((MtuError::Mtu, InnerSerializer { builder, buf, get_buf }))
        }
    }

    fn serialize(self, c: SerializeConstraints) -> Result<Either<Buf, O>, (Never, Self)> {
        let InnerSerializer { builder, buf, get_buf } = self;
        let total_len = c.prefix_len + cmp::max(c.min_body_len, builder.bytes_len()) + c.suffix_len;
        Ok(Self::serialize_inner(builder, buf, get_buf, c.prefix_len, total_len))
    }
}

impl<B: Debug, Buf: Debug, F> Debug for InnerSerializer<B, Buf, F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        f.debug_struct("FnSerializer")
            .field("builder", &self.builder)
            .field("buf", &self.buf)
            .finish()
    }
}

/// A [`Serializer`] for inner packets, wrapping an `InnerPacketBuilder`.
///
/// An `FnSerializer` implements `Serializer` for an [`InnerPacketBuilder`]. It
/// stores a function which is called to produce a buffer. This function is used
/// to implement [`Serializer::serialize`].
#[derive(Copy, Clone)]
pub struct FnSerializer<B, F> {
    builder: B,
    get_buf: F,
}

impl<B, F> FnSerializer<B, F> {
    /// Constructs a new `FnSerializer`.
    ///
    /// `new` takes an [`InnerPacketBuider`] and a function, and produces a
    /// `FnSerializer`. The function accepts a `usize`, and produces a buffer of
    /// that length.
    ///
    /// When `serialize` is called, `get_buf` is called in order to produce a
    /// buffer, `builder` is serialized into the buffer, and it is returned.
    pub fn new(builder: B, get_buf: F) -> FnSerializer<B, F> {
        FnSerializer { builder, get_buf }
    }
}

// See comment on InnerSerializer for why we have this impl block.

impl<B> FnSerializer<B, ()> {
    /// Constructs a new `FnSerializer` which allocates a new `Vec`.
    ///
    /// `new_vec` is like `new`, except that its `get_buf` function is
    /// automatically set to allocate a new `Vec` on the heap.
    pub fn new_vec(
        builder: B,
    ) -> FnSerializer<B, impl FnOnce(usize) -> Buf<Vec<u8>> + Copy + Clone> {
        FnSerializer::new(builder, |n| Buf::new(vec![0; n], ..))
    }
}

impl<B, O, F> Serializer for FnSerializer<B, F>
where
    B: InnerPacketBuilder,
    O: BufferMut,
    F: FnOnce(usize) -> O,
{
    type Buffer = O;
    type Error = Never;
    type InnerError = Never;

    fn serialize_mtu(
        self,
        mtu: usize,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<Never>, Self)> {
        let FnSerializer { builder, get_buf } = self;
        let body_len = cmp::max(c.min_body_len, builder.bytes_len());
        let total_len = c.prefix_len + body_len + c.suffix_len;

        if body_len <= mtu {
            let mut buf = get_buf(total_len);
            buf.shrink(c.prefix_len..(c.prefix_len + builder.bytes_len()));
            builder.serialize(buf.as_mut());
            Ok(buf)
        } else {
            Err((MtuError::Mtu, FnSerializer { builder, get_buf }))
        }
    }

    fn serialize(self, c: SerializeConstraints) -> Result<O, (Never, Self)> {
        let FnSerializer { builder, get_buf } = self;
        let total_len = c.prefix_len + cmp::max(c.min_body_len, builder.bytes_len()) + c.suffix_len;

        let mut buf = get_buf(total_len);
        buf.shrink(c.prefix_len..(c.prefix_len + builder.bytes_len()));
        builder.serialize(buf.as_mut());
        Ok(buf)
    }
}

impl<B: Debug, F> Debug for FnSerializer<B, F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        f.debug_struct("FnSerializer").field("builder", &self.builder).finish()
    }
}

/// The direction a buffer's body should be truncated from to force
/// it to fit within a MTU.
#[derive(Copy, Clone)]
pub enum TruncateDirection {
    /// If a buffer cannot fit within an MTU, discard bytes from the
    /// front of the body.
    DiscardFront,
    /// If a buffer cannot fit within an MTU, discard bytes from the
    /// end of the body.
    DiscardBack,
    /// Do not attempt to truncate a buffer to make it fit within an MTU.
    NoTruncating,
}

/// A [`Serializer`] which returns a packet already serialized into an existing
/// buffer.
///
/// A `BufferSerializer` wraps an existing buffer, and implements the
/// `Serializer` trait, treating the body of the buffer as the packet to be
/// serialized. In the fast path, when the buffer already satisfies the
/// constraints passed to [`Serializer::serialize`], the buffer itself is
/// returned. In the slow path, a function is used to produce a buffer which
/// satisfies the constraints, and the body is copied from the original buffer
/// to the new one.
///
/// `BufferSerializer`s are useful as performance optimization in order to avoid
/// unnecessary allocation when a buffer already exists during serialization.
/// This can happen, for example, when a buffer is used to store an incoming
/// packet, and then that buffer can be reused to serialize an outgoing packet
/// sent in response.
#[derive(Copy, Clone)]
pub struct BufferSerializer<B, F> {
    buf: B,
    get_buf: F,
    direction: TruncateDirection,
}

impl<B, F> BufferSerializer<B, F> {
    /// Constructs a new `BufferSerializer`.
    ///
    /// `new` is equivalent to `new_truncate` with
    /// `TruncateDirection::NoTruncating` given as the `direction` argument.
    pub fn new(buffer: B, get_buf: F) -> BufferSerializer<B, F> {
        BufferSerializer { buf: buffer, get_buf, direction: TruncateDirection::NoTruncating }
    }

    /// Constructs a new `BufferSerializer` that may truncate a buffer.
    ///
    /// `new_truncate` accepts a buffer and a `get_buf` function, which produces
    /// a buffer. When `serialize` is called, if the existing buffer satisfies
    /// the constraints, it is returned. Otherwise, `get_buf` is used to
    /// produce a buffer which satisfies the constraints, and the body is
    /// copied from the original buffer into the new one before it is
    /// returned. `direction` configures the `BufferSerializer`'s buffer
    /// truncating options when it will not fit within an MTU when serializing
    /// (see [`BufferSerializer::serialize_mtu`]).
    ///
    /// `get_buf` accepts a `usize`, and produces a buffer of that length.
    pub fn new_truncate(
        buffer: B,
        get_buf: F,
        direction: TruncateDirection,
    ) -> BufferSerializer<B, F> {
        BufferSerializer { buf: buffer, get_buf, direction }
    }

    /// Consume this `BufferSerializer` and return the encapsulated buffer.
    pub fn into_buffer(self) -> B {
        self.buf
    }
}

// See comment on InnerSerializer for why we have this impl block.

impl<B> BufferSerializer<B, ()> {
    /// Constructs a new `BufferSerializer` which allocates a new `Vec` in the
    /// fallback path.
    ///
    /// `new_vec` is like `new_vec_truncate` with
    /// `TruncateDirection::NoTruncating` given as the `direction` argument.
    pub fn new_vec(
        buffer: B,
    ) -> BufferSerializer<B, impl FnOnce(usize) -> Buf<Vec<u8>> + Copy + Clone> {
        BufferSerializer::new(buffer, |n| Buf::new(vec![0; n], ..))
    }

    /// Constructs a new `BufferSerializer` which allocates a new `Vec` in the
    /// fallback path and may truncate a buffer.
    ///
    /// `new_vec_truncate` is like [`BufferSerializer::new_truncate`] except that
    /// its `get_buf` function is automatically set to allocate a new `Vec`
    /// on the heap.
    pub fn new_vec_truncate(
        buffer: B,
        direction: TruncateDirection,
    ) -> BufferSerializer<B, impl FnOnce(usize) -> Buf<Vec<u8>> + Copy + Clone> {
        BufferSerializer::new_truncate(buffer, |n| Buf::new(vec![0; n], ..), direction)
    }
}

impl<B: BufferMut, O: BufferMut, F: FnOnce(usize) -> O> BufferSerializer<B, F> {
    fn serialize_inner(
        buf: B,
        get_buf: F,
        c: SerializeConstraints,
        body_and_padding: usize,
        total_len: usize,
    ) -> Either<B, O> {
        if buf.prefix_len() >= c.prefix_len
            && buf.len() + buf.suffix_len() >= body_and_padding + c.suffix_len
        {
            // The buffer satisfies the requirements, so there's no work to do.
            Either::A(buf)
        // } else if buf.cap() >= total_len {
        //     // The buffer is large enough, but the body is currently too far
        //     // forward or too far backwards to satisfy the prefix or suffix
        //     // requirements, so we need to move the body within the buffer.
        //     unimplemented!()
        } else {
            // The buffer is too small, so we need to allocate a new one.
            let mut new_buf = get_buf(total_len);
            new_buf.shrink(c.prefix_len..(c.prefix_len + buf.len()));
            new_buf.as_mut().copy_from_slice(buf.as_ref());
            Either::B(new_buf)
        }
    }
}

impl<B: Debug, F> Debug for BufferSerializer<B, F> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        f.debug_struct("BufferSerializer").field("buf", &self.buf).finish()
    }
}

impl<B: BufferMut, O: BufferMut, F: FnOnce(usize) -> O> Serializer for BufferSerializer<B, F> {
    type Buffer = Either<B, O>;
    type Error = Never;
    type InnerError = Never;

    /// Attempt to serialize a buffer within some MTU constraint, `mtu`. If the buffer as
    /// is will not fit within the `mtu`, the buffer may be truncated from either the
    /// front, the back, or not at all, depending on the value of `BufferSerializer`'s
    /// `direction` value.
    fn serialize_mtu(
        self,
        mtu: usize,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<Never>, Self)> {
        // Make sure that `mtu` is big enough to fit the minimum
        // body len provided by `c.min_body_len`.
        if c.min_body_len > mtu {
            return Err((MtuError::Mtu, self));
        }

        let BufferSerializer { mut buf, get_buf, direction } = self;

        // Check if `buf` wont fit in the MTU.
        if let Some(excess_bytes) = buf.len().checked_sub(usize::from(mtu)) {
            if excess_bytes > 0 {
                // Body does not fit in MTU.
                //
                // Attempt to truncate if configured to do so.
                match direction {
                    TruncateDirection::NoTruncating => {
                        return Err((MtuError::Mtu, BufferSerializer { buf, get_buf, direction }))
                    }
                    TruncateDirection::DiscardFront => buf.shrink_front(excess_bytes),
                    TruncateDirection::DiscardBack => buf.shrink_back(excess_bytes),
                };
            }
        }

        let body_and_padding = cmp::max(c.min_body_len, buf.len());

        // At this point, the body and padding MUST fit within
        // `mtu`.
        assert!(body_and_padding <= mtu);

        let total_len = c.prefix_len + body_and_padding + c.suffix_len;

        Ok(Self::serialize_inner(buf, get_buf, c, body_and_padding, total_len))
    }

    fn serialize(self, c: SerializeConstraints) -> Result<Either<B, O>, (Never, Self)> {
        let BufferSerializer { buf, get_buf, direction: _ } = self;
        let body_and_padding = cmp::max(c.min_body_len, buf.len());
        let total_len = c.prefix_len + body_and_padding + c.suffix_len;
        Ok(Self::serialize_inner(buf, get_buf, c, body_and_padding, total_len))
    }
}

/// A [`Serializer`] which adds a maximum transmission unit (MTU) constraint to
/// an existing `Serializer`.
///
/// `MtuSerializer`s are produced by the [`Serializer::with_mtu`] method. See
/// its documentation for more details.
#[derive(Copy, Clone, Debug)]
pub struct MtuSerializer<S: Serializer> {
    mtu: usize,
    inner: S,
}

impl<S: Serializer> MtuSerializer<S> {
    /// Consume this `MtuSerializer` and return the MTU and encapsulated
    /// `Serializer` separately.
    pub fn into_mtu_serializer(self) -> (usize, S) {
        let MtuSerializer { mtu, inner } = self;
        (mtu, inner)
    }

    /// Consume this `MtuSerializer` and return the encapsulated `Serializer`.
    pub fn into_serializer(self) -> S {
        let MtuSerializer { inner, .. } = self;
        inner
    }
}

impl<S: Serializer> Serializer for MtuSerializer<S> {
    type Buffer = S::Buffer;
    type Error = MtuError<S::InnerError>;
    type InnerError = S::InnerError;

    fn serialize_mtu(
        self,
        mtu: usize,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<S::InnerError>, Self)> {
        let self_mtu = self.mtu;
        self.inner
            .serialize_mtu(cmp::min(mtu, self_mtu), c)
            .map_err(|(err, inner)| (err, MtuSerializer { mtu: self_mtu, inner }))
    }

    fn serialize(
        self,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<S::InnerError>, Self)> {
        self.serialize_mtu(std::usize::MAX, c)
    }
}

/// A [`Serializer`] which encapsulates another `Serializer` in a new packet
/// layer described by a [`PacketBuilder`].
///
/// An `EncapsulatingSerializer` takes a `Serializer` - which describes a
/// complete packet - and a `PacketBuilder` - which describes a new layer of a
/// packet - and produces a `Serializer` which describes the encapsulation of
/// the former in the latter.
///
/// `EncapsulatingSerializer`s are produced by the [`Serializer::encapsulate`]
/// method.
#[derive(Copy, Clone, Debug)]
pub struct EncapsulatingSerializer<B: PacketBuilder, S: Serializer> {
    builder: B,
    inner: S,
}

impl<B: PacketBuilder, S: Serializer> EncapsulatingSerializer<B, S> {
    /// Consume this `EncapsulatingSerializer` and return the builder and
    /// encapsulated `Serializer` separately.
    pub fn into_builder_serializer(self) -> (B, S) {
        let EncapsulatingSerializer { builder, inner } = self;
        (builder, inner)
    }

    /// Consume this `EncapsulatingSerializer` and return the encapsulated
    /// `Serializer`.
    pub fn into_serializer(self) -> S {
        let EncapsulatingSerializer { inner, .. } = self;
        inner
    }

    // The minimum body and MTU required of the encapsulated serializer. Returns
    // None if the MTU is exeeded by this layer's header and footer.
    fn min_body_mtu(&self, min_body: usize, mtu: usize) -> Option<(usize, usize)> {
        // The number of bytes consumed by the header and footer of this layer.
        //
        // Note that the implementer of PacketBuilder promises that this
        // addition will not overflow. In debug mode, an overflow here panics,
        // which is consistent with them violating that contract. In release
        // mode, an overflow here will likely produce incorrect buffer results,
        // and that's also consistent with them violating that contract. In
        // neither case is it a safety concern.
        let header_footer_total = self.builder.header_len() + self.builder.footer_len();
        // The number required by this layer.
        let this_min_body = self.builder.min_body_len();
        // The number required by the next outer layer, taking into account the
        // number of header and footer bytes consumed by this layer.
        let next_min_body = min_body.saturating_sub(header_footer_total);
        let min_body = cmp::max(this_min_body, next_min_body);

        // The MTU required by this layer, taking into account the number of
        // header and footer bytes consumed by this layer.
        let this_mtu = self.builder.max_body_len();
        // The MTU required by the next layer, taking into account the number of
        // header and footer bytes consumed by this layer.
        let next_mtu = mtu.checked_sub(header_footer_total)?;
        let mtu = cmp::min(next_mtu, this_mtu);

        Some((min_body, mtu))
    }
}

impl<B: PacketBuilder, S: Serializer> Serializer for EncapsulatingSerializer<B, S> {
    type Buffer = S::Buffer;
    type Error = MtuError<S::InnerError>;
    type InnerError = S::InnerError;

    fn serialize_mtu(
        self,
        mtu: usize,
        mut c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<S::InnerError>, Self)> {
        c.prefix_len += self.builder.header_len();
        c.suffix_len += self.builder.footer_len();
        let (min_body_len, mtu) = if let Some(min_body_mtu) = self.min_body_mtu(c.min_body_len, mtu)
        {
            min_body_mtu
        } else {
            return Err((MtuError::Mtu, self));
        };
        c.min_body_len = min_body_len;

        let EncapsulatingSerializer { builder, inner } = self;
        match inner.serialize_mtu(mtu, c) {
            Ok(mut buffer) => {
                buffer.serialize(builder);
                Ok(buffer)
            }
            Err((err, inner)) => Err((err, EncapsulatingSerializer { builder, inner })),
        }
    }

    fn serialize(
        self,
        c: SerializeConstraints,
    ) -> Result<Self::Buffer, (MtuError<S::InnerError>, Self)> {
        self.serialize_mtu(std::usize::MAX, c)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // DummyPacketBuilder:
    // - Implements PacketBuilder, consuming a 1-byte header and a 1-byte
    //   footer with no minimum or maximum body length requirements
    // - Implements InnerPacketBuilder, producing a 1-byte packet
    #[derive(Clone)]
    struct DummyPacketBuilder;

    impl PacketBuilder for DummyPacketBuilder {
        fn header_len(&self) -> usize {
            1
        }
        fn footer_len(&self) -> usize {
            1
        }
        fn min_body_len(&self) -> usize {
            0
        }
        fn max_body_len(&self) -> usize {
            std::usize::MAX
        }

        fn serialize(self, _buffer: SerializeBuffer<'_>) {}
    }

    impl InnerPacketBuilder for DummyPacketBuilder {
        fn bytes_len(&self) -> usize {
            1
        }

        fn serialize(self, _buffer: &mut [u8]) {}
    }

    #[test]
    fn test_either_into_inner() {
        fn ret_either(a: u32, b: u32, c: bool) -> Either<u32, u32> {
            if c {
                Either::A(a)
            } else {
                Either::B(b)
            }
        }

        assert_eq!(ret_either(1, 2, true).into_inner(), 1);
        assert_eq!(ret_either(1, 2, false).into_inner(), 2);
    }

    #[test]
    fn test_buffer_serializer_and_inner_serializer() {
        fn verify_buffer_serializer<B: BufferMut + Debug>(
            buffer: B,
            prefix_len: usize,
            suffix_len: usize,
            min_body_len: usize,
        ) {
            let old_len = buffer.len();
            let mut old_body = Vec::with_capacity(old_len);
            old_body.extend_from_slice(buffer.as_ref());

            let buffer = BufferSerializer::new_vec(buffer)
                .serialize(SerializeConstraints { prefix_len, suffix_len, min_body_len })
                .unwrap();
            verify(buffer, &old_body, prefix_len, suffix_len, min_body_len);
        }

        fn verify_inner_serializer(
            body: &[u8],
            buf_len: usize,
            prefix_len: usize,
            suffix_len: usize,
            min_body_len: usize,
        ) {
            let buffer = InnerSerializer::new_vec(body, Buf::new(vec![0; buf_len], ..))
                .serialize(SerializeConstraints { prefix_len, suffix_len, min_body_len })
                .unwrap();
            verify(buffer, body, prefix_len, suffix_len, min_body_len);
        }

        fn verify<B: Buffer>(
            buffer: B,
            body: &[u8],
            prefix_len: usize,
            suffix_len: usize,
            min_body_len: usize,
        ) {
            assert_eq!(buffer.as_ref(), body);
            assert!(buffer.prefix_len() >= prefix_len);
            assert!(buffer.suffix_len() >= suffix_len);
            assert!(buffer.len() + buffer.suffix_len() >= (min_body_len + suffix_len));
        }
        // Test for every valid combination of buf_len, range_start, range_end,
        // prefix, suffix, and min_body within [0, 8).
        for buf_len in 0..8 {
            for range_start in 0..buf_len {
                for range_end in range_start..buf_len {
                    for prefix in 0..8 {
                        for suffix in 0..8 {
                            for min_body in 0..8 {
                                let mut vec = vec![0; buf_len];
                                // Initialize the vector with values 0, 1, 2,
                                // ... so that we can check to make sure that
                                // the range bytes have been properly copied if
                                // the buffer is reallocated.
                                #[allow(clippy::needless_range_loop)]
                                for i in 0..vec.len() {
                                    vec[i] = i as u8;
                                }
                                verify_buffer_serializer(
                                    Buf::new(vec.as_mut_slice(), range_start..range_end),
                                    prefix,
                                    suffix,
                                    min_body,
                                );
                                verify_inner_serializer(
                                    &vec.as_slice()[range_start..range_end],
                                    buf_len,
                                    prefix,
                                    suffix,
                                    min_body,
                                );
                            }
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn test_mtu() {
        // ser is a Serializer that will consume 1 byte of buffer space
        fn test<S: Serializer + Clone>(ser: S) {
            // Each of these tests encapsulates ser in a DummyPacketBuilder.
            // Thus, the inner serializer will consume 1 byte, while the
            // DummyPacketBuilder will consume 2 bytes, for a total of 3 bytes.

            // Test that an MTU of 3 is OK.
            assert!(ser.clone().encapsulate(DummyPacketBuilder).serialize_mtu_outer(3).is_ok());
            // Test that the inner MTU of 1 only applies to the inner
            // serializer, and so is still OK even though the outer serializer
            // consumes 3 bytes total.
            assert!(ser
                .clone()
                .with_mtu(1)
                .encapsulate(DummyPacketBuilder)
                .serialize_mtu_outer(3)
                .is_ok());
            // Test that the inner MTU of 0 is exceeded by the inner
            // serializer's 1 byte length.
            assert!(ser
                .clone()
                .with_mtu(0)
                .encapsulate(DummyPacketBuilder)
                .serialize_outer()
                .is_err());
            // Test that an MTU which would be exceeded by the encapsulating
            // layer is rejected by EncapsulatingSerializer's implementation. If
            // this doesn't work properly, then the MTU should underflow,
            // resulting in a panic (see the EncapsulatingSerializer
            // implementation of Serialize).
            assert!(ser.clone().encapsulate(DummyPacketBuilder).serialize_mtu_outer(1).is_err());
        }

        test(DummyPacketBuilder);
        test(InnerSerializer::new_vec(DummyPacketBuilder, Buf::new(vec![], ..)));
        test(FnSerializer::new_vec(DummyPacketBuilder));
        test(BufferSerializer::new_vec(Buf::new(vec![0], ..)));
    }

    #[test]
    fn test_truncating_buffer_serializer() {
        //
        // Test truncate front.
        //

        let body = vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        let ser = BufferSerializer::new_vec_truncate(
            Buf::new(body.clone(), ..),
            TruncateDirection::DiscardFront,
        );
        let buf = ser.serialize_mtu_outer(4).unwrap();
        let buf: &[u8] = buf.as_ref();
        assert_eq!(buf, &[6, 7, 8, 9][..]);

        //
        // Test truncate back.
        //

        let ser = BufferSerializer::new_vec_truncate(
            Buf::new(body.clone(), ..),
            TruncateDirection::DiscardBack,
        );
        let buf = ser.serialize_mtu_outer(7).unwrap();
        let buf: &[u8] = buf.as_ref();
        assert_eq!(buf, &[0, 1, 2, 3, 4, 5, 6][..]);

        //
        // Test no truncating (default/original case).
        //

        let ser = BufferSerializer::new_vec_truncate(
            Buf::new(body.clone(), ..),
            TruncateDirection::NoTruncating,
        );
        assert!(ser.serialize_mtu_outer(5).is_err());
        let ser = BufferSerializer::new_vec(Buf::new(body.clone(), ..));
        assert!(ser.serialize_mtu_outer(5).is_err());
    }
}
