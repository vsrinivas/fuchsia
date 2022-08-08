// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Serialization.

use std::cmp;
use std::convert::Infallible as Never;
use std::fmt::{self, Debug, Formatter};
use std::ops::{Range, RangeBounds};

use arrayvec::ArrayVec;
use zerocopy::ByteSlice;

use crate::{
    canonicalize_range, take_back, take_back_mut, take_front, take_front_mut,
    AsFragmentedByteSlice, BufferMut, BufferView, BufferViewMut, ContiguousBuffer,
    ContiguousBufferImpl, ContiguousBufferMut, ContiguousBufferMutImpl, EmptyBuf, FragmentedBuffer,
    FragmentedBufferMut, FragmentedBytes, FragmentedBytesMut, GrowBuffer, GrowBufferMut,
    ParsablePacket, ParseBuffer, ParseBufferMut, ReusableBuffer, SerializeBuffer, ShrinkBuffer,
    TargetBuffer,
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

impl<A, B> Either<A, B> {
    /// Maps the `A` variant of an `Either`.
    ///
    /// Given an `Either<A, B>` and a function from `A` to `AA`, `map_a`
    /// produces an `Either<AA, B>` by applying the function to the `A` variant
    /// or passing on the `B` variant unmodified.
    pub fn map_a<AA, F: FnOnce(A) -> AA>(self, f: F) -> Either<AA, B> {
        match self {
            Either::A(a) => Either::A(f(a)),
            Either::B(b) => Either::B(b),
        }
    }

    /// Maps the `B` variant of an `Either`.
    ///
    /// Given an `Either<A, B>` and a function from `B` to `BB`, `map_b`
    /// produces an `Either<A, BB>` by applying the function to the `B` variant
    /// or passing on the `A` variant unmodified.
    pub fn map_b<BB, F: FnOnce(B) -> BB>(self, f: F) -> Either<A, BB> {
        match self {
            Either::A(a) => Either::A(a),
            Either::B(b) => Either::B(f(b)),
        }
    }

    /// Returns the `A` variant in an `Either<A, B>`.
    ///
    /// # Panics
    ///
    /// Panics if this `Either<A, B>` does not hold the `A` variant.
    pub fn unwrap_a(self) -> A {
        match self {
            Either::A(x) => x,
            Either::B(_) => panic!("This `Either<A, B>` does not hold the `A` variant"),
        }
    }

    /// Returns the `B` variant in an `Either<A, B>`.
    ///
    /// # Panics
    ///
    /// Panics if this `Either<A, B>` does not hold the `B` variant.
    pub fn unwrap_b(self) -> B {
        match self {
            Either::A(_) => panic!("This `Either<A, B>` does not hold the `B` variant"),
            Either::B(x) => x,
        }
    }
}

impl<A> Either<A, A> {
    /// Returns the inner value held by this `Either` when both possible values
    /// `Either::A` and `Either::B` contain the same inner types.
    pub fn into_inner(self) -> A {
        match self {
            Either::A(x) => x,
            Either::B(x) => x,
        }
    }
}

impl<A> Either<A, Never> {
    /// Returns the `A` value in an `Either<A, Never>`.
    #[inline]
    pub fn into_a(self) -> A {
        match self {
            Either::A(a) => a,
            Either::B(never) => match never {},
        }
    }
}

impl<B> Either<Never, B> {
    /// Returns the `B` value in an `Either<Never, B>`.
    #[inline]
    pub fn into_b(self) -> B {
        match self {
            Either::A(never) => match never {},
            Either::B(b) => b,
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

impl<A, B> FragmentedBuffer for Either<A, B>
where
    A: FragmentedBuffer,
    B: FragmentedBuffer,
{
    fn len(&self) -> usize {
        call_method_on_either!(self, len)
    }

    fn with_bytes<R, F>(&self, f: F) -> R
    where
        F: for<'a, 'b> FnOnce(FragmentedBytes<'a, 'b>) -> R,
    {
        call_method_on_either!(self, with_bytes, f)
    }
}

impl<A, B> ContiguousBuffer for Either<A, B>
where
    A: ContiguousBuffer,
    B: ContiguousBuffer,
{
}

impl<A, B> ContiguousBufferMut for Either<A, B>
where
    A: ContiguousBufferMut,
    B: ContiguousBufferMut,
{
}

impl<A, B> ShrinkBuffer for Either<A, B>
where
    A: ShrinkBuffer,
    B: ShrinkBuffer,
{
    fn shrink<R: RangeBounds<usize>>(&mut self, range: R) {
        call_method_on_either!(self, shrink, range)
    }
    fn shrink_front(&mut self, n: usize) {
        call_method_on_either!(self, shrink_front, n)
    }
    fn shrink_back(&mut self, n: usize) {
        call_method_on_either!(self, shrink_back, n)
    }
}

impl<A, B> ParseBuffer for Either<A, B>
where
    A: ParseBuffer,
    B: ParseBuffer,
{
    fn parse<'a, P: ParsablePacket<&'a [u8], ()>>(&'a mut self) -> Result<P, P::Error> {
        call_method_on_either!(self, parse)
    }
    fn parse_with<'a, ParseArgs, P: ParsablePacket<&'a [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        call_method_on_either!(self, parse_with, args)
    }
}

impl<A, B> FragmentedBufferMut for Either<A, B>
where
    A: FragmentedBufferMut,
    B: FragmentedBufferMut,
{
    fn with_bytes_mut<R, F>(&mut self, f: F) -> R
    where
        F: for<'a, 'b> FnOnce(FragmentedBytesMut<'a, 'b>) -> R,
    {
        call_method_on_either!(self, with_bytes_mut, f)
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
}

impl<A, B> GrowBuffer for Either<A, B>
where
    A: GrowBuffer,
    B: GrowBuffer,
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

impl<A, B> TargetBuffer for Either<A, B>
where
    A: TargetBuffer,
    B: TargetBuffer,
{
    fn with_parts<O, F>(&mut self, f: F) -> O
    where
        F: for<'a, 'b> FnOnce(&'a mut [u8], FragmentedBytesMut<'a, 'b>, &'a mut [u8]) -> O,
    {
        call_method_on_either!(self, with_parts, f)
    }

    fn serialize<BB: PacketBuilder>(&mut self, c: PacketConstraints, builder: BB) {
        call_method_on_either!(self, serialize, c, builder)
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

/// A byte slice wrapper providing buffer functionality.
///
/// A `Buf` wraps a byte slice (a type which implements `AsRef<[u8]>` or
/// `AsMut<[u8]>`) and implements various buffer traits by keeping track of
/// prefix, body, and suffix offsets within the byte slice.
#[derive(Clone, Debug)]
pub struct Buf<B> {
    buf: B,
    body: Range<usize>,
}

impl<B: AsRef<[u8]>> PartialEq for Buf<B> {
    fn eq(&self, other: &Self) -> bool {
        let self_slice = AsRef::<[u8]>::as_ref(self);
        let other_slice = AsRef::<[u8]>::as_ref(other);
        PartialEq::eq(self_slice, other_slice)
    }
}

impl<B: AsRef<[u8]>> Eq for Buf<B> {}

impl Buf<Vec<u8>> {
    /// Extracts the contained data trimmed to the buffer's range.
    pub fn into_inner(self) -> Vec<u8> {
        let Buf { mut buf, body } = self;
        let len = body.end - body.start;
        let _ = buf.drain(..body.start);
        buf.truncate(len);
        buf
    }
}

impl<B: AsRef<[u8]>> Buf<B> {
    /// Constructs a new `Buf`.
    ///
    /// `new` constructs a new `Buf` from a buffer and a body range. The bytes
    /// within the range will be the body, the bytes before the range will be
    /// the prefix, and the bytes after the range will be the suffix.
    ///
    /// # Panics
    ///
    /// Panics if `range` is out of bounds of `buf`, or if it is nonsensical
    /// (the end precedes the start).
    pub fn new<R: RangeBounds<usize>>(buf: B, body: R) -> Buf<B> {
        let len = buf.as_ref().len();
        Buf { buf, body: canonicalize_range(len, &body) }
    }

    /// Constructs a [`BufView`] which will be a [`BufferView`] into this `Buf`.
    pub fn buffer_view(&mut self) -> BufView<'_> {
        BufView { buf: &self.buf.as_ref()[self.body.clone()], body: &mut self.body }
    }
}

impl<B: AsRef<[u8]> + AsMut<[u8]>> Buf<B> {
    /// Constructs a [`BufViewMut`] which will be a [`BufferViewMut`] into this `Buf`.
    pub fn buffer_view_mut(&mut self) -> BufViewMut<'_> {
        BufViewMut { buf: &mut self.buf.as_mut()[self.body.clone()], body: &mut self.body }
    }
}

impl<B: AsRef<[u8]>> ContiguousBufferImpl for Buf<B> {}
impl<B: AsRef<[u8]>> ShrinkBuffer for Buf<B> {
    fn shrink<R: RangeBounds<usize>>(&mut self, range: R) {
        let len = self.len();
        let mut range = canonicalize_range(len, &range);
        range.start += self.body.start;
        range.end += self.body.start;
        self.body = range;
    }

    fn shrink_front(&mut self, n: usize) {
        assert!(n <= self.len());
        self.body.start += n;
    }
    fn shrink_back(&mut self, n: usize) {
        assert!(n <= self.len());
        self.body.end -= n;
    }
}
impl<B: AsRef<[u8]>> ParseBuffer for Buf<B> {
    fn parse_with<'a, ParseArgs, P: ParsablePacket<&'a [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse(self.buffer_view(), args)
    }
}

impl<B: AsRef<[u8]> + AsMut<[u8]>> ParseBufferMut for Buf<B> {
    fn parse_with_mut<'a, ParseArgs, P: ParsablePacket<&'a mut [u8], ParseArgs>>(
        &'a mut self,
        args: ParseArgs,
    ) -> Result<P, P::Error> {
        P::parse_mut(self.buffer_view_mut(), args)
    }
}

impl<B: AsRef<[u8]>> GrowBuffer for Buf<B> {
    fn capacity(&self) -> usize {
        self.buf.as_ref().len()
    }
    fn prefix_len(&self) -> usize {
        self.body.start
    }
    fn suffix_len(&self) -> usize {
        self.buf.as_ref().len() - self.body.end
    }
    fn grow_front(&mut self, n: usize) {
        assert!(n <= self.body.start);
        self.body.start -= n;
    }
    fn grow_back(&mut self, n: usize) {
        assert!(n <= self.buf.as_ref().len() - self.body.end);
        self.body.end += n;
    }
}
impl<B: AsRef<[u8]> + AsMut<[u8]>> ContiguousBufferMutImpl for Buf<B> {}
impl<B: AsRef<[u8]> + AsMut<[u8]>> TargetBuffer for Buf<B> {
    fn with_parts<O, F>(&mut self, f: F) -> O
    where
        F: for<'a, 'b> FnOnce(&'a mut [u8], FragmentedBytesMut<'a, 'b>, &'a mut [u8]) -> O,
    {
        let (prefix, buf) = self.buf.as_mut().split_at_mut(self.body.start);
        let (body, suffix) = buf.split_at_mut(self.body.end - self.body.start);
        let mut body = [&mut body[..]];
        f(prefix, body.as_fragmented_byte_slice(), suffix)
    }
}

impl<B: AsRef<[u8]>> AsRef<[u8]> for Buf<B> {
    fn as_ref(&self) -> &[u8] {
        &self.buf.as_ref()[self.body.clone()]
    }
}

impl<B: AsMut<[u8]>> AsMut<[u8]> for Buf<B> {
    fn as_mut(&mut self) -> &mut [u8] {
        &mut self.buf.as_mut()[self.body.clone()]
    }
}

/// A [`BufferView`] into a [`Buf`].
///
/// A `BufView` is constructed by [`Buf::buffer_view`], and implements
/// `BufferView`, providing a view into the `Buf` from which it was constructed.
pub struct BufView<'a> {
    buf: &'a [u8],
    body: &'a mut Range<usize>,
}

impl<'a> BufferView<&'a [u8]> for BufView<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.len() < n {
            return None;
        }
        self.body.start += n;
        Some(take_front(&mut self.buf, n))
    }

    fn take_back(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.len() < n {
            return None;
        }
        self.body.end -= n;
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
    body: &'a mut Range<usize>,
}

impl<'a> BufferView<&'a mut [u8]> for BufViewMut<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a mut [u8]> {
        if self.len() < n {
            return None;
        }
        self.body.start += n;
        Some(take_front_mut(&mut self.buf, n))
    }

    fn take_back(&mut self, n: usize) -> Option<&'a mut [u8]> {
        if self.len() < n {
            return None;
        }
        self.body.end -= n;
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

/// The constraints required by a [`PacketBuilder`].
///
/// `PacketConstraints` represents the constraints that must be satisfied in
/// order to serialize a `PacketBuilder`.
///
/// A `PacketConstraints`, `c`, guarantees two properties:
/// - `c.max_body_len() >= c.min_body_len()`
/// - `c.header_len() + c.min_body_len() + c.footer_len()` does not overflow
///   `usize`
///
/// It is not possible (using safe code) to obtain a `PacketConstraints` which
/// violates these properties, so code may rely for its correctness on the
/// assumption that these properties hold.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct PacketConstraints {
    header_len: usize,
    footer_len: usize,
    min_body_len: usize,
    max_body_len: usize,
}

impl PacketConstraints {
    /// Constructs a new `PacketConstraints`.
    ///
    /// # Panics
    ///
    /// `new` panics if the arguments violate the validity properties of
    /// `PacketConstraints` - if `max_body_len < min_body_len`, or if
    /// `header_len + min_body_len + footer_len` overflows `usize`.
    #[inline]
    pub fn new(
        header_len: usize,
        footer_len: usize,
        min_body_len: usize,
        max_body_len: usize,
    ) -> PacketConstraints {
        PacketConstraints::try_new(header_len, footer_len, min_body_len, max_body_len).expect(
            "max_body_len < min_body_len or header_len + min_body_len + footer_len overflows usize",
        )
    }

    /// Tries to construct a new `PacketConstraints`.
    ///
    /// `new` returns `None` if the provided values violate the validity
    /// properties of `PacketConstraints` - if `max_body_len < min_body_len`, or
    /// if `header_len + min_body_len + footer_len` overflows `usize`.
    #[inline]
    pub fn try_new(
        header_len: usize,
        footer_len: usize,
        min_body_len: usize,
        max_body_len: usize,
    ) -> Option<PacketConstraints> {
        // Test case 3 in test_nested_packet_builder
        let header_min_body_footer_overflows = header_len
            .checked_add(min_body_len)
            .and_then(|sum| sum.checked_add(footer_len))
            .is_none();
        // Test case 5 in test_nested_packet_builder
        let max_less_than_min = max_body_len < min_body_len;
        if max_less_than_min || header_min_body_footer_overflows {
            return None;
        }
        Some(PacketConstraints { header_len, footer_len, min_body_len, max_body_len })
    }

    /// The number of bytes in this packet's header.
    #[inline]
    pub fn header_len(&self) -> usize {
        self.header_len
    }

    /// The number of bytes in this packet's footer.
    #[inline]
    pub fn footer_len(&self) -> usize {
        self.footer_len
    }

    /// The minimum body length (in bytes) required by this packet in order to
    /// avoid adding padding.
    ///
    /// `min_body_len` returns the minimum number of body bytes required in
    /// order to avoid adding padding. Note that, if padding bytes are required,
    /// they may not necessarily belong immediately following the body,
    /// depending on which packet layer imposes the minimum. In particular, in a
    /// nested packet, padding goes after the body of the layer which imposes
    /// the minimum. This means that, if the layer that imposes the minimum is
    /// not the innermost one, then padding must be added not after the
    /// innermost body, but instead in between footers.
    /// [`NestedPacketBuilder::serialize_into`] is responsible for inserting
    /// padding when serializing nested packets.
    ///
    /// If there is no minimum body length, this returns 0.
    #[inline]
    pub fn min_body_len(&self) -> usize {
        self.min_body_len
    }

    /// The maximum length (in bytes) of a body allowed by this packet.
    ///
    /// If there is no maximum body length, this returns [`core::usize::MAX`].
    #[inline]
    pub fn max_body_len(&self) -> usize {
        self.max_body_len
    }
}

/// A builder capable of serializing a packet's headers and footers.
///
/// A `PacketBuilder` describes a packet's headers and footers, and is capable
/// of serializing that packet into an existing buffer via the `serialize`
/// method. A `PacketBuilder` never describes a body - one must always be
/// provided in order to call `serialize`.
///
/// Thanks to a blanket implementation, every `PacketBuilder` is also a
/// [`NestedPacketBuilder`]. While a `PacketBuilder` represents exactly one
/// "layer" of a packet - a pair of a header and a footer - a
/// `NestedPacketBuilder` can represent multiple nested layers of a packet, and
/// can be composed from other `NestedPacketBuilder`s. See the trait
/// documentation for more details.
///
/// `()` may be used as an "empty" `PacketBuilder` with no header, footer,
/// minimum body length requirement, or maximum body length requirement.
pub trait PacketBuilder {
    /// Gets the constraints for this `PacketBuilder`.
    fn constraints(&self) -> PacketConstraints;

    /// Serializes this packet into an existing buffer.
    ///
    /// `serialize` is called with a [`SerializeBuffer`] which provides access
    /// to the parts of a buffer corresponding to the header, body, and footer
    /// of this packet. The buffer's body is initialized with the body to be
    /// encapsulated, and `serialize` is responsible for serializing the header
    /// and footer into the appropriate sections of the buffer. The caller is
    /// responsible for ensuring that the body satisfies both the minimum and
    /// maximum body length requirements, possibly by adding padding or
    /// truncating the body.
    ///
    /// This method is usually called from
    /// [`NestedPacketBuilder::serialize_into`], not directly by the user.
    ///
    /// # Security
    ///
    /// `serialize` must initialize the bytes of the header and footer, even if
    /// only to zero, in order to avoid leaking the contents of packets
    /// previously stored in the same buffer.
    ///
    /// # Panics
    ///
    /// May panic if the `SerializeBuffer`'s header or footer are not large
    /// enough to fit the packet's header and footer, or if the body does not
    /// satisfy the minimum or maximum body length requirements.
    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>);
}

/// One or more nested [`PacketBuilder`]s.
///
/// A `NestedPacketBuilder` represents one or more `PacketBuilder`s nested
/// inside of each other. Two `NestedPacketBuilder`s, `a` and `b`, can be
/// composed by calling `a.encapsulate(b)`. The resulting `NestedPacketBuilder`
/// has a header comprising `b`'s header followed by `a`'s header, and has a
/// footer comprising `a`'s footer followed by `b`'s footer. It also has minimum
/// and maximum body length requirements which are the composition of those of
/// `a` and `b`. See [`encapsulate`] for more details.
///
/// [`encapsulate`]: NestedPacketBuilder::encapsulate
pub trait NestedPacketBuilder {
    /// Gets the constraints for this `NestedPacketBuilder`.
    ///
    /// If `constraints` returns `None`, it means that a valid
    /// [`PacketConstraints`] cannot be constructed. Since
    /// `NestedPacketBuilder`s can be nested, multiple valid
    /// `NestedPacketBuilder`s can nest to create an invalid
    /// `NestedPacketBuilder`. This can happen if an inner
    /// `NestedPacketBuilder`'s headers and footers take up more space than an
    /// outer `NestedPacketBuilder`'s maximum body length, so the maximum body
    /// length of this `NestedPacketBuilder` is technically negative. It can
    /// also happen if the header and footer lengths, when summed, overflow
    /// `usize`. In either case, the `PacketConstraints` type cannot represent
    /// the constraints, and no body exists which would satisfy those
    /// constraints (a satisfying body would need to have negative length).
    /// Thus, the correct behavior is to interpret a `None` value as implying
    /// that an MTU error should be reported.
    ///
    /// If `constraints` returns `None`, the caller must not call
    /// [`serialize_into`], or risk unspecified behavior (including possibly a
    /// panic).
    ///
    /// [`serialize_into`]: NestedPacketBuilder::serialize_into
    fn try_constraints(&self) -> Option<PacketConstraints>;

    /// Serializes this `NestedPacketBuilder` into a buffer.
    ///
    /// `serialize_into` takes a buffer containing a body, and serializes this
    /// `NestedPacketBuilder`'s headers and footers before and after that body
    /// respectively. When `serialize_into` returns, the buffer's body has been
    /// expanded to include the newly-serialized headers and footers.
    ///
    /// If the provided body is smaller than this `NestedPacketBuilder`'s
    /// minimum body length requirement, `serialize_into` will add padding after
    /// the body in order to meet that requriement. If this
    /// `NestedPacketBuilder` comprises multiple other `NestedPacketBuilder`s
    /// with their own minimum body length requirements, then internal padding
    /// may be added between footers in order to meet those requirements. In
    /// particular, padding is always added after the body of the packet with
    /// that requirement (as opposed to after the body of an encapsulated
    /// packet).
    ///
    /// Callers should *not* add their own post-body padding! The minimum body
    /// length requirement might come from a `NestedPacketBuilder` which is not
    /// the innermost one, in which case padding belongs after at least one of
    /// the footers, and adding padding directly after the body would be
    /// incorrect. As described in the previous paragraph, `serialize_into` will
    /// take care of putting padding in the right place.
    ///
    /// # Security
    ///
    /// Any added padding will be zeroed in order to ensure that the contents of
    /// another packet previously stored in the same buffer do not leak.
    ///
    /// # Panics
    ///
    /// May panic if `buffer` doesn't have enough prefix and suffix space to
    /// hold the headers and footers. In particular, if this
    /// `NestedPacketBuilder` has [`PacketConstraints`] `c`, then `buffer` must
    /// satisfy the following requirements:
    /// - `buffer.prefix_len() >= c.header_len()`
    /// - `buffer.suffix_len() >= c.footer_len()`
    /// - `buffer.len() <= c.max_body_len()`
    /// - If `padding = c.min_body_len().saturating_sub(buffer.len())`, then
    ///   `buffer.suffix_len() >= padding + c.footer_len()`
    ///
    /// Note that the `PacketConstraints` type has certain invariants that make
    /// it easier for implementers to satisfy these preconditions.
    ///
    /// `serialize_into` may exhibit unspecified behavior (including possibly
    /// panicking) if `self.try_constraints()` returns `None`. In order to avoid
    /// a panic, the caller must call that method first, and only call
    /// `serialize_into` if it returns `Some`.
    fn serialize_into<B: TargetBuffer>(&self, buffer: &mut B);

    /// Encapsulates this `NestedPacketBuilder` inside of another one.
    ///
    /// If `a` and `b` are `NestedPacketBuilder`s with [`PacketConstraints`]
    /// `ac` and `bc`, then `a.encapsulate(b)` produces a `NestedPacketBuilder`
    /// with the following properties:
    /// - Its header is equivalent to `b`'s header followed by `a`'s header, and
    ///   has length `bc.header_len() + ac.header_len()`
    /// - Its footer is equivalent to `a`'s footer followed by `b`'s footer, and
    ///   has length `ac.footer_len() + bc.footer_len()`
    /// - Its minimum body length requirement is
    ///   `core::cmp::max(ac.min_body_len(), bc.min_body_len() -
    ///   (ac.header_len() + ac.footer_len()))`
    /// - Its maximum body length requirement is
    ///   `core::cmp::min(ac.max_body_len(), bc.max_body_len() -
    ///   (ac.header_len() + ac.footer_len()))`
    ///
    /// Note that `a` and `b` having valid `PacketConstraints` does *not* imply
    /// that `a.encapsulate(b)` will as well. This could be for one of the
    /// following reasons:
    /// - `b` has a maximum body length requirement which is exceeded by `a`'s
    ///   headers and footers alone (without considering further space occupied
    ///   by a body)
    /// - `b` has a maximum body length requirement which is exceeded by the sum
    ///   of `a`'s header and footers and `a`'s minimum body length requirement
    /// - The `PacketConstraints` would have values that overflow `usize`, such
    ///   as `b`'s header length plus `a`'s header length
    ///
    /// [`serialize_into`]: NestedPacketBuilder::serialize_into
    #[inline]
    fn encapsulate<O: NestedPacketBuilder>(self, outer: O) -> Nested<Self, O>
    where
        Self: Sized,
    {
        Nested { inner: self, outer }
    }

    // TODO(joshlf): Clarify that the MTU created by with_mtu apply outside of
    // the NestedPacketBuilder.

    /// Constructs a new `NestedPacketBuilder` with an additional maximum
    /// transmission unit (MTU) constraint.
    ///
    /// The returned `NestedPacketBuilder` will have a maximum body length
    /// constraint equal to the minimum of its original maximum body length
    /// constraint and `mtu`.
    #[inline]
    fn with_mtu(self, mtu: usize) -> MtuPacketBuilder<Self>
    where
        Self: Sized,
    {
        MtuPacketBuilder { mtu, inner: self }
    }
}

impl<PB: PacketBuilder> NestedPacketBuilder for PB {
    fn try_constraints(&self) -> Option<PacketConstraints> {
        Some(self.constraints())
    }

    fn serialize_into<B: TargetBuffer>(&self, buffer: &mut B) {
        buffer.serialize(self.constraints(), self);
    }
}

impl<'a, B: PacketBuilder> PacketBuilder for &'a B {
    #[inline]
    fn constraints(&self) -> PacketConstraints {
        B::constraints(self)
    }
    #[inline]
    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        B::serialize(self, buffer)
    }
}

impl<'a, B: PacketBuilder> PacketBuilder for &'a mut B {
    #[inline]
    fn constraints(&self) -> PacketConstraints {
        B::constraints(self)
    }
    #[inline]
    fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
        B::serialize(self, buffer)
    }
}

impl PacketBuilder for () {
    #[inline]
    fn constraints(&self) -> PacketConstraints {
        PacketConstraints {
            header_len: 0,
            footer_len: 0,
            min_body_len: 0,
            max_body_len: usize::MAX,
        }
    }
    #[inline]
    fn serialize(&self, _buffer: &mut SerializeBuffer<'_, '_>) {}
}

impl PacketBuilder for Never {
    fn constraints(&self) -> PacketConstraints {
        match *self {}
    }
    fn serialize(&self, _buffer: &mut SerializeBuffer<'_, '_>) {}
}

/// One object encapsulated in another one.
///
/// `Nested`s are constructed using the [`NestedPacketBuilder::encapsulate`] and
/// [`Serializer::encapsulate`] methods.
///
/// When `I: NestedPacketBuilder` and `O: NestedPacketBuilder`, `Nested<I, O>`
/// implements [`NestedPacketBuilder`]. When `I: Serializer` and `O:
/// NestedPacketBuilder`, `Nested<I, O>` implements [`Serializer`].
#[derive(Copy, Clone, Debug)]
#[cfg_attr(test, derive(Eq, PartialEq))]
pub struct Nested<I, O> {
    inner: I,
    outer: O,
}

impl<I, O> Nested<I, O> {
    /// Consumes this `Nested` and returns the inner object, discarding the
    /// outer one.
    #[inline]
    pub fn into_inner(self) -> I {
        self.inner
    }

    /// Consumes this `Nested` and returns the outer object, discarding the
    /// inner one.
    #[inline]
    pub fn into_outer(self) -> O {
        self.outer
    }
}

impl<I: NestedPacketBuilder, O: NestedPacketBuilder> NestedPacketBuilder for Nested<I, O> {
    #[inline]
    fn try_constraints(&self) -> Option<PacketConstraints> {
        let inner = self.inner.try_constraints()?;
        let outer = self.outer.try_constraints()?;

        // Test case 1 in test_nested_packet_builder
        let header_len = inner.header_len.checked_add(outer.header_len)?;
        // Test case 2 in test_nested_packet_builder
        let footer_len = inner.footer_len.checked_add(outer.footer_len)?;
        // This is guaranteed not to overflow by the invariants on
        // PacketConstraint.
        let inner_header_footer_len = inner.header_len + inner.footer_len;
        // Note the saturating_sub here - it's OK if the inner PacketBuilder
        // more than satisfies the outer PacketBuilder's minimum body length
        // requirement.
        let min_body_len = cmp::max(
            outer.min_body_len.saturating_sub(inner_header_footer_len),
            inner.min_body_len,
        );
        // Note the checked_sub here - it's NOT OK if the inner PacketBuilder
        // exceeds the outer PacketBuilder's maximum body length requirement.
        //
        // Test case 4 in test_nested_packet_builder
        let max_body_len =
            cmp::min(outer.max_body_len.checked_sub(inner_header_footer_len)?, inner.max_body_len);
        // It's still possible that `min_body_len > max_body_len` or that
        // `header_len + min_body_len + footer_len` overflows `usize`; `try_new`
        // checks those constraints for us.
        PacketConstraints::try_new(header_len, footer_len, min_body_len, max_body_len)
    }

    #[inline]
    fn serialize_into<B: TargetBuffer>(&self, buffer: &mut B) {
        // `serialize_into` is required to serialize padding, and in particular,
        // to serialize it in the right place, immediately following the body of
        // the packet which imposes the minimum body length requirement. This
        // happens naturally as a consequence of the fact that the
        // implementation of `NestedPacketBuilder` for `PB: PacketBuilder` adds
        // its own padding. Inner `PacketBuilder`s which do not have a minimum
        // body length requirement will not add padding and, when the
        // `PacketBuilder` with the minimum body length requirement is reached,
        // padding will still be required (assuming the minimum isn't already
        // satisfied by the original body and headers/footers), and that
        // `PacketBuilder`'s `serialize_into` implementation will add the
        // padding at that point.
        self.inner.serialize_into(buffer);
        self.outer.serialize_into(buffer);
    }
}

// It would be great to just do `impl<'a, PB: NestedPacketBuilder>
// NestedPacketBuilder for &'a PB`, but it would conflict with the blanket impl
// for `PB: PacketBuilder`. This replaces that impl. Note that it's not a big
// deal that we don't have that impl because the one that really matters is
// `PacketBuilder for &PB` where `PB: PacketBuilder` because users of this crate
// only implement the `PacketBuilder` trait.
struct RefNestedPacketBuilder<'a, PB>(&'a PB);

impl<'a, PB: NestedPacketBuilder> NestedPacketBuilder for RefNestedPacketBuilder<'a, PB> {
    fn try_constraints(&self) -> Option<PacketConstraints> {
        self.0.try_constraints()
    }

    fn serialize_into<B: TargetBuffer>(&self, buffer: &mut B) {
        self.0.serialize_into(buffer)
    }
}

/// A [`PacketBuilder`] with a specific maximum transmission unit (MTU)
/// constraint.
///
/// `MtuPacketBuilder`s are constructed using the
/// [`NestedPacketBuilder::with_mtu`] method.
#[derive(Copy, Clone, Debug)]
#[cfg_attr(test, derive(Eq, PartialEq))]
pub struct MtuPacketBuilder<B> {
    mtu: usize,
    inner: B,
}

impl<B> MtuPacketBuilder<B> {
    /// Consumes this `MtuPacketBuilder` and returns the inner `PacketBuilder`.
    #[inline]
    pub fn into_inner(self) -> B {
        self.inner
    }
}

impl<PB: NestedPacketBuilder> NestedPacketBuilder for MtuPacketBuilder<PB> {
    #[inline]
    fn try_constraints(&self) -> Option<PacketConstraints> {
        let mut c = self.inner.try_constraints()?;
        // This is guaranteed not to overflow by the invariants on
        // PacketConstraint.
        let header_footer_len = c.header_len + c.footer_len;
        c.max_body_len = cmp::min(self.mtu.checked_sub(header_footer_len)?, c.max_body_len);
        Some(c)
    }

    #[inline]
    fn serialize_into<B: TargetBuffer>(&self, buffer: &mut B) {
        self.inner.serialize_into(buffer)
    }
}

/// A builder capable of serializing packets - which do not encapsulate other
/// packets - into an existing buffer.
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
    /// `serialize` is called with a buffer of length `self.bytes_len()`, and is
    /// responsible for serializing the packet into the buffer.
    ///
    /// # Security
    ///
    /// All of the bytes of the buffer should be initialized, even if only to
    /// zero, in order to avoid leaking the contents of packets previously
    /// stored in the same buffer.
    ///
    /// # Panics
    ///
    /// May panic if `buffer.len() != self.bytes_len()`.
    fn serialize(&self, buffer: &mut [u8]);

    /// Converts this `InnerPacketBuilder` into a [`Serializer`].
    ///
    /// `into_serializer` is like [`into_serializer_with`], except that no
    /// buffer is provided for reuse in serialization.
    ///
    /// [`into_serializer_with`]: InnerPacketBuilder::into_serializer_with
    #[inline]
    fn into_serializer(self) -> InnerSerializer<Self, EmptyBuf>
    where
        Self: Sized,
    {
        self.into_serializer_with(EmptyBuf)
    }

    /// Converts this `InnerPacketBuilder` into a [`Serializer`] with a buffer
    /// that can be used for serialization.
    ///
    /// `into_serializer_with` consumes a buffer and converts `self` into a type
    /// which implements `Serialize` by treating it as the innermost body to be
    /// contained within any encapsulating [`PacketBuilder`]s. During
    /// serialization, `buffer` will be provided to the [`BufferProvider`],
    /// allowing it to reuse the buffer for serialization and avoid allocating a
    /// new one if possible.
    ///
    /// `buffer` will have its body shrunk to be zero bytes before the
    /// `InnerSerializer` is constructed.
    fn into_serializer_with<B: BufferMut>(self, mut buffer: B) -> InnerSerializer<Self, B>
    where
        Self: Sized,
    {
        buffer.shrink_back_to(0);
        InnerSerializer { inner: self, buffer }
    }
}

impl<'a, I: InnerPacketBuilder> InnerPacketBuilder for &'a I {
    #[inline]
    fn bytes_len(&self) -> usize {
        I::bytes_len(self)
    }
    #[inline]
    fn serialize(&self, buffer: &mut [u8]) {
        I::serialize(self, buffer)
    }
}
impl<'a, I: InnerPacketBuilder> InnerPacketBuilder for &'a mut I {
    #[inline]
    fn bytes_len(&self) -> usize {
        I::bytes_len(self)
    }
    #[inline]
    fn serialize(&self, buffer: &mut [u8]) {
        I::serialize(self, buffer)
    }
}
impl<'a> InnerPacketBuilder for &'a [u8] {
    #[inline]
    fn bytes_len(&self) -> usize {
        self.len()
    }
    #[inline]
    fn serialize(&self, buffer: &mut [u8]) {
        buffer.copy_from_slice(self);
    }
}
impl<'a> InnerPacketBuilder for &'a mut [u8] {
    #[inline]
    fn bytes_len(&self) -> usize {
        self.len()
    }
    #[inline]
    fn serialize(&self, buffer: &mut [u8]) {
        buffer.copy_from_slice(self);
    }
}
impl<'a> InnerPacketBuilder for Vec<u8> {
    #[inline]
    fn bytes_len(&self) -> usize {
        self.len()
    }
    #[inline]
    fn serialize(&self, buffer: &mut [u8]) {
        buffer.copy_from_slice(self.as_slice());
    }
}
impl<const N: usize> InnerPacketBuilder for ArrayVec<u8, N> {
    fn bytes_len(&self) -> usize {
        self.as_slice().bytes_len()
    }
    fn serialize(&self, buffer: &mut [u8]) {
        self.as_slice().serialize(buffer);
    }
}

/// An [`InnerPacketBuilder`] created from any [`B: ByteSlice`].
///
/// `ByteSliceInnerPacketBuilder<B>` implements `InnerPacketBuilder` so long as
/// `B: ByteSlice`.
///
/// [`B: ByteSlice`]: zerocopy::ByteSlice
pub struct ByteSliceInnerPacketBuilder<B>(pub B);

impl<B: ByteSlice> InnerPacketBuilder for ByteSliceInnerPacketBuilder<B> {
    fn bytes_len(&self) -> usize {
        self.0.deref().bytes_len()
    }
    fn serialize(&self, buffer: &mut [u8]) {
        self.0.deref().serialize(buffer)
    }
}

impl<B: ByteSlice> Debug for ByteSliceInnerPacketBuilder<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        write!(f, "ByteSliceInnerPacketBuilder({:?})", self.0.as_ref())
    }
}

/// An error in serializing a packet.
///
/// `SerializeError` is the type of errors returned from methods on the
/// [`Serializer`] trait. The `Alloc` variant indicates that a new buffer could
/// not be allocated, while the `Mtu` variant indicates that an MTU constraint
/// was exceeded.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum SerializeError<A> {
    /// A new buffer could not be allocated.
    Alloc(A),
    /// A maximum transmission unit (MTU) constraint was exceeded.
    Mtu,
}

impl<A> SerializeError<A> {
    /// Is this `SerializeError::Alloc`?
    #[inline]
    pub fn is_alloc(&self) -> bool {
        match self {
            SerializeError::Alloc(_) => true,
            SerializeError::Mtu => false,
        }
    }

    /// Is this `SerializeError::Mtu`?
    #[inline]
    pub fn is_mtu(&self) -> bool {
        match self {
            SerializeError::Alloc(_) => false,
            SerializeError::Mtu => true,
        }
    }
}

impl<A> From<A> for SerializeError<A> {
    fn from(a: A) -> SerializeError<A> {
        SerializeError::Alloc(a)
    }
}

/// The error returned when a buffer is too short to hold a serialized packet,
/// and the [`BufferProvider`] is incapable of allocating a new one.
///
/// `BufferTooShortError` is returned by the [`Serializer`] methods
/// [`serialize_no_alloc`] and [`serialize_no_alloc_outer`].
///
/// [`serialize_no_alloc`]: Serializer::serialize_no_alloc
/// [`serialize_no_alloc_outer`]: Serializer::serialize_no_alloc_outer
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct BufferTooShortError;

/// An object capable of providing buffers which satisfy certain constraints.
///
/// A `BufferProvider<Input, Output>` is an object which is capable of consuming
/// a buffer of type `Input` and, either by reusing it or by allocating a new
/// one and copying the input buffer's body into it, producing a buffer of type
/// `Output` which meets certain prefix and suffix length constraints.
///
/// A `BufferProvider` must always be provided when serializing a
/// [`Serializer`].
///
/// Implementors may find the helper function [`try_reuse_buffer`] useful.
///
/// For clients who don't need the full expressive power of this trait, the
/// simpler [`BufferAlloc`] trait is provided. It only defines how to allocate
/// new buffers, and two blanket impls of `BufferProvider` are provided for all
/// `BufferAlloc` types.
pub trait BufferProvider<Input, Output> {
    /// The type of errors returned from [`reuse_or_realloc`].
    ///
    /// [`reuse_or_realloc`]: BufferProvider::reuse_or_realloc
    type Error;

    /// Consumes an input buffer and attempts to produce an output buffer with
    /// the given constraints, either by reusing the input buffer or by
    /// allocating a new one and copying the body into it.
    ///
    /// `reuse_or_realloc` consumes a buffer by value, and produces a new buffer
    /// with the following invariants:
    /// - The output buffer must have at least `prefix` bytes of prefix
    /// - The output buffer must have at least `suffix` bytes of suffix
    /// - The output buffer must have the same body as the input buffer
    ///
    /// If these requirements cannot be met, then an error is returned along
    /// with the input buffer, which is unmodified.
    fn reuse_or_realloc(
        self,
        buffer: Input,
        prefix: usize,
        suffix: usize,
    ) -> Result<Output, (Self::Error, Input)>;
}

/// An object capable of allocating new buffers.
///
/// A `BufferAlloc<Output>` is an object which is capable of allocating new
/// buffers of type `Output`.
///
/// [Two blanket implementations] of [`BufferProvider`] are given for any type
/// which implements `BufferAlloc<O>`. One blanket implementation works for any
/// input buffer type, `I`, and produces buffers of type `Either<I, O>` as
/// output. One blanket implementation works only when the input and output
/// buffer types are the same, and produces buffers of that type. See the
/// documentation on those impls for more details.
///
/// The following implementations of `BufferAlloc` are provided:
/// - Any `FnOnce(usize) -> Result<O, E>` implements `BufferAlloc<O, Error = E>`
/// - `()` implements `BufferAlloc<Never, Error = ()>` (an allocator which
///   always fails)
/// - [`new_buf_vec`] implements `BufferAlloc<Buf<Vec<u8>>, Error = Never>` (an
///   allocator which infallibly heap-allocates `Vec`s)
///
/// [Two blanket implementations]: trait.BufferProvider.html#implementors
pub trait BufferAlloc<Output> {
    /// The type of errors returned from [`alloc`].
    ///
    /// [`alloc`]: BufferAlloc::alloc
    type Error;

    /// Attempts to allocate a new buffer of size `len`.
    fn alloc(self, len: usize) -> Result<Output, Self::Error>;
}

impl<O, E, F: FnOnce(usize) -> Result<O, E>> BufferAlloc<O> for F {
    type Error = E;

    #[inline]
    fn alloc(self, len: usize) -> Result<O, E> {
        self(len)
    }
}

impl BufferAlloc<Never> for () {
    type Error = ();

    #[inline]
    fn alloc(self, _len: usize) -> Result<Never, ()> {
        Err(())
    }
}

/// Allocates a new `Buf<Vec<u8>>`.
///
/// `new_buf_vec(len)` is shorthand for `Ok(Buf::new(vec![0; len], ..))`. It
/// implements [`BufferAlloc<Buf<Vec<u8>>, Error = Never>`], and, thanks to a
/// blanket impl, [`BufferProvider<I, Either<I, Buf<Vec<u8>>>, Error = Never>`]
/// for all `I: BufferMut`, and `BufferProvider<Buf<Vec<u8>>, Buf<Vec<u8>>,
/// Error = Never>`.
///
/// [`BufferAlloc<Buf<Vec<u8>>, Error = Never>`]: BufferAlloc
/// [`BufferProvider<I, Either<I, Buf<Vec<u8>>>, Error = Never>`]: BufferProvider
pub fn new_buf_vec(len: usize) -> Result<Buf<Vec<u8>>, Never> {
    Ok(Buf::new(vec![0; len], ..))
}

/// Attempts to reuse a buffer for the purposes of implementing
/// [`BufferProvider::reuse_or_realloc`].
///
/// `try_reuse_buffer` attempts to reuse an existing buffer to satisfy the given
/// prefix and suffix constraints. If it succeeds, it returns `Ok` containing a
/// buffer with the same body as the input, and with at least `prefix` prefix
/// bytes and at least `suffix` suffix bytes. Otherwise, it returns `Err`
/// containing the original, unmodified input buffer.
///
/// Concretely, `try_reuse_buffer` has the following behavior:
/// - If the prefix and suffix constraints are already met, it returns `Ok` with
///   the input unmodified
/// - If the prefix and suffix constraints are not yet met, then...
///   - If there is enough capacity to meet the constraints and the body is not
///     larger than `max_copy_bytes`, the body will be moved within the buffer
///     in order to meet the constraints, and it will be returned
///   - Otherwise, if there is not enough capacity or the body is larger than
///     `max_copy_bytes`, it returns `Err` with the input unmodified
///
/// `max_copy_bytes` is meant to be an estimate of how many bytes can be copied
/// before allocating a new buffer will be cheaper than copying.
#[inline]
pub fn try_reuse_buffer<B: GrowBufferMut + ShrinkBuffer>(
    mut buffer: B,
    prefix: usize,
    suffix: usize,
    max_copy_bytes: usize,
) -> Result<B, B> {
    let need_prefix = prefix;
    let need_suffix = suffix;
    let have_prefix = buffer.prefix_len();
    let have_body = buffer.len();
    let have_suffix = buffer.suffix_len();
    let need_capacity = need_prefix + have_body + need_suffix;

    if have_prefix >= need_prefix && have_suffix >= need_suffix {
        // We already satisfy the prefix and suffix requirements.
        Ok(buffer)
    } else if buffer.capacity() >= need_capacity && have_body <= max_copy_bytes {
        // The buffer is large enough, but the body is currently too far
        // forward or too far backwards to satisfy the prefix or suffix
        // requirements, so we need to move the body within the buffer.
        buffer.reset();

        // Copy the original body range to a point starting immediatley
        // after `prefix`. This satisfies the `prefix` constraint by
        // definition, and satisfies the `suffix` constraint since we know
        // that the total buffer capacity is sufficient to hold the total
        // length of the prefix, body, and suffix.
        buffer.copy_within(have_prefix..(have_prefix + have_body), need_prefix);
        buffer.shrink(need_prefix..(need_prefix + have_body));
        debug_assert_eq!(buffer.prefix_len(), need_prefix);
        debug_assert!(buffer.suffix_len() >= need_suffix);
        debug_assert_eq!(buffer.len(), have_body);
        Ok(buffer)
    } else {
        Err(buffer)
    }
}

/// All types which implement `BufferAlloc<O>` also implement `BufferProvider<I,
/// Either<I, O>>` for all `I` where `I` and `O` both implement [`BufferMut`].
///
/// Note that, if `I` and `O` are the same type, calling methods on `Either<I,
/// O>` will often be just as fast as calling methods on `I`/`O` itself since
/// Rust can optimize out branching on which enum variant is present. However,
/// in this case, an impl of `BufferProvider<I, I>` is also provided.
impl<I: ReusableBuffer, O: ReusableBuffer, A: BufferAlloc<O>> BufferProvider<I, Either<I, O>>
    for A
{
    type Error = A::Error;

    /// If `buffer` has enough capacity to store `need_prefix + need_suffix +
    /// buffer.len()` bytes, then reuse `buffer`. Otherwise, allocate a new
    /// buffer using `A`'s [`BufferAlloc`] implementation.
    ///
    /// If there is enough capacity, but the body is too far forwards or
    /// backwards in the buffer to satisfy the prefix and suffix constraints,
    /// the body will be moved within the buffer in order to satisfy the
    /// constraints. This operation is linear in the length of the body.
    #[inline]
    fn reuse_or_realloc(
        self,
        buffer: I,
        need_prefix: usize,
        need_suffix: usize,
    ) -> Result<Either<I, O>, (A::Error, I)> {
        // TODO(joshlf): Maybe it's worth coming up with a heuristic for when
        // moving the body is likely to be more expensive than allocating
        // (rather than just using `usize::MAX`)? This will be tough since we
        // don't know anything about the performance of `A::alloc`.
        match try_reuse_buffer(buffer, need_prefix, need_suffix, usize::MAX) {
            Ok(buffer) => Ok(Either::A(buffer)),
            Err(buffer) => {
                let have_body = buffer.len();
                let need_capacity = need_prefix + have_body + need_suffix;

                let mut buf = match BufferAlloc::alloc(self, need_capacity) {
                    Ok(buf) => buf,
                    Err(err) => return Err((err, buffer)),
                };
                buf.shrink(need_prefix..(need_prefix + have_body));

                buf.copy_from(&buffer);
                debug_assert_eq!(buf.prefix_len(), need_prefix);
                debug_assert!(buf.suffix_len() >= need_suffix);
                debug_assert_eq!(buf.len(), have_body);
                Ok(Either::B(buf))
            }
        }
    }
}

/// All types which implement `BufferAlloc<B>` also implement `BufferProvider<B,
/// B>` where `B` implements [`GrowBufferMut`] and [`ShrinkBuffer`].
impl<B: ReusableBuffer, A: BufferAlloc<B>> BufferProvider<B, B> for A {
    type Error = A::Error;

    /// If `buffer` has enough capacity to store `need_prefix + need_suffix +
    /// buffer.len()` bytes, then reuse `buffer`. Otherwise, allocate a new
    /// buffer using `A`'s [`BufferAlloc`] implementation.
    ///
    /// If there is enough capacity, but the body is too far forwards or
    /// backwards in the buffer to satisfy the prefix and suffix constraints,
    /// the body will be moved within the buffer in order to satisfy the
    /// constraints. This operation is linear in the length of the body.
    #[inline]
    fn reuse_or_realloc(self, buffer: B, prefix: usize, suffix: usize) -> Result<B, (A::Error, B)> {
        BufferProvider::<B, Either<B, B>>::reuse_or_realloc(self, buffer, prefix, suffix)
            .map(Either::into_inner)
    }
}

pub trait Serializer: Sized {
    /// The type of buffers returned from serialization methods on this trait.
    type Buffer;

    /// Serializes this `Serializer`, producing a buffer.
    ///
    /// `serialize` accepts a [`PacketBuilder`] and a [`BufferProvider`], and
    /// produces a buffer which contains the contents of this `Serializer`
    /// encapsulated in the header and footer described by the `PacketBuilder`.
    ///
    /// As `Serializer`s can be nested using the [`Nested`] type (constructed
    /// using the [`encapsulate`] method), the `serialize` method is recursive -
    /// calling it on a `Nested` will recurse into the inner `Serializer`, which
    /// might itself be a `Nested`, and so on. When the innermost `Serializer`
    /// is reached, the contained buffer is passed to the `provider`, allowing
    /// it to decide how to produce a buffer which is large enough to fit the
    /// entire packet - either by reusing the existing buffer, or by discarding
    /// it and allocating a new one.
    ///
    /// [`encapsulate`]: Serializer::encapsulate
    fn serialize<B: TargetBuffer, PB: NestedPacketBuilder, P: BufferProvider<Self::Buffer, B>>(
        self,
        outer: PB,
        provider: P,
    ) -> Result<B, (SerializeError<P::Error>, Self)>;

    /// Serializes this `Serializer`, allocating a [`Buf<Vec<u8>>`] if the
    /// contained buffer isn't large enough.
    ///
    /// `serialize_vec` is like [`serialize`], except that, if the contained
    /// buffer isn't large enough to contain the packet, a new `Vec<u8>` is
    /// allocated and wrapped in a [`Buf`]. If the buffer is large enough, but
    /// the body is too far forwards or backwards to fit the encapsulating
    /// headers or footers, the body will be moved within the buffer (this
    /// operation's cost is linear in the size of the body).
    ///
    /// `serialize_vec` is equivalent to calling `serialize` with
    /// [`new_buf_vec`] as the [`BufferProvider`].
    ///
    /// [`Buf<Vec<u8>>`]: Buf
    /// [`serialize`]: Serializer::serialize
    #[inline]
    #[allow(clippy::type_complexity)]
    fn serialize_vec<PB: NestedPacketBuilder>(
        self,
        outer: PB,
    ) -> Result<Either<Self::Buffer, Buf<Vec<u8>>>, (SerializeError<Never>, Self)>
    where
        Self::Buffer: ReusableBuffer,
    {
        self.serialize(outer, new_buf_vec)
    }

    /// Serializes this `Serializer`, failing if the existing buffer is not
    /// large enough.
    ///
    /// `serialize_no_alloc` is like [`serialize`], except that it will fail if
    /// the existing buffer isn't large enough. If the buffer is large enough,
    /// but the body is too far forwards or backwards to fit the encapsulating
    /// headers or footers, the body will be moved within the buffer (this
    /// operation's cost is linear in the size of the body).
    ///
    /// `serialize_no_alloc` is equivalent to calling `serialize` with a
    /// `BufferProvider` which cannot allocate a new buffer (such as `()`).
    ///
    /// [`serialize`]: Serializer::serialize
    #[inline]
    fn serialize_no_alloc<PB: NestedPacketBuilder>(
        self,
        outer: PB,
    ) -> Result<Self::Buffer, (SerializeError<BufferTooShortError>, Self)>
    where
        Self::Buffer: ReusableBuffer,
    {
        self.serialize(outer, ()).map(Either::into_a).map_err(|(err, slf)| {
            (
                match err {
                    SerializeError::Alloc(()) => BufferTooShortError.into(),
                    SerializeError::Mtu => SerializeError::Mtu,
                },
                slf,
            )
        })
    }

    /// Serializes this `Serializer` as the outermost packet.
    ///
    /// `serialize_outer` is like [`serialize`], except that it is called when
    /// this `Serializer` describes the outermost packet, not encapsulated in
    /// any other packets. It is equivalent to calling `serialize` with an empty
    /// [`PacketBuilder`] (such as `()`).
    ///
    /// [`serialize`]: Serializer::serialize
    #[inline]
    fn serialize_outer<B: TargetBuffer, P: BufferProvider<Self::Buffer, B>>(
        self,
        provider: P,
    ) -> Result<B, (SerializeError<P::Error>, Self)> {
        self.serialize((), provider)
    }

    /// Serializes this `Serializer` as the outermost packet, allocating a
    /// [`Buf<Vec<u8>>`] if the contained buffer isn't large enough.
    ///
    /// `serialize_vec_outer` is like [`serialize_vec`], except that it is
    /// called when this `Serializer` describes the outermost packet, not
    /// encapsulated in any other packets. It is equivalent to calling
    /// `serialize_vec` with an empty [`PacketBuilder`] (such as `()`).
    ///
    /// [`Buf<Vec<u8>>`]: Buf
    /// [`serialize_vec`]: Serializer::serialize_vec
    #[inline]
    #[allow(clippy::type_complexity)]
    fn serialize_vec_outer(
        self,
    ) -> Result<Either<Self::Buffer, Buf<Vec<u8>>>, (SerializeError<Never>, Self)>
    where
        Self::Buffer: ReusableBuffer,
    {
        self.serialize_vec(())
    }

    /// Serializes this `Serializer` as the outermost packet, failing if the
    /// existing buffer is not large enough.
    ///
    /// `serialize_no_alloc_outer` is like [`serialize_no_alloc`], except that
    /// it is called when this `Serializer` describes the outermost packet, not
    /// encapsulated in any other packets. It is equivalent to calling
    /// `serialize_no_alloc` with an empty [`PacketBuilder`] (such as `()`).
    ///
    /// [`serialize_no_alloc`]: Serializer::serialize_no_alloc
    #[inline]
    fn serialize_no_alloc_outer(
        self,
    ) -> Result<Self::Buffer, (SerializeError<BufferTooShortError>, Self)>
    where
        Self::Buffer: ReusableBuffer,
    {
        self.serialize_no_alloc(())
    }

    /// Encapsulates this `Serializer` in another packet, producing a new
    /// `Serializer`.
    ///
    /// `encapsulate` consumes this `Serializer` and a [`PacketBuilder`], and
    /// produces a new `Serializer` which describes encapsulating this one in
    /// the packet described by `outer`.
    #[inline]
    fn encapsulate<B: NestedPacketBuilder>(self, outer: B) -> Nested<Self, B> {
        Nested { inner: self, outer }
    }

    /// Creates a new `Serializer` which will enforce a maximum transmission
    /// unit (MTU).
    ///
    /// `with_mtu` consumes this `Serializer` and an MTU, and produces a new
    /// `Serializer` which will enforce the given MTU on all serialization
    /// requests. Note that the given MTU will be enforced at this layer -
    /// serialization requests will be rejected if the body produced by the
    /// request at this layer would exceed the MTU. It has no effect on headers
    /// or footers added by encapsulating layers outside of this one.
    #[inline]
    fn with_mtu(self, mtu: usize) -> Nested<Self, MtuPacketBuilder<()>> {
        self.encapsulate(MtuPacketBuilder { mtu, inner: () })
    }
}

/// A [`Serializer`] constructed from an [`InnerPacketBuilder`].
///
/// An `InnerSerializer` wraps an `InnerPacketBuilder` and a buffer, and
/// implements the `Serializer` trait. When a serialization is requested, it
/// either reuses the stored buffer or allocates a new one large enough to hold
/// itself and all outer `PacketBuilder`s.
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct InnerSerializer<I, B> {
    inner: I,
    // The buffer's length must be zero since we encapsulate the buffer in a
    // PacketBuilder. If the length were non-zero, that would have the effect of
    // retaining the contents of the buffer when serializing, and putting them
    // immediately after the bytes of `inner`.
    buffer: B,
}

impl<I: InnerPacketBuilder, B: GrowBuffer + ShrinkBuffer> Serializer for InnerSerializer<I, B> {
    type Buffer = B;

    #[inline]
    #[allow(clippy::type_complexity)]
    fn serialize<BB: TargetBuffer, PB: NestedPacketBuilder, P: BufferProvider<B, BB>>(
        self,
        outer: PB,
        provider: P,
    ) -> Result<BB, (SerializeError<P::Error>, InnerSerializer<I, B>)> {
        // A wrapper for InnerPacketBuilders which implements PacketBuilder by
        // treating the entire InnerPacketBuilder as the header of the
        // PacketBuilder. This allows us to compose our InnerPacketBuilder with
        // the outer PacketBuilders into a single, large PacketBuilder, and then
        // serialize it using self.buffer.
        struct InnerPacketBuilderWrapper<I>(I);

        impl<I: InnerPacketBuilder> PacketBuilder for InnerPacketBuilderWrapper<I> {
            fn constraints(&self) -> PacketConstraints {
                PacketConstraints::new(self.0.bytes_len(), 0, 0, usize::MAX)
            }

            fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
                // Note that the body might be non-empty if an outer
                // PacketBuilder added a minimum body length constraint that
                // required padding.
                debug_assert_eq!(buffer.header().len(), self.0.bytes_len());
                debug_assert_eq!(buffer.footer().len(), 0);

                InnerPacketBuilder::serialize(&self.0, buffer.header());
            }
        }

        let pb = InnerPacketBuilderWrapper(self.inner);
        debug_assert_eq!(self.buffer.len(), 0);
        match self.buffer.encapsulate(&pb).serialize(outer, provider) {
            Ok(buf) => Ok(buf),
            Err((err, buffer)) => {
                let buffer = buffer.into_inner();
                Err((err, InnerSerializer { inner: pb.0, buffer }))
            }
        }
    }
}

impl<B: GrowBuffer + ShrinkBuffer> Serializer for B {
    type Buffer = B;

    #[inline]
    fn serialize<BB: TargetBuffer, PB: NestedPacketBuilder, P: BufferProvider<Self::Buffer, BB>>(
        self,
        outer: PB,
        provider: P,
    ) -> Result<BB, (SerializeError<P::Error>, Self)> {
        TruncatingSerializer::new(self, TruncateDirection::NoTruncating)
            .serialize(outer, provider)
            .map_err(|(err, ser)| (err, ser.buffer))
    }
}

/// Either of two serializers.
///
/// An `EitherSerializer` wraps one of two different serializer types.
pub enum EitherSerializer<A, B> {
    A(A),
    B(B),
}

impl<A: Serializer, B: Serializer<Buffer = A::Buffer>> Serializer for EitherSerializer<A, B> {
    type Buffer = A::Buffer;

    fn serialize<TB: TargetBuffer, PB: NestedPacketBuilder, P: BufferProvider<Self::Buffer, TB>>(
        self,
        outer: PB,
        provider: P,
    ) -> Result<TB, (SerializeError<P::Error>, Self)> {
        match self {
            EitherSerializer::A(s) => {
                s.serialize(outer, provider).map_err(|(err, s)| (err, EitherSerializer::A(s)))
            }
            EitherSerializer::B(s) => {
                s.serialize(outer, provider).map_err(|(err, s)| (err, EitherSerializer::B(s)))
            }
        }
    }
}

/// The direction a buffer's body should be truncated from to force
/// it to fit within a maximum transmission unit (MTU).
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
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

/// A [`Serializer`] that truncates its body if it would exceed a maximum
/// transmission unit (MTU) constraint.
///
/// `TruncatingSerializer` wraps a buffer, and implements `Serializer`. Unlike
/// the blanket impl of `Serializer` for `B: GrowBuffer + ShrinkBuffer`, if the
/// buffer's body exceeds the MTU constraint passed to `Serializer::serialize`,
/// the body is truncated to fit.
///
/// Note that this does not guarantee that MTU errors will not occur. The MTU
/// may be small enough that the encapsulating headers alone exceed the MTU.
/// There may also be a minimum body length constraint which is larger than the
/// MTU.
#[derive(Copy, Clone, Debug)]
#[cfg_attr(test, derive(Eq, PartialEq))]
pub struct TruncatingSerializer<B> {
    buffer: B,
    direction: TruncateDirection,
}

impl<B> TruncatingSerializer<B> {
    /// Constructs a new `TruncatingSerializer`.
    pub fn new(buffer: B, direction: TruncateDirection) -> TruncatingSerializer<B> {
        TruncatingSerializer { buffer, direction }
    }
}

impl<B: GrowBuffer + ShrinkBuffer> Serializer for TruncatingSerializer<B> {
    type Buffer = B;

    fn serialize<BB: TargetBuffer, PB: NestedPacketBuilder, P: BufferProvider<B, BB>>(
        mut self,
        outer: PB,
        provider: P,
    ) -> Result<BB, (SerializeError<P::Error>, Self)> {
        let c = match outer.try_constraints() {
            Some(c) => c,
            None => return Err((SerializeError::Mtu, self)),
        };
        let original_len = self.buffer.len();
        let excess_bytes =
            if original_len > c.max_body_len { Some(original_len - c.max_body_len) } else { None };
        if let Some(excess_bytes) = excess_bytes {
            match self.direction {
                TruncateDirection::DiscardFront => self.buffer.shrink_front(excess_bytes),
                TruncateDirection::DiscardBack => self.buffer.shrink_back(excess_bytes),
                TruncateDirection::NoTruncating => return Err((SerializeError::Mtu, self)),
            }
        }

        let padding = c.min_body_len().saturating_sub(self.buffer.len());

        // At this point, the body and padding MUST fit within the MTU. Note
        // that PacketConstraints guarantees that min_body_len <= max_body_len,
        // so the padding can't cause this assertion to fail.
        debug_assert!(self.buffer.len() + padding <= c.max_body_len());
        match provider.reuse_or_realloc(self.buffer, c.header_len(), padding + c.footer_len()) {
            Ok(mut buffer) => {
                outer.serialize_into(&mut buffer);
                Ok(buffer)
            }
            Err((err, mut buffer)) => {
                // Undo the effects of shrinking the buffer so that the buffer
                // we return is unmodified from its original (which is required
                // by the contract of this method).
                if let Some(excess_bytes) = excess_bytes {
                    match self.direction {
                        TruncateDirection::DiscardFront => buffer.grow_front(excess_bytes),
                        TruncateDirection::DiscardBack => buffer.grow_back(excess_bytes),
                        TruncateDirection::NoTruncating => unreachable!(),
                    }
                }

                Err((
                    SerializeError::Alloc(err),
                    TruncatingSerializer { buffer, direction: self.direction },
                ))
            }
        }
    }
}

impl<I: Serializer, O: NestedPacketBuilder> Serializer for Nested<I, O> {
    type Buffer = I::Buffer;

    #[inline]
    fn serialize<B: TargetBuffer, PB: NestedPacketBuilder, P: BufferProvider<I::Buffer, B>>(
        self,
        outer: PB,
        provider: P,
    ) -> Result<B, (SerializeError<P::Error>, Self)> {
        // We use `RefNestedPacketBuilder` here so that the call to `serialize`
        // doesn't consume `self.outer` by value. If it did, we'd have to way of
        // getting it back in the event of an error (when we need to reconstruct
        // `self` to return).
        match self.inner.serialize(
            NestedPacketBuilder::encapsulate(RefNestedPacketBuilder(&self.outer), outer),
            provider,
        ) {
            Ok(buf) => Ok(buf),
            Err((err, inner)) => Err((err, inner.encapsulate(self.outer))),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::Buffer;
    use std::fmt::Debug;
    use test_case::test_case;

    // DummyPacketBuilder:
    // - Implements PacketBuilder with the stored constraints; it fills the
    //   header with 0xFF and the footer with 0xFE
    // - Implements InnerPacketBuilder by consuming a `header_len`-bytes body,
    //   and filling it with 0xFF
    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    struct DummyPacketBuilder {
        header_len: usize,
        footer_len: usize,
        min_body_len: usize,
        max_body_len: usize,
    }

    impl DummyPacketBuilder {
        fn new(
            header_len: usize,
            footer_len: usize,
            min_body_len: usize,
            max_body_len: usize,
        ) -> DummyPacketBuilder {
            DummyPacketBuilder { header_len, footer_len, min_body_len, max_body_len }
        }
    }

    fn fill(bytes: &mut [u8], byte: u8) {
        for b in bytes {
            *b = byte;
        }
    }

    impl PacketBuilder for DummyPacketBuilder {
        fn constraints(&self) -> PacketConstraints {
            PacketConstraints::new(
                self.header_len,
                self.footer_len,
                self.min_body_len,
                self.max_body_len,
            )
        }

        fn serialize(&self, buffer: &mut SerializeBuffer<'_, '_>) {
            // // `serialize` is allowed to panic if called on a `PacketBuilder`
            // // with invalid constraints.
            // assert!(self.try_constraints().is_some());
            assert_eq!(buffer.header().len(), self.header_len);
            assert_eq!(buffer.footer().len(), self.footer_len);
            assert!(buffer.body().len() >= self.min_body_len);
            assert!(buffer.body().len() <= self.max_body_len);
            fill(buffer.header(), 0xFF);
            fill(buffer.footer(), 0xFE);
        }
    }

    impl InnerPacketBuilder for DummyPacketBuilder {
        fn bytes_len(&self) -> usize {
            self.header_len
        }

        fn serialize(&self, buffer: &mut [u8]) {
            assert_eq!(buffer.len(), self.header_len);
            fill(buffer, 0xFF);
        }
    }

    impl SerializeError<Never> {
        fn into<E>(self) -> SerializeError<E> {
            match self {
                SerializeError::Alloc(never) => match never {},
                SerializeError::Mtu => SerializeError::Mtu,
            }
        }
    }

    // A Serializer that verifies certain invariants while operating. In
    // particular:
    // - If serialization fails, the original Serializer is returned unmodified.
    // - If `outer.try_constraints()` returns `None`, serialization fails.
    // - If the MTU is exceeded and truncation is disabled, serialization fails.
    // - If serialization succeeds, the body has the correct length, including
    //   taking into account `outer`'s minimum body length requirement
    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    struct VerifyingSerializer<S> {
        ser: S,
        // Is the inner Serializer a truncating (a TruncatingSerializer with
        // TruncateDirection::DiscardFront or DiscardBack)?
        truncating: bool,
    }

    impl<S: Serializer + Debug + Clone + Eq> Serializer for VerifyingSerializer<S>
    where
        S::Buffer: ReusableBuffer,
    {
        type Buffer = S::Buffer;

        fn serialize<
            B: TargetBuffer,
            PB: NestedPacketBuilder,
            P: BufferProvider<Self::Buffer, B>,
        >(
            self,
            outer: PB,
            provider: P,
        ) -> Result<B, (SerializeError<P::Error>, Self)> {
            let orig = self.ser.clone();

            // How long is the packet if we serialize it without the outer
            // PacketBuilder?
            let inner_len = match self.clone().ser.serialize_vec_outer() {
                Ok(buf) => buf.len(),
                Err((err, ser)) => {
                    // If serialization fails, the original Serializer should be
                    // unmodified.
                    assert_eq!(ser, orig);
                    return Err((err.into(), ser.into_verifying(self.truncating)));
                }
            };
            let outer_constraints = outer.try_constraints();
            let should_fail_mtu = outer_constraints
                .map(|c| c.max_body_len() < inner_len && !self.truncating)
                .unwrap_or(true);

            let res = self.ser.serialize(outer, provider);
            match res {
                Ok(buf) => {
                    let c = outer_constraints.unwrap();
                    // Since serialization has succeeded, we know that either
                    // inner_len <= c.max_body_len(), or that the body was
                    // truncated to fit.
                    let body_len =
                        cmp::min(cmp::max(inner_len, c.min_body_len()), c.max_body_len());
                    assert_eq!(buf.len(), c.header_len() + body_len + c.footer_len());
                    assert!(!should_fail_mtu);
                    Ok(buf)
                }
                Err((err, ser)) => {
                    // If we shouldn't fail as a result of an MTU error, we
                    // might still fail as a result of allocation.
                    assert!(should_fail_mtu || err.is_alloc());
                    // If serialization fails, the original Serializer should be
                    // unmodified.
                    assert_eq!(ser, orig);
                    Err((err, ser.into_verifying(self.truncating)))
                }
            }
        }
    }

    trait SerializerExt: Serializer {
        fn into_verifying(self, truncating: bool) -> VerifyingSerializer<Self>
        where
            Self::Buffer: ReusableBuffer,
        {
            VerifyingSerializer { ser: self, truncating }
        }

        fn encapsulate_verifying<B: PacketBuilder>(
            self,
            outer: B,
            truncating: bool,
        ) -> VerifyingSerializer<Nested<Self, B>>
        where
            Self::Buffer: ReusableBuffer,
        {
            self.encapsulate(outer).into_verifying(truncating)
        }

        fn with_mtu_verifying(
            self,
            mtu: usize,
            truncating: bool,
        ) -> VerifyingSerializer<Nested<Self, MtuPacketBuilder<()>>>
        where
            Self::Buffer: ReusableBuffer,
        {
            self.with_mtu(mtu).into_verifying(truncating)
        }
    }

    impl<S: Serializer> SerializerExt for S {}

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
    fn test_either_unwrap_success() {
        assert_eq!(Either::<u16, u32>::A(5).unwrap_a(), 5);
        assert_eq!(Either::<u16, u32>::B(10).unwrap_b(), 10);
    }

    #[test]
    #[should_panic]
    fn test_either_unwrap_a_panic() {
        let _: u16 = Either::<u16, u32>::B(10).unwrap_a();
    }

    #[test]
    #[should_panic]
    fn test_either_unwrap_b_panic() {
        let _: u32 = Either::<u16, u32>::A(5).unwrap_b();
    }

    #[test_case(Buf::new((0..100).collect(), ..); "entire buf")]
    #[test_case(Buf::new((0..100).collect(), 0..0); "empty range")]
    #[test_case(Buf::new((0..100).collect(), ..50); "prefix")]
    #[test_case(Buf::new((0..100).collect(), 50..); "suffix")]
    #[test_case(Buf::new((0..100).collect(), 25..75); "middle")]
    fn test_buf_into_inner(buf: Buf<Vec<u8>>) {
        assert_eq!(buf.clone().as_ref(), buf.into_inner());
    }

    #[test]
    fn test_packet_constraints() {
        use PacketConstraints as PC;

        // Sanity check.
        assert!(PC::try_new(0, 0, 0, 0).is_some());
        // header_len + min_body_len + footer_len doesn't overflow usize
        assert!(PC::try_new(usize::MAX / 2, usize::MAX / 2, 0, 0).is_some());
        // header_len + min_body_len + footer_len overflows usize
        assert_eq!(PC::try_new(usize::MAX, 1, 0, 0), None);
        // min_body_len > max_body_len
        assert_eq!(PC::try_new(0, 0, 1, 0), None);
    }

    #[test]
    fn test_nested_packet_builder() {
        // DummyPacketBuilder itself doesn't have any interesting logic - it
        // just calls out to PacketConstraints::new. We've already tested that
        // method in test_packet_constraints. This test instead exercises the
        // logic in Nested to make sure that PacketBuilders compose correctly.
        //
        // Each failure test case corresponds to one check in either
        // Nested::constraints or PacketConstraints::new (which is called from
        // Nested::constraints). Each test case is labeled "Test case N", and a
        // corresponding comment in either of those two functions identifies
        // which line is being tested.

        use DummyPacketBuilder as DPB;
        use PacketConstraints as PC;

        // Sanity check.
        let pb = DPB::new(10, 10, 0, usize::MAX);
        assert_eq!(
            pb.encapsulate(pb).try_constraints().unwrap(),
            PC::new(20, 20, 0, usize::MAX - 20),
        );

        // The outer PacketBuilder's minimum body length requirement of 10 is
        // more than satisfied by the inner PacketBuilder's combined 20 bytes of
        // header and footer. The resulting PacketBuilder has its minimum body
        // length requirement saturated to 0.
        let inner = DPB::new(10, 10, 0, usize::MAX);
        let outer = DPB::new(0, 0, 10, usize::MAX);
        assert_eq!(
            inner.encapsulate(outer).try_constraints().unwrap(),
            PC::new(10, 10, 0, usize::MAX - 20),
        );

        // Test case 1
        //
        // The sum of the inner and outer header lengths overflows `usize`.
        let inner = DPB::new(usize::MAX, 0, 0, usize::MAX);
        let outer = DPB::new(1, 0, 0, usize::MAX);
        assert_eq!(inner.encapsulate(outer).try_constraints(), None);

        // Test case 2
        //
        // The sum of the inner and outer footer lengths overflows `usize`.
        let inner = DPB::new(0, usize::MAX, 0, usize::MAX);
        let outer = DPB::new(0, 1, 0, usize::MAX);
        assert_eq!(inner.encapsulate(outer).try_constraints(), None);

        // Test case 3
        //
        // The sum of the resulting header, footer, and minimum body lengths
        // overflows `usize`. We use usize::MAX / 5 + 1 as the constant so that
        // none of the intermediate additions overflow, so we make sure to test
        // that an overflow in the final addition will be caught.
        let one_fifth_max = (usize::MAX / 5) + 1;
        let inner = DPB::new(one_fifth_max, one_fifth_max, one_fifth_max, usize::MAX);
        let outer = DPB::new(one_fifth_max, one_fifth_max, 0, usize::MAX);
        assert_eq!(inner.encapsulate(outer).try_constraints(), None);

        // Test case 4
        //
        // The header and footer of the inner PacketBuilder exceed the maximum
        // body length requirement of the outer PacketBuilder.
        let inner = DPB::new(10, 10, 0, usize::MAX);
        let outer = DPB::new(0, 0, 0, 10);
        assert_eq!(inner.encapsulate(outer).try_constraints(), None);

        // Test case 5
        //
        // The resulting minimum body length (thanks to the inner
        // PacketBuilder's minimum body length) is larger than the resulting
        // maximum body length.
        let inner = DPB::new(0, 0, 10, usize::MAX);
        let outer = DPB::new(0, 0, 0, 5);
        assert_eq!(inner.encapsulate(outer).try_constraints(), None);
    }

    #[test]
    fn test_inner_serializer() {
        const INNER: &[u8] = &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9];

        fn concat<'a, I: IntoIterator<Item = &'a &'a [u8]>>(slices: I) -> Vec<u8> {
            let mut v = Vec::new();
            for slc in slices.into_iter() {
                v.extend_from_slice(slc);
            }
            v
        }

        // Sanity check.
        let buf = INNER.into_serializer().serialize_vec_outer().unwrap();
        assert_eq!(buf.as_ref(), INNER);

        // A larger minimum body length requirement will cause padding to be
        // added.
        let buf = INNER
            .into_serializer()
            .into_verifying(false)
            .serialize_vec(DummyPacketBuilder::new(0, 0, 20, usize::MAX))
            .unwrap();
        assert_eq!(buf.as_ref(), concat(&[INNER, vec![0; 10].as_ref()]).as_slice());

        // Headers and footers are added as appropriate (note that
        // DummyPacketBuilder fills its header with 0xFF and its footer with
        // 0xFE).
        let buf = INNER
            .into_serializer()
            .into_verifying(false)
            .serialize_vec(DummyPacketBuilder::new(10, 10, 0, usize::MAX))
            .unwrap();
        assert_eq!(
            buf.as_ref(),
            concat(&[vec![0xFF; 10].as_ref(), INNER, vec![0xFE; 10].as_ref()]).as_slice()
        );

        // An exceeded maximum body size is rejected.
        assert_eq!(
            INNER
                .into_serializer()
                .into_verifying(false)
                .serialize_vec(DummyPacketBuilder::new(0, 0, 0, 9))
                .unwrap_err()
                .0,
            SerializeError::Mtu
        );

        // `into_serializer_with` truncates the buffer's body to zero before
        // returning, so those body bytes are not included in the serialized
        // output.
        assert_eq!(
            INNER
                .into_serializer_with(Buf::new(vec![0xFF], ..))
                .into_verifying(false)
                .serialize_vec_outer()
                .unwrap()
                .as_ref(),
            INNER
        );
    }

    #[test]
    fn test_buffer_serializer_and_inner_serializer() {
        fn verify_buffer_serializer<B: BufferMut + Debug>(
            buffer: B,
            header_len: usize,
            footer_len: usize,
            min_body_len: usize,
        ) {
            let old_body = buffer.to_flattened_vec();

            let buffer = buffer
                .serialize_vec(DummyPacketBuilder::new(
                    header_len,
                    footer_len,
                    min_body_len,
                    usize::MAX,
                ))
                .unwrap();
            verify(buffer, &old_body, header_len, footer_len, min_body_len);
        }

        fn verify_inner_packet_builder_serializer(
            body: &[u8],
            header_len: usize,
            footer_len: usize,
            min_body_len: usize,
        ) {
            let buffer = body
                .into_serializer()
                .serialize_vec(DummyPacketBuilder::new(
                    header_len,
                    footer_len,
                    min_body_len,
                    usize::MAX,
                ))
                .unwrap();
            verify(buffer, body, header_len, footer_len, min_body_len);
        }

        fn verify<B: Buffer>(
            buffer: B,
            body: &[u8],
            header_len: usize,
            footer_len: usize,
            min_body_len: usize,
        ) {
            let flat = buffer.to_flattened_vec();
            let header_bytes = &flat[..header_len];
            let body_bytes = &flat[header_len..header_len + body.len()];
            let padding_len = min_body_len.saturating_sub(body.len());
            let padding_bytes =
                &flat[header_len + body.len()..header_len + body.len() + padding_len];
            let total_body_len = body.len() + padding_len;
            let footer_bytes = &flat[header_len + total_body_len..];
            assert_eq!(
                buffer.len() - total_body_len,
                header_len + footer_len,
                "buffer.len()({}) - total_body_len({}) != header_len({}) + footer_len({})",
                buffer.len(),
                header_len,
                footer_len,
                min_body_len,
            );

            // DummyPacketBuilder fills its header with 0xFF
            assert!(
                header_bytes.iter().all(|b| *b == 0xFF),
                "header_bytes {:?} are not filled with 0xFF's",
                header_bytes,
            );
            assert_eq!(body_bytes, body);
            // Padding bytes must be initialized to zero
            assert!(
                padding_bytes.iter().all(|b| *b == 0),
                "padding_bytes {:?} are not filled with 0s",
                padding_bytes,
            );
            // DummyPacketBuilder fills its footer with 0xFE
            assert!(
                footer_bytes.iter().all(|b| *b == 0xFE),
                "footer_bytes {:?} are not filled with 0xFE's",
                footer_bytes,
            );
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
                                if range_start == 0 {
                                    // Unlike verify_buffer_serializer, this
                                    // test doesn't make use of the prefix or
                                    // suffix. In order to avoid running the
                                    // exact same test multiple times, we only
                                    // run this when `range_start == 0`, which
                                    // has the effect of reducing the number of
                                    // times that this test is run by roughly a
                                    // factor of 8.
                                    verify_inner_packet_builder_serializer(
                                        &vec.as_slice()[range_start..range_end],
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
    }

    #[test]
    fn test_min_body_len() {
        // Test that padding is added after the body of the packet whose minimum
        // body length constraint requires it. A previous version of this code
        // had a bug where padding was always added after the innermost body.

        let body = &[1, 2];

        // 4 bytes of header and footer for a total of 6 bytes (including the
        // body).
        let inner = DummyPacketBuilder::new(2, 2, 0, usize::MAX);
        // Minimum body length of 8 will require 2 bytes of padding.
        let outer = DummyPacketBuilder::new(2, 2, 8, usize::MAX);
        let buf = body
            .into_serializer()
            .into_verifying(false)
            .encapsulate_verifying(inner, false)
            .encapsulate_verifying(outer, false)
            .serialize_vec_outer()
            .unwrap();
        assert_eq!(
            buf.as_ref(),
            &[
                0xFF, 0xFF, // Outer header
                0xFF, 0xFF, // Inner header
                1, 2, // Inner body
                0xFE, 0xFE, // Inner footer
                0, 0, // Padding to satisfy outer minimum body length requirement
                0xFE, 0xFE // Outer footer
            ]
        );
    }

    #[test]
    fn test_mtu() {
        // ser is a Serializer that will consume 1 byte of buffer space
        fn test<S: Serializer + Clone + Debug + Eq>(ser: S)
        where
            S::Buffer: ReusableBuffer,
        {
            // Each of these tests encapsulates ser in a DummyPacketBuilder
            // which consumes 1 byte for the header and one byte for the footer.
            // Thus, the inner serializer will consume 1 byte, while the
            // DummyPacketBuilder will consume 2 bytes, for a total of 3 bytes.

            let pb = DummyPacketBuilder::new(1, 1, 0, usize::MAX);

            // Test that an MTU of 3 is OK. Note that this is an important test
            // since it tests the case when the MTU is exactly sufficient. A
            // previous version of this code had a bug where a packet which fit
            // the MTU exactly would be rejected.
            assert!(ser
                .clone()
                .encapsulate_verifying(pb, false)
                .with_mtu_verifying(3, false)
                .serialize_vec_outer()
                .is_ok());
            // Test that a more-than-large-enough MTU of 4 is OK.
            assert!(ser
                .clone()
                .encapsulate_verifying(pb, false)
                .with_mtu_verifying(4, false)
                .serialize_vec_outer()
                .is_ok());
            // Test that the inner MTU of 1 only applies to the inner
            // serializer, and so is still OK even though the outer serializer
            // consumes 3 bytes total.
            assert!(ser
                .clone()
                .with_mtu_verifying(1, false)
                .encapsulate_verifying(pb, false)
                .with_mtu_verifying(3, false)
                .serialize_vec_outer()
                .is_ok());
            // Test that the inner MTU of 0 is exceeded by the inner
            // serializer's 1 byte length.
            assert!(ser
                .clone()
                .with_mtu_verifying(0, false)
                .encapsulate_verifying(pb, false)
                .serialize_vec_outer()
                .is_err());
            // Test that an MTU which would be exceeded by the encapsulating
            // layer is rejected by Nested's implementation. If this doesn't
            // work properly, then the MTU should underflow, resulting in a
            // panic (see the Nested implementation of Serialize).
            assert!(ser
                .clone()
                .encapsulate_verifying(pb, false)
                .with_mtu_verifying(1, false)
                .serialize_vec_outer()
                .is_err());
        }

        // We use this as an InnerPacketBuilder which consumes 1 byte of body.
        test(DummyPacketBuilder::new(1, 0, 0, usize::MAX).into_serializer().into_verifying(false));
        test(Buf::new(vec![0], ..).into_verifying(false));
    }

    #[test]
    fn test_truncating_serializer() {
        //
        // Test truncate front.
        //

        let body = vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
        let ser =
            TruncatingSerializer::new(Buf::new(body.clone(), ..), TruncateDirection::DiscardFront)
                .into_verifying(true);
        let buf = ser.clone().with_mtu_verifying(4, true).serialize_vec_outer().unwrap();
        let buf: &[u8] = buf.as_ref();
        assert_eq!(buf, &[6, 7, 8, 9][..]);

        //
        // Test truncate back.
        //

        let ser =
            TruncatingSerializer::new(Buf::new(body.clone(), ..), TruncateDirection::DiscardBack)
                .into_verifying(true);
        let buf = ser.with_mtu_verifying(7, true).serialize_vec_outer().unwrap();
        let buf: &[u8] = buf.as_ref();
        assert_eq!(buf, &[0, 1, 2, 3, 4, 5, 6][..]);

        //
        // Test no truncating (default/original case).
        //

        let ser =
            TruncatingSerializer::new(Buf::new(body.clone(), ..), TruncateDirection::NoTruncating)
                .into_verifying(false);
        assert!(ser.clone().with_mtu_verifying(5, true).serialize_vec_outer().is_err());
        assert!(ser.with_mtu_verifying(5, true).serialize_vec_outer().is_err());

        //
        // Test that, when serialization fails, any truncation is undone.
        //

        // `ser` has a body of `[1, 2]` and no prefix or suffix
        fn test_serialization_failure<S: Serializer + Clone + Eq + Debug>(
            ser: S,
            err: SerializeError<BufferTooShortError>,
        ) where
            S::Buffer: ReusableBuffer + Debug,
        {
            // Serialize with a PacketBuilder with an MTU of 1 so that the body
            // (of length 2) is too large. If `ser` is configured not to
            // truncate, it should result in an MTU error. If it is configured
            // to truncate, the 2 + 2 = 4 combined bytes of header and footer
            // will cause allocating a new buffer to fail, and it should result
            // in an allocation failure. Even if the body was truncated, it
            // should be returned to its original un-truncated state before
            // being returned from `serialize`.
            let (e, new_ser) =
                ser.clone().serialize_no_alloc(DummyPacketBuilder::new(2, 2, 0, 1)).unwrap_err();
            assert_eq!(err, e);
            assert_eq!(new_ser, ser);
        }

        let body = Buf::new(vec![1, 2], ..);
        test_serialization_failure(
            TruncatingSerializer::new(body.clone(), TruncateDirection::DiscardFront)
                .into_verifying(true),
            SerializeError::Alloc(BufferTooShortError),
        );
        test_serialization_failure(
            TruncatingSerializer::new(body.clone(), TruncateDirection::DiscardFront)
                .into_verifying(true),
            SerializeError::Alloc(BufferTooShortError),
        );
        test_serialization_failure(
            TruncatingSerializer::new(body.clone(), TruncateDirection::NoTruncating)
                .into_verifying(false),
            SerializeError::Mtu,
        );
    }

    #[test]
    fn test_try_reuse_buffer() {
        fn test_expect_success(
            body_range: Range<usize>,
            prefix: usize,
            suffix: usize,
            max_copy_bytes: usize,
        ) {
            let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
            let buffer = Buf::new(&mut bytes[..], body_range);
            let body = buffer.as_ref().to_vec();
            let buffer = try_reuse_buffer(buffer, prefix, suffix, max_copy_bytes).unwrap();
            assert_eq!(buffer.as_ref(), body.as_slice());
            assert!(buffer.prefix_len() >= prefix);
            assert!(buffer.suffix_len() >= suffix);
        }

        fn test_expect_failure(
            body_range: Range<usize>,
            prefix: usize,
            suffix: usize,
            max_copy_bytes: usize,
        ) {
            let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
            let buffer = Buf::new(&mut bytes[..], body_range.clone());
            let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
            let orig = Buf::new(&mut bytes[..], body_range.clone());
            let buffer = try_reuse_buffer(buffer, prefix, suffix, max_copy_bytes).unwrap_err();
            assert_eq!(buffer, orig);
        }

        // No prefix or suffix trivially succeeds.
        test_expect_success(0..10, 0, 0, 0);
        // If we have enough prefix/suffix, it succeeds.
        test_expect_success(1..9, 1, 1, 0);
        // If we don't have enough prefix/suffix, but we have enough capacity to
        // move the buffer within the body, it succeeds...
        test_expect_success(0..9, 1, 0, 9);
        test_expect_success(1..10, 0, 1, 9);
        // ...but if we don't provide a large enough max_copy_bytes, it will fail.
        test_expect_failure(0..9, 1, 0, 8);
        test_expect_failure(1..10, 0, 1, 8);
    }

    #[test]
    fn test_buffer_alloc_buffer_provider() {
        // Test that the blanket impl of `BufferProvider` for `A: BufferAlloc`
        // works as expected, returning Either::A when reusing is possible, and
        // returning Either::B when realloc'ing is needed.

        fn test_expect(body_range: Range<usize>, prefix: usize, suffix: usize, expect_a: bool) {
            let mut bytes = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];
            let buffer = Buf::new(&mut bytes[..], body_range);
            let body = buffer.as_ref().to_vec();
            let buffer =
                BufferProvider::reuse_or_realloc(new_buf_vec, buffer, prefix, suffix).unwrap();
            match &buffer {
                Either::A(_) if expect_a => {}
                Either::B(_) if !expect_a => {}
                Either::A(_) => panic!("expected Eitehr::B variant"),
                Either::B(_) => panic!("expected Eitehr::A variant"),
            }
            let bytes: &[u8] = buffer.as_ref();
            assert_eq!(bytes, body.as_slice());
            assert!(buffer.prefix_len() >= prefix);
            assert!(buffer.suffix_len() >= suffix);
        }

        // Expect that we'll be able to reuse the existing buffer.
        fn test_expect_reuse(body_range: Range<usize>, prefix: usize, suffix: usize) {
            test_expect(body_range, prefix, suffix, true);
        }

        // Expect that we'll need to allocate a new buffer.
        fn test_expect_realloc(body_range: Range<usize>, prefix: usize, suffix: usize) {
            test_expect(body_range, prefix, suffix, false);
        }

        // No prefix or suffix trivially succeeds.
        test_expect_reuse(0..10, 0, 0);
        // If we have enough prefix/suffix, it succeeds.
        test_expect_reuse(1..9, 1, 1);
        // If we don't have enough prefix/suffix, but we have enough capacity to
        // move the buffer within the body, it succeeds.
        test_expect_reuse(0..9, 1, 0);
        test_expect_reuse(1..10, 0, 1);
        // If we don't have enough capacity, it fails and must realloc.
        test_expect_realloc(0..9, 1, 1);
        test_expect_realloc(1..10, 1, 1);
    }

    /// Simple Vec-backed buffer to test fragmented buffers implementation.
    ///
    /// `ScatterGatherBuf` keeps:
    /// - an inner buffer `inner`, which is always part of its body.
    /// - extra backing memory in `data`.
    ///
    /// `data` has two "root" regions, marked by the midpoint `mid`. Everything
    /// left of `mid` is this buffer's prefix, and after `mid` is this buffer's
    /// suffix.
    ///
    /// The `range` field keeps the range in `data` that contains *filled*
    /// prefix and suffix information. `range.start` is always less than or
    /// equal to `mid` and `range.end` is always greater than or equal to `mid`,
    /// such that growing the front of the buffer means decrementing
    /// `range.start` and growing the back of the buffer means incrementing
    /// `range.end`.
    ///
    ///  At any time this buffer's parts are:
    /// - Free prefix data in range `0..range.start`.
    /// - Used prefix data (now part of body) in range `range.start..mid`.
    /// - Inner buffer body in `inner`.
    /// - Used suffix data (now part of body) in range `mid..range.end`.
    /// - Free suffix data in range `range.end..`
    struct ScatterGatherBuf<B> {
        data: Vec<u8>,
        mid: usize,
        range: Range<usize>,
        inner: B,
    }

    impl<B: BufferMut> FragmentedBuffer for ScatterGatherBuf<B> {
        fn len(&self) -> usize {
            self.inner.len() + (self.range.end - self.range.start)
        }

        fn with_bytes<R, F>(&self, f: F) -> R
        where
            F: for<'a, 'b> FnOnce(FragmentedBytes<'a, 'b>) -> R,
        {
            let (_, rest) = self.data.split_at(self.range.start);
            let (prefix_b, rest) = rest.split_at(self.mid - self.range.start);
            let (suffix_b, _) = rest.split_at(self.range.end - self.mid);
            let mut bytes = [prefix_b, self.inner.as_ref(), suffix_b];
            f(FragmentedBytes::new(&mut bytes[..]))
        }
    }

    impl<B: BufferMut> FragmentedBufferMut for ScatterGatherBuf<B> {
        fn with_bytes_mut<R, F>(&mut self, f: F) -> R
        where
            F: for<'a, 'b> FnOnce(FragmentedBytesMut<'a, 'b>) -> R,
        {
            let (_, rest) = self.data.split_at_mut(self.range.start);
            let (prefix_b, rest) = rest.split_at_mut(self.mid - self.range.start);
            let (suffix_b, _) = rest.split_at_mut(self.range.end - self.mid);
            let mut bytes = [prefix_b, self.inner.as_mut(), suffix_b];
            f(FragmentedBytesMut::new(&mut bytes[..]))
        }
    }

    impl<B: BufferMut> GrowBuffer for ScatterGatherBuf<B> {
        fn prefix_len(&self) -> usize {
            self.range.start
        }

        fn suffix_len(&self) -> usize {
            self.data.len() - self.range.end
        }

        fn grow_front(&mut self, n: usize) {
            self.range.start -= n;
        }

        fn grow_back(&mut self, n: usize) {
            self.range.end += n;
            assert!(self.range.end <= self.data.len());
        }
    }

    impl<B: BufferMut> TargetBuffer for ScatterGatherBuf<B> {
        fn with_parts<O, F>(&mut self, f: F) -> O
        where
            F: for<'a, 'b> FnOnce(&'a mut [u8], FragmentedBytesMut<'a, 'b>, &'a mut [u8]) -> O,
        {
            let (prefix, rest) = self.data.split_at_mut(self.range.start);
            let (prefix_b, rest) = rest.split_at_mut(self.mid - self.range.start);
            let (suffix_b, suffix) = rest.split_at_mut(self.range.end - self.mid);
            let mut bytes = [prefix_b, self.inner.as_mut(), suffix_b];
            f(prefix, bytes.as_fragmented_byte_slice(), suffix)
        }
    }

    struct ScatterGatherProvider;

    impl<B: BufferMut> BufferProvider<B, ScatterGatherBuf<B>> for ScatterGatherProvider {
        type Error = Never;

        fn reuse_or_realloc(
            self,
            buffer: B,
            prefix: usize,
            suffix: usize,
        ) -> Result<ScatterGatherBuf<B>, (Self::Error, B)> {
            let inner = buffer;
            let data = vec![0; prefix + suffix];
            let range = Range { start: prefix, end: prefix };
            let mid = prefix;
            Ok(ScatterGatherBuf { inner, data, range, mid })
        }
    }

    #[test]
    fn test_scatter_gather_serialize() {
        // Assert that a buffer composed of different allocations can be used as
        // a serialization target, while reusing an internal body buffer.
        let buf = Buf::new(vec![10, 20, 30, 40, 50], ..);
        let pb = DummyPacketBuilder::new(3, 2, 0, usize::MAX);
        let ser = buf.encapsulate(pb);
        let result = ser.serialize_outer(ScatterGatherProvider {}).unwrap();
        let flattened = result.to_flattened_vec();
        assert_eq!(&flattened[..], &[0xFF, 0xFF, 0xFF, 10, 20, 30, 40, 50, 0xFE, 0xFE]);
    }
}
