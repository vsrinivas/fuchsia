// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Serialization.
use std::cmp;
use std::ops::{Range, RangeBounds};

use crate::{
    canonicalize_range, take_back, take_back_mut, take_front, take_front_mut, Buffer, BufferMut,
    BufferView, BufferViewMut, ParsablePacket, ParseBuffer, ParseBufferMut, SerializeBuffer,
};

/// Either of two buffers.
///
/// An `Either` wraps one of two different buffer types. It implements all of
/// the relevant traits by calling the corresponding methods on the wrapped
/// buffer.
pub enum Either<A, B> {
    A(A),
    B(B),
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

    // in a separate method so it can be used in testing
    pub(crate) fn buffer_view(&mut self) -> BufView {
        BufView { buf: &self.buf.as_ref()[self.range.clone()], range: &mut self.range }
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
        P::parse(
            BufViewMut { buf: &mut self.buf.as_mut()[self.range.clone()], range: &mut self.range },
            args,
        )
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

// used in testing in a different module
pub(crate) struct BufView<'a> {
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

struct BufViewMut<'a> {
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
    fn header_len(&self) -> usize;
    /// The minimum size of body required by this packet.
    fn min_body_len(&self) -> usize;
    /// The number of bytes in this packet's footer.
    fn footer_len(&self) -> usize;

    /// Serializes this packet into an existing buffer.
    ///
    /// `serialize` is called with a [`SerializeBuffer`] which provides access
    /// to the parts of a buffer corresponding to the header, body, and footer
    /// of this packet. The header and footer bytes will be uninitialized, and
    /// will have the number of bytes specified by the `header_len` and
    /// `footer_len` methods. The body will be initialized with the contents of
    /// the packet to be encapsulated, and will have at least `min_body_len`
    /// bytes (padding may have been added in order to satisfy this minimum).
    /// `serialize` is responsible for serializing the header and footer into
    /// the appropriate sections of the buffer.
    ///
    /// # Security
    ///
    /// All of the bytes of the header and footer should be initialized, even if
    /// only to zero, in order to avoid leaking the contents of packets
    /// previously stored in the same buffer.
    fn serialize(self, buffer: SerializeBuffer);
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

/// Constraints passed to [`Serializer::serialize`].
///
/// `SerializeConstraints` describes the prefix length, minimum body length, and
/// suffix length required of the buffer returned from a call to `serialize`.
#[derive(Copy, Clone)]
pub struct SerializeConstraints {
    pub prefix_len: usize,
    pub min_body_len: usize,
    pub suffix_len: usize,
}

pub trait Serializer: Sized {
    /// The type of buffers returned from a call to `serialize`.
    type Buffer: BufferMut;

    /// Serialize this `Serializer`, producing a buffer.
    ///
    /// `serialize` accepts a set of constraints, and produces a buffer which
    /// satisfies them. In particular, the returned buffer must:
    /// - Have at least `c.prefix_len` bytes of prefix
    /// - Have at least `c.min_body_len` bytes of body, initialized to the
    ///   contents of the packet described by this `Serializer`
    /// - Have at least `c.suffix_len` bytes of suffix
    fn serialize(self, c: SerializeConstraints) -> Self::Buffer;

    /// Serialize this `Serializer` as the outermost packet.
    ///
    /// `serialize_outer` is like `serialize`, except that it is called when
    /// this `Serializer` describes the outermost packet, not encapsulated in
    /// any other packets. It is equivalent to calling `serialize` with a
    /// `SerializationConstraints` of all zeros.
    fn serialize_outer(self) -> Self::Buffer {
        self.serialize(SerializeConstraints { prefix_len: 0, min_body_len: 0, suffix_len: 0 })
    }

    /// Serialize this `Serializer` with a maximum transmission unit (MTU)
    /// constraint.
    ///
    /// `serialize_mtu` is like `serialize`, except that it accepts an MTU
    /// constraint. If the total length of the buffer that would be produced
    /// (including prefix and suffix length) would exceed the MTU, then an error
    /// is returned instead of a buffer.
    fn serialize_mtu(self, mtu: usize, c: SerializeConstraints) -> Result<Self::Buffer, Self>;

    /// Serialize this `Serializer` with a maximum transmission unit (MTU)
    /// constraint as the outermost packet.
    ///
    /// `serialize_mtu_outer` is like `serialize_mtu`, except that it is called
    /// when this `Serializer` describes the outermost packet, not encapsulated
    /// in any other packets. It is equivalent to calling `serialize_mtu` with a
    /// `SerializationConstraints` of all zeros.
    fn serialize_mtu_outer(self, mtu: usize) -> Result<Self::Buffer, Self> {
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
}

// TODO(joshlf): Once impl specialization is stable, make this a default impl,
// and add a specializing impl for 'B: Buffer'.

fn inner_packet_builder_serializer_total_len_body_range<B: InnerPacketBuilder>(
    builder: &B,
    c: SerializeConstraints,
) -> (usize, Range<usize>) {
    let body_bytes = cmp::max(c.min_body_len, builder.bytes_len());
    let total_len = c.prefix_len + body_bytes + c.suffix_len;
    let body_range = c.prefix_len..(c.prefix_len + builder.bytes_len());
    (total_len, body_range)
}

impl<B: InnerPacketBuilder> Serializer for B {
    type Buffer = Buf<Vec<u8>>;

    fn serialize_mtu(self, mtu: usize, c: SerializeConstraints) -> Result<Self::Buffer, B> {
        let (total_len, body_range) =
            inner_packet_builder_serializer_total_len_body_range(&self, c);

        if total_len <= mtu {
            let mut buf = vec![0; total_len];
            <Self as InnerPacketBuilder>::serialize(self, &mut buf[body_range.clone()]);
            Ok(Buf::new(buf, body_range))
        } else {
            Err(self)
        }
    }

    fn serialize(self, c: SerializeConstraints) -> Buf<Vec<u8>> {
        let (total_len, body_range) =
            inner_packet_builder_serializer_total_len_body_range(&self, c);

        let mut buf = vec![0; total_len];
        <Self as InnerPacketBuilder>::serialize(self, &mut buf[body_range.clone()]);
        Buf::new(buf, body_range)
    }
}

/// A [`Serializer`] for inner packets, wrapping an [`InnerPacketBuilder`] and a
/// buffer.
///
/// An `InnerSerializer` implements `Serializer` for an `InnerPacketBuilder`. It
/// stores a buffer for the fast path and, in case that buffer does not satisfy
/// the constraints passed to [`Serializer::serialize`], it stores a function
/// capable of producing a buffer which does.
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
    ) -> InnerSerializer<B, Buf, impl FnOnce(usize) -> crate::Buf<Vec<u8>>> {
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

    fn serialize_mtu(self, mtu: usize, c: SerializeConstraints) -> Result<Self::Buffer, Self> {
        let InnerSerializer { builder, buf, get_buf } = self;
        let total_len = c.prefix_len + cmp::max(c.min_body_len, builder.bytes_len()) + c.suffix_len;

        if total_len <= mtu {
            Ok(Self::serialize_inner(builder, buf, get_buf, c.prefix_len, total_len))
        } else {
            Err(InnerSerializer { builder, buf, get_buf })
        }
    }

    fn serialize(self, c: SerializeConstraints) -> Either<Buf, O> {
        let InnerSerializer { builder, buf, get_buf } = self;
        let total_len = c.prefix_len + cmp::max(c.min_body_len, builder.bytes_len()) + c.suffix_len;
        Self::serialize_inner(builder, buf, get_buf, c.prefix_len, total_len)
    }
}

/// A [`Serializer`] for inner packets, wrapping an `InnerPacketBuilder`.
///
/// An `FnSerializer` implements `Serializer` for an [`InnerPacketBuilder`]. It
/// stores a function which is called to produce a buffer. This function is used
/// to implement [`Serializer::serialize`].
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
    pub fn new_vec(builder: B) -> FnSerializer<B, impl FnOnce(usize) -> Buf<Vec<u8>>> {
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

    fn serialize_mtu(self, mtu: usize, c: SerializeConstraints) -> Result<Self::Buffer, Self> {
        let FnSerializer { builder, get_buf } = self;
        let total_len = c.prefix_len + cmp::max(c.min_body_len, builder.bytes_len()) + c.suffix_len;

        if total_len <= mtu {
            let mut buf = get_buf(total_len);
            buf.shrink(c.prefix_len..(c.prefix_len + builder.bytes_len()));
            builder.serialize(buf.as_mut());
            Ok(buf)
        } else {
            Err(FnSerializer { builder, get_buf })
        }
    }

    fn serialize(self, c: SerializeConstraints) -> O {
        let FnSerializer { builder, get_buf } = self;
        let total_len = c.prefix_len + cmp::max(c.min_body_len, builder.bytes_len()) + c.suffix_len;

        let mut buf = get_buf(total_len);
        buf.shrink(c.prefix_len..(c.prefix_len + builder.bytes_len()));
        builder.serialize(buf.as_mut());
        buf
    }
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
pub struct BufferSerializer<B, F> {
    buf: B,
    get_buf: F,
}

impl<B, F> BufferSerializer<B, F> {
    /// Constructs a new `BufferSerializer`.
    ///
    /// `new` accepts a buffer and a `get_buf` function, which produces a
    /// buffer. When `serialize` is called, if the existing buffer satisfies the
    /// constraints, it is returned. Otherwise, `get_buf` is used to produce a
    /// buffer which satisfies the constraints, and the body is copied from the
    /// original buffer into the new one before it is returned.
    ///
    /// `get_buf` accepts a `usize`, and produces a buffer of that length.
    pub fn new(buffer: B, get_buf: F) -> BufferSerializer<B, F> {
        BufferSerializer { buf: buffer, get_buf }
    }
}

// See comment on InnerSerializer for why we have this impl block.

impl<B> BufferSerializer<B, ()> {
    /// Constructs a new `BufferSerializer` which allocates a new `Vec` in the
    /// fallback path.
    ///
    /// `new_vec` is like `new`, except that its `get_buf` function is
    /// automatically set to allocate a new `Vec` on the heap.
    pub fn new_vec(buffer: B) -> BufferSerializer<B, impl FnOnce(usize) -> Buf<Vec<u8>>> {
        BufferSerializer::new(buffer, |n| Buf::new(vec![0; n], ..))
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

impl<B: BufferMut, O: BufferMut, F: FnOnce(usize) -> O> Serializer for BufferSerializer<B, F> {
    type Buffer = Either<B, O>;

    fn serialize_mtu(self, mtu: usize, c: SerializeConstraints) -> Result<Self::Buffer, Self> {
        let BufferSerializer { buf, get_buf } = self;
        let body_and_padding = cmp::max(c.min_body_len, buf.len());
        let total_len = c.prefix_len + body_and_padding + c.suffix_len;

        if total_len <= mtu {
            Ok(Self::serialize_inner(buf, get_buf, c, body_and_padding, total_len))
        } else {
            Err(BufferSerializer { buf, get_buf })
        }
    }

    fn serialize(self, c: SerializeConstraints) -> Either<B, O> {
        let BufferSerializer { buf, get_buf } = self;
        let body_and_padding = cmp::max(c.min_body_len, buf.len());
        let total_len = c.prefix_len + body_and_padding + c.suffix_len;
        Self::serialize_inner(buf, get_buf, c, body_and_padding, total_len)
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
pub struct EncapsulatingSerializer<B: PacketBuilder, S: Serializer> {
    builder: B,
    inner: S,
}

impl<B: PacketBuilder, S: Serializer> EncapsulatingSerializer<B, S> {
    // The minimum body required of the encapsulated serializer.
    fn min_body(&self, min_body: usize) -> usize {
        // The number required by this layer.
        let this_min_body = self.builder.min_body_len();
        // The number required by the next outer layer, taking into account that
        // header_len + footer_len will be consumed by this layer.
        let next_min_body = min_body
            .checked_sub(self.builder.header_len() + self.builder.footer_len())
            .unwrap_or(0);
        cmp::max(this_min_body, next_min_body)
    }
}

impl<B: PacketBuilder, S: Serializer> Serializer for EncapsulatingSerializer<B, S> {
    type Buffer = S::Buffer;

    fn serialize_mtu(self, mtu: usize, mut c: SerializeConstraints) -> Result<Self::Buffer, Self> {
        c.prefix_len += self.builder.header_len();
        c.suffix_len += self.builder.footer_len();
        c.min_body_len = self.min_body(c.min_body_len);

        let EncapsulatingSerializer { builder, inner } = self;
        match inner.serialize_mtu(mtu, c) {
            Ok(mut buffer) => {
                buffer.serialize(builder);
                Ok(buffer)
            }
            Err(inner) => Err(EncapsulatingSerializer { builder, inner }),
        }
    }

    fn serialize(self, mut c: SerializeConstraints) -> Self::Buffer {
        c.prefix_len += self.builder.header_len();
        c.suffix_len += self.builder.footer_len();
        c.min_body_len = self.min_body(c.min_body_len);

        let EncapsulatingSerializer { builder, inner } = self;
        let mut buffer = inner.serialize(c);
        buffer.serialize(builder);
        buffer
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_buffer_serializer_and_inner_serializer() {
        fn verify_buffer_serializer<B: BufferMut>(
            buffer: B,
            prefix_len: usize,
            suffix_len: usize,
            min_body_len: usize,
        ) {
            let old_len = buffer.len();
            let mut old_body = Vec::with_capacity(old_len);
            old_body.extend_from_slice(buffer.as_ref());

            let buffer = BufferSerializer::new_vec(buffer).serialize(SerializeConstraints {
                prefix_len,
                suffix_len,
                min_body_len,
            });
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
                .serialize(SerializeConstraints { prefix_len, suffix_len, min_body_len });
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
}
