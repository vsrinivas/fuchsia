// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for parsing and serializing sequential records.
//!
//! This module provides utilities for parsing and serializing repeated,
//! sequential records. Examples of packet formats which include repeated,
//! sequential records include IPv4, IPv6, TCP, NDP, and IGMP.
//!
//! The utilities in this module are very flexible and generic. The user must
//! supply a number of details about the format in order for parsing and
//! serializing to work. See the TODO trait for more details.
//!
//! Some packet formats use a [type-length-value]-like encoding for options.
//! Examples include IPv4, TCP, and NDP options. Special support for these
//! formats is provided by the [`options`] submodule.
//!
//! [`options`]: crate::records::options
//! [type-length-value]: https://en.wikipedia.org/wiki/Type-length-value

use core::marker::PhantomData;
use core::ops::Deref;

use zerocopy::ByteSlice;

use crate::serialize::InnerPacketBuilder;
use crate::util::{FromRaw, MaybeParsed};
use crate::{BufferView, BufferViewMut};

/// A parsed sequence of records.
///
/// `Records` represents a pre-parsed sequence of records whose structure is
/// enforced by the impl in `R`.
#[derive(Debug)]
pub struct Records<B, R: RecordsImplLayout> {
    bytes: B,
    context: R::Context,
}

/// An unchecked sequence of records.
///
/// `RecordsRaw` represents a not-yet-parsed and not-yet-validated sequence of
/// records, whose structure is enforced by the impl in `R`.
///
/// [`Records`] provides an implementation of [`FromRaw`] that can be used to
/// validate a `RecordsRaw`.
pub struct RecordsRaw<B, R: RecordsImplLayout> {
    bytes: B,
    context: R::Context,
}

impl<B, R> RecordsRaw<B, R>
where
    R: RecordsImplLayout<Context = ()>,
{
    /// Creates a new `RecordsRaw` with the data in `bytes`.
    pub fn new(bytes: B) -> Self {
        Self { bytes, context: () }
    }
}

impl<B, R> RecordsRaw<B, R>
where
    R: for<'a> RecordsRawImpl<'a>,
    B: ByteSlice,
{
    /// Raw parse a sequence of records with a context.
    ///
    /// See [`RecordsRaw::parse_raw_with_mut_context`] for details on `bytes`,
    /// `context`, and return value. `parse_raw_with_context` just calls
    /// `parse_raw_with_mut_context` with a mutable reference to the `context`
    /// which is passed by value to this function.
    pub fn parse_raw_with_context<BV: BufferView<B>>(
        bytes: &mut BV,
        mut context: R::Context,
    ) -> MaybeParsed<Self, (B, R::Error)> {
        Self::parse_raw_with_mut_context(bytes, &mut context)
    }

    /// Raw parse a sequence of records with a mutable context.
    ///
    /// `parse_raw_with_mut_context` shallowly parses `bytes` as a sequence of
    /// records. `context` may be used by implementers to maintain state.
    ///
    /// `parse_raw_with_mut_context` performs a single pass over all of the
    /// records to be able to find the end of the records list and update
    /// `bytes` accordingly. Upon return, `bytes` is moved to the first byte
    /// after the records list (if the return is a [`MaybeParsed::Complete`],
    /// otherwise `bytes` will be at the point where raw parsing error was
    /// found.
    pub fn parse_raw_with_mut_context<BV: BufferView<B>>(
        bytes: &mut BV,
        context: &mut R::Context,
    ) -> MaybeParsed<Self, (B, R::Error)> {
        let c = context.clone();
        let mut b = LongLivedBuff::new(bytes.as_ref());
        let r = loop {
            match R::parse_raw_with_context(&mut b, context) {
                Ok(true) => {} // continue consuming from data
                Ok(false) => {
                    break None;
                }
                Err(e) => {
                    break Some(e);
                }
            }
        };

        // When we get here, we know that whatever is left in `b` is not needed
        // so we only take the amount of bytes we actually need from `bytes`,
        // leaving the rest alone for the caller to continue parsing with.
        let bytes_len = bytes.len();
        let b_len = b.len();
        let taken = bytes.take_front(bytes_len - b_len).unwrap();

        match r {
            Some(error) => MaybeParsed::Incomplete((taken, error)),
            None => MaybeParsed::Complete(RecordsRaw { bytes: taken, context: c }),
        }
    }

    /// Raw parses a sequence of records.
    ///
    /// Equivalent to calling [`RecordsRaw::parse_raw_with_context`] with
    /// `context = ()`.
    pub fn parse_raw<BV: BufferView<B>>(bytes: &mut BV) -> MaybeParsed<Self, (B, R::Error)>
    where
        R: RecordsImplLayout<Context = ()>,
    {
        Self::parse_raw_with_context(bytes, ())
    }
}

impl<B, R> Deref for RecordsRaw<B, R>
where
    B: ByteSlice,
    R: RecordsImplLayout,
{
    type Target = [u8];

    fn deref(&self) -> &[u8] {
        self.bytes.deref()
    }
}

/// An iterator over the records contained inside a [`Records`] instance.
pub struct RecordsIter<'a, R: RecordsImpl<'a>> {
    bytes: &'a [u8],
    context: R::Context,
}

/// The context kept while performing records parsing.
///
/// Types which implement `RecordsContext` can be used as the long-lived context
/// which is kept during records parsing. This context allows parsers to keep
/// running computations over the span of multiple records.
pub trait RecordsContext: Sized + Clone {
    /// Clone a context for iterator purposes.
    ///
    /// `clone_for_iter` is useful for cloning a context to be used by
    /// `RecordsIter`. Since `Records::parse_with_context` will do a full pass
    /// over all the records to check for errors, a `RecordsIter` should never
    /// error. Therefore, instead of doing checks when iterating (if a context
    /// was used for checks), a clone of a context can be made specifically for
    /// iterator purposes that does not do checks (which may be expensive).
    ///
    /// The default implementation of this method is equivalent to
    /// `self.clone()`.
    fn clone_for_iter(&self) -> Self {
        self.clone()
    }
}

// Implement the `RecordsContext` trait for `usize` which will be used by record
// limiting contexts (see [`LimitedRecordsImpl`]) and for `()` which is to
// represent an empty/no context-type.
impl RecordsContext for usize {}
impl RecordsContext for () {}

/// Basic associated types used by a [`RecordsImpl`].
///
/// This trait is kept separate from `RecordsImpl` so that the associated types
/// do not depend on the lifetime parameter to `RecordsImpl`.
pub trait RecordsImplLayout {
    // TODO(joshlf): Once associated type defaults are stable, give the
    // `Context` type a default of `()`.

    /// A context type that can be used to maintain state while parsing multiple
    /// records.
    type Context: RecordsContext;

    /// The type of errors that may be returned by a call to
    /// [`RecordsImpl::parse_with_context`].
    type Error;
}

/// An implementation of a records parser.
///
/// `RecordsImpl` provides functions to parse sequential records. It is required
///  in order to construct a [`Records`] or [`RecordsIter`].
pub trait RecordsImpl<'a>: RecordsImplLayout {
    /// The type of a single record; the output from the [`parse_with_context`]
    /// function.
    ///
    /// For long or variable-length data, implementers are advised to make
    /// `Record` a reference into the bytes passed to `parse_with_context`. Such
    /// a reference will need to carry the lifetime `'a`, which is the same
    /// lifetime that is passed to `parse_with_context`, and is also the
    /// lifetime parameter to this trait.
    ///
    /// [`parse_with_context`]: crate::records::RecordsImpl::parse_with_context
    type Record;

    /// Parses a record with some context.
    ///
    /// `parse_with_context` takes a variable-length `data` and a `context` to
    /// maintain state, and returns `Ok(Some(Some(o)))` if the record is
    /// successfully parsed as `o`, `Ok(Some(None))` if the record is
    /// well-formed but the implementer can't extract a concrete object (e.g.
    /// the record is an unimplemented enumeration, but it can be safely
    /// "skipped"), `Ok(None)` if `parse_with_context` is unable to parse more
    /// records, and `Err(err)` if the `data` is malformed for the attempted
    /// record parsing.
    ///
    /// `data` may be empty. It is up to the implementer to handle an exhausted
    /// `data`.
    ///
    /// When returning `Ok(Some(None))` it's the implementer's responsibility to
    /// consume the bytes of the record from `data`. If this doesn't happen,
    /// then `parse_with_context` will be called repeatedly on the same `data`,
    /// and the program will be stuck in an infinite loop. If the implementation
    /// is unable to know how many bytes to consume from `data` in order to skip
    /// the record, `parse_with_context` must return `Err`.
    ///
    /// `parse_with_context` must be deterministic, or else
    /// [`Records::parse_with_context`] cannot guarantee that future iterations
    /// will not produce errors (and panic).
    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> Result<Option<Option<Self::Record>>, Self::Error>;
}

/// An implementation of a raw records parser.
///
/// `RecordsRawImpl` provides functions to raw-parse sequential records. It is
/// required to construct a partially-parsed [`RecordsRaw`].
///
/// `RecordsRawImpl` is meant to perform little or no validation on each record
/// it consumes. It is primarily used to be able to walk record sequences with
/// unknown lengths.
pub trait RecordsRawImpl<'a>: RecordsImplLayout {
    /// Raw-parses a single record with some context.
    ///
    /// `parse_raw_with_context` takes a variable length `data` and a `context`
    /// to maintain state, and returns `Ok(true)` if a record is successfully
    /// consumed, `Ok(false)` if it is unable to parse more records, and
    /// `Err(err)` if the `data` is malformed in any way.
    ///
    /// `data` may be empty. It is up to the implementer to handle an exhausted
    /// `data`.
    ///
    /// It's the implementer's responsibility to consume exactly one record from
    /// `data` when returning `Ok(_)`.
    fn parse_raw_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> Result<bool, Self::Error>;
}

/// A limited, parsed sequence of records.
///
/// `LimitedRecords` represents a parsed sequence of records that can be limited
/// to a certain number of records. Unlike [`Records`], which accepts a
/// [`RecordsImpl`], `LimitedRecords` accepts a type that implements
/// [`LimitedRecordsImpl`].
pub type LimitedRecords<B, R> = Records<B, LimitedRecordsImplBridge<R>>;

/// Create a bridge to `RecordsImplLayout` and `RecordsImpl` from an `R` that
/// implements `LimitedRecordsImplLayout`.
///
/// The obvious solution to this problem would be to define `LimitedRecords` as
/// follows, along with the following blanket impls:
///
/// ```rust,ignore
/// pub type LimitedRecords<B, R> = Records<B, R>;
///
/// impl<R: LimitedRecordsImplLayout> RecordsImplLayout for R { ... }
///
/// impl<'a, R: LimitedRecordsImpl<'a>> RecordsImpl<'a> for R { ... }
/// ```
///
/// Unfortunately, we also provide a similar type alias in the `options` module,
/// defining options parsing in terms of records parsing. If we were to provide
/// both these blanket impls and the similar blanket impls in terms of
/// `OptionsImplLayout` and `OptionsImpl`, we would have conflicting blanket
/// impls. Instead, we wrap the `LimitedRecordsImpl` type in a
/// `LimitedRecordsImplBridge` in order to make it a distinct concrete type and
/// avoid the conflicting blanket impls problem.
///
/// Note that we could theoretically provide the blanket impl here and only use
/// the newtype trick in the `options` module (or vice-versa), but that would
/// just result in more patterns to keep track of.
///
/// `LimitedRecordsImplBridge` is `#[doc(hidden)]`; it is only `pub` because it
/// appears in the type alias `LimitedRecords`.
#[derive(Debug)]
#[doc(hidden)]
pub struct LimitedRecordsImplBridge<O>(PhantomData<O>);

impl<R: LimitedRecordsImplLayout> RecordsImplLayout for LimitedRecordsImplBridge<R> {
    /// All `LimitedRecords` get a context type of usize.
    type Context = usize;
    type Error = R::Error;
}

impl<'a, R: LimitedRecordsImpl<'a>> RecordsImpl<'a> for LimitedRecordsImplBridge<R> {
    type Record = R::Record;

    /// Parse some bytes with a given `context` as a limit.
    ///
    /// `parse_with_context` accepts a `bytes` buffer and limit `context` and
    /// verifies that the limit has not been reached and that `bytes` is not
    /// empty. See [`EXACT_LIMIT_ERROR`] for information about exact limiting.
    /// If the limit has not been reached and `bytes` has not been exhausted,
    /// `LimitedRecordsImpl::parse` will be called to do the actual parsing of a
    /// record.
    ///
    /// [`EXACT_LIMIT_ERROR`]: LimitedRecordsImplLayout::EXACT_LIMIT_ERROR
    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        bytes: &mut BV,
        context: &mut Self::Context,
    ) -> Result<Option<Option<Self::Record>>, Self::Error> {
        let limit_hit = *context == 0;

        if bytes.is_empty() || limit_hit {
            return match R::EXACT_LIMIT_ERROR {
                Some(_) if bytes.is_empty() ^ limit_hit => Err(R::EXACT_LIMIT_ERROR.unwrap()),
                _ => Ok(None),
            };
        }

        *context = context.checked_sub(1).expect("Can't decrement counter below 0");

        R::parse(bytes)
    }
}

/// A records implementation that limits the number of records read from a
/// buffer.
///
/// Some protocol formats encode the number of records that appear in a given
/// sequence, for example in a header preceding the records (as in IGMP).
/// `LimitedRecordsImplLayout` is like [`RecordsImplLayout`], except that it
/// imposes a limit on the number of records that may be present. If more are
/// present than expected, a parsing error is returned.
///
/// By default, the limit is used as an upper bound - fewer records than the
/// limit may be present. This behavior may be overridden by setting the
/// associated [`EXACT_LIMIT_ERROR`] constant to `Some(e)`. In that case, if the
/// buffer is exhausted before the limit is reached, the error `e` will be
/// returned.
///
/// Note that an implementation, `R`, of `LimitedRecordsImpl` cannot be used as
/// the [`RecordsImpl`] type parameter to [`Records`] (ie, `Records<_, R>`).
/// Instead, use [`LimitedRecords<_, R>`].
///
/// [`EXACT_LIMIT_ERROR`]: crate::records::LimitedRecordsImplLayout::EXACT_LIMIT_ERROR
/// [`LimitedRecords<_, R>`]: crate::records::LimitedRecords
pub trait LimitedRecordsImplLayout {
    /// The type of error returned from parsing.
    type Error;

    /// If `Some(E)`, `parse_with_context` of `LimitedRecordsImplBridge` will emit the
    /// provided constant as an error if the provided buffer is exhausted while `context`
    /// is not 0, or if the `context` reaches 0 but the provided buffer is not empty.
    const EXACT_LIMIT_ERROR: Option<Self::Error> = None;
}

/// An implementation of a limited records parser.
///
/// Like [`RecordsImpl`], `LimitedRecordsImpl` provides functions to parse
/// sequential records. Unlike `RecordsImpl`, `LimitedRecordsImpl` also imposes
/// a limit on the number of records that may be present. See
/// [`LimitedRecordsImplLayout`] for more details.
pub trait LimitedRecordsImpl<'a>: LimitedRecordsImplLayout {
    /// The limited records analogue of [`RecordsImpl::Record`]; see that type's
    /// documentation for more details.
    type Record;

    /// Parses a record after limit check has completed.
    ///
    /// `parse` is like [`RecordsImpl::parse_with_context`], except that it is
    /// only called after limit checks have been performed. When `parse` is
    /// called, it is guaranteed that the limit has not yet been reached.
    ///
    /// Each call to `parse` is assumed to parse exactly one record. If `parse`
    /// parses zero or more than one record, it may cause a parsing that should
    /// fail to succeed, or a parsing that should succeed to fail.
    ///
    /// For information about return values, see
    /// [`RecordsImpl::parse_with_context`].
    fn parse<BV: BufferView<&'a [u8]>>(
        bytes: &mut BV,
    ) -> Result<Option<Option<Self::Record>>, Self::Error>;
}

/// An implementation of a records serializer.
///
/// `RecordsSerializerImpl` provides functions to serialize sequential records.
/// It is required in order to construct a [`RecordsSerializer`].
pub trait RecordsSerializerImpl<'a> {
    /// The input type to this serializer.
    ///
    /// This is the serialization analogue of [`RecordsImpl::Record`]. Records
    /// serialization expects an [`Iterator`] of `Record`s.
    type Record;

    /// Provides the serialized length of a record.
    ///
    /// Returns the total length, in bytes, of the serialized encoding of
    /// `record`.
    fn record_length(record: &Self::Record) -> usize;

    /// Serializes `record` into a buffer.
    ///
    /// `data` will be exactly `Self::record_length(record)` bytes long.
    ///
    /// # Panics
    ///
    /// If `data` is not exactly `Self::record_length(record)` bytes long,
    /// `serialize` may panic.
    fn serialize(data: &mut [u8], record: &Self::Record);
}

/// An implementation of a serializer for records with alignment requirements.
pub trait AlignedRecordsSerializerImpl<'a>: RecordsSerializerImpl<'a> {
    /// Returns the alignment requirement of `record`.
    ///
    /// `alignment_requirement(record)` returns `(x, y)`, which means that the
    /// serialized encoding of `record` must be aligned at `x * n + y` bytes
    /// from the beginning of the records sequence for some non-negative `n`.
    ///
    /// `x` must be non-zero and `y` must be smaller than `x`.
    fn alignment_requirement(record: &Self::Record) -> (usize, usize);

    /// Serialize the padding between subsequent aligned records.
    ///
    /// Some formats require that padding bytes have particular content. This
    /// function serializes padding bytes as required by the format.
    fn serialize_padding(buf: &mut [u8], length: usize);
}

/// An instance of records serialization.
///
/// `RecordsSerializer` is instantiated with an [`Iterator`] that provides
/// items to be serialized by a [`RecordsSerializerImpl`].
///
/// `RecordsSerializer` implements [`InnerPacketBuilder`].
#[derive(Debug)]
pub struct RecordsSerializer<'a, S, R: 'a, I>
where
    S: RecordsSerializerImpl<'a, Record = R>,
    I: Iterator<Item = &'a R> + Clone,
{
    records: I,
    _marker: PhantomData<S>,
}

impl<'a, S, R: 'a, I> RecordsSerializer<'a, S, R, I>
where
    S: RecordsSerializerImpl<'a, Record = R>,
    I: Iterator<Item = &'a R> + Clone,
{
    /// Creates a new `RecordsSerializer` with given `records`.
    ///
    /// `records` must produce the same sequence of values from every iterator,
    /// even if cloned. Serialization is typically performed with two passes on
    /// `records`: one to calculate the total length in bytes
    /// (`records_bytes_len`) and another one to serialize to a buffer
    /// (`serialize_records`). Violating this rule may cause panics or malformed
    /// packets.
    pub fn new(records: I) -> Self {
        Self { records, _marker: PhantomData }
    }

    /// Returns the total length, in bytes, of the serialized encoding of the
    /// records contained within the `RecordsSerializer`.
    pub fn records_bytes_len(&self) -> usize {
        self.records.clone().map(|r| S::record_length(r)).sum()
    }

    /// `serialize_records` serializes all the records contained within the
    /// `RecordsSerializer`.
    ///
    /// # Panics
    ///
    /// `serialize_records` expects that `buffer` has enough bytes to serialize
    /// the contained records (as obtained from `records_bytes_len`), otherwise
    /// it's considered a violation of the API contract and the call may panic.
    pub fn serialize_records(&self, buffer: &mut [u8]) {
        let mut b = &mut &mut buffer[..];
        for r in self.records.clone() {
            // SECURITY: Take a zeroed buffer from b to prevent leaking
            // information from packets previously stored in this buffer.
            S::serialize(b.take_front_zero(S::record_length(r)).unwrap(), r);
        }
    }
}

/// An instance of aligned records serialization.
///
/// `AlignedRecordsSerializer` is instantiated with an [`Iterator`] that
/// provides items to be serialized by a [`AlignedRecordsSerializerImpl`].
#[derive(Debug)]
pub struct AlignedRecordsSerializer<'a, S, R: 'a, I>
where
    S: AlignedRecordsSerializerImpl<'a, Record = R>,
    I: Iterator<Item = &'a R> + Clone,
{
    start_pos: usize,
    records: I,
    _marker: PhantomData<S>,
}

impl<'a, S, R: 'a, I> AlignedRecordsSerializer<'a, S, R, I>
where
    S: AlignedRecordsSerializerImpl<'a, Record = R>,
    I: Iterator<Item = &'a R> + Clone,
{
    /// Creates a new `AlignedRecordsSerializer` with given `records` and
    /// `start_pos`.
    ///
    /// `records` must produce the same sequence of values from every iterator,
    /// even if cloned. See `RecordsSerializer` for more details.
    ///
    /// Alignment is calculated relative to the beginning of a virtual space of
    /// bytes. If non-zero, `start_pos` instructs the serializer to consider the
    /// buffer passed to [`serialize_records`] to start at the byte `start_pos`
    /// within this virtual space, and to calculate alignment and padding
    /// accordingly. For example, in the IPv6 Hop-by-Hop extension header, a
    /// fixed header of two bytes precedes that extension header's options, but
    /// alignment is calculated relative to the beginning of the extension
    /// header, not relative to the beginning of the options. Thus, when
    /// constructing an `AlignedRecordsSerializer` to serialize those options,
    /// `start_pos` would be 2.
    ///
    /// [`serialize_records`]: AlignedRecordsSerializer::serialize_records
    pub fn new(start_pos: usize, records: I) -> Self {
        Self { start_pos, records, _marker: PhantomData }
    }

    /// Returns the total length, in bytes, of the serialized records contained
    /// within the `AlignedRecordsSerializer`.
    ///
    /// Note that this length includes all padding required to ensure that all
    /// records satisfy their alignment requirements.
    pub fn records_bytes_len(&self) -> usize {
        let mut pos = self.start_pos;
        self.records
            .clone()
            .map(|r| {
                let (x, y) = S::alignment_requirement(r);
                let new_pos = align_up_to(pos, x, y) + S::record_length(r);
                let result = new_pos - pos;
                pos = new_pos;
                result
            })
            .sum()
    }

    /// `serialize_records` serializes all the records contained within the
    /// `AlignedRecordsSerializer`.
    ///
    /// # Panics
    ///
    /// `serialize_records` expects that `buffer` has enough bytes to serialize
    /// the contained records (as obtained from `records_bytes_len`), otherwise
    /// it's considered a violation of the API contract and the call may panic.
    pub fn serialize_records(&self, buffer: &mut [u8]) {
        let mut b = &mut &mut buffer[..];
        let mut pos = self.start_pos;
        for r in self.records.clone() {
            let (x, y) = S::alignment_requirement(r);
            let aligned = align_up_to(pos, x, y);
            let pad_len = aligned - pos;
            let pad = b.take_front_zero(pad_len).unwrap();
            S::serialize_padding(pad, pad_len);
            pos = aligned;
            // SECURITY: Take a zeroed buffer from b to prevent leaking
            // information from packets previously stored in this buffer.
            S::serialize(b.take_front_zero(S::record_length(r)).unwrap(), r);
            pos += S::record_length(r);
        }
        // we have to pad the containing header to 8-octet boundary.
        let padding = b.take_rest_front_zero();
        S::serialize_padding(padding, padding.len());
    }
}

/// Returns the aligned offset which is at `x * n + y`.
///
/// # Panics
///
/// Panics if `x == 0` or `y >= x`.
fn align_up_to(offset: usize, x: usize, y: usize) -> usize {
    assert!(x != 0 && y < x);
    // first add `x` to prevent overflow.
    (offset + x - 1 - y) / x * x + y
}

impl<'a, S, R: 'a, I> InnerPacketBuilder for RecordsSerializer<'a, S, R, I>
where
    S: RecordsSerializerImpl<'a, Record = R>,
    I: Iterator<Item = &'a R> + Clone,
{
    fn bytes_len(&self) -> usize {
        self.records_bytes_len()
    }

    fn serialize(&self, buffer: &mut [u8]) {
        self.serialize_records(buffer)
    }
}

impl<B, R> Records<B, R>
where
    B: ByteSlice,
    R: for<'a> RecordsImpl<'a>,
{
    /// Parses a sequence of records with a context.
    ///
    /// See `parse_with_mut_context` for details on `bytes`, `context`, and
    /// return value. `parse_with_context` just calls `parse_with_mut_context`
    /// with a mutable reference to the `context` which is passed by value to
    /// this function.
    pub fn parse_with_context(
        bytes: B,
        mut context: R::Context,
    ) -> Result<Records<B, R>, R::Error> {
        Self::parse_with_mut_context(bytes, &mut context)
    }

    /// Parses a sequence of records with a mutable context.
    ///
    /// `parse_with_mut_context` parses `bytes` as a sequence of records.
    /// `context` may be used by implementers to maintain state while parsing
    /// multiple records.
    ///
    /// `parse_with_mut_context` performs a single pass over all of the records
    /// to verify that they are well-formed. Once `parse_with_context` returns
    /// successfully, the resulting `Records` can be used to construct
    /// infallible iterators.
    pub fn parse_with_mut_context(
        bytes: B,
        context: &mut R::Context,
    ) -> Result<Records<B, R>, R::Error> {
        // First, do a single pass over the bytes to detect any errors up front.
        // Once this is done, since we have a reference to `bytes`, these bytes
        // can't change out from under us, and so we can treat any iterator over
        // these bytes as infallible. This makes a few assumptions, but none of
        // them are that big of a deal. In all cases, breaking these assumptions
        // would at worst result in a runtime panic.
        // - B could return different bytes each time
        // - R::parse could be non-deterministic
        let c = context.clone();
        let mut b = LongLivedBuff::new(bytes.deref());
        while next::<_, R>(&mut b, context)?.is_some() {}
        Ok(Records { bytes, context: c })
    }
}

impl<B, R> Records<B, R>
where
    B: ByteSlice,
    R: for<'a> RecordsImpl<'a, Context = ()>,
{
    /// Parses a sequence of records.
    ///
    /// Equivalent to calling [`parse_with_context`] with `context = ()`.
    ///
    /// [`parse_with_context`]: crate::records::Records::parse_with_context
    pub fn parse(bytes: B) -> Result<Records<B, R>, R::Error> {
        Self::parse_with_context(bytes, ())
    }
}

impl<B, R> FromRaw<RecordsRaw<B, R>, ()> for Records<B, R>
where
    for<'a> R: RecordsImpl<'a>,
    B: ByteSlice,
{
    type Error = R::Error;

    fn try_from_raw_with(raw: RecordsRaw<B, R>, _args: ()) -> Result<Self, R::Error> {
        Records::<B, R>::parse_with_context(raw.bytes, raw.context)
    }
}

impl<B: Deref<Target = [u8]>, R> Records<B, R>
where
    R: for<'a> RecordsImpl<'a>,
{
    /// Gets the underlying bytes.
    ///
    /// `bytes` returns a reference to the byte slice backing this `Records`.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}

impl<'a, B, R> Records<B, R>
where
    B: 'a + ByteSlice,
    R: RecordsImpl<'a>,
{
    /// Creates an iterator over options.
    ///
    /// `iter` constructs an iterator over the records. Since the records were
    /// validated in `parse`, then so long as [`R::parse_with_context`] is
    /// deterministic, the iterator is infallible.
    ///
    /// [`R::parse_with_context`]: crate::records::RecordsImpl::parse_with_context
    pub fn iter(&'a self) -> RecordsIter<'a, R> {
        RecordsIter { bytes: &self.bytes, context: self.context.clone_for_iter() }
    }
}

impl<'a, R> RecordsIter<'a, R>
where
    R: RecordsImpl<'a>,
{
    /// Gets a reference to the context.
    pub fn context(&self) -> &R::Context {
        &self.context
    }
}

impl<'a, R> Iterator for RecordsIter<'a, R>
where
    R: RecordsImpl<'a>,
{
    type Item = R::Record;

    fn next(&mut self) -> Option<R::Record> {
        let mut bytes = LongLivedBuff::new(self.bytes);
        // use match rather than expect because expect requires that Err: Debug
        #[allow(clippy::match_wild_err_arm)]
        let result = match next::<_, R>(&mut bytes, &mut self.context) {
            Ok(o) => o,
            Err(_) => panic!("already-validated options should not fail to parse"),
        };
        self.bytes = bytes.into_rest();
        result
    }
}

/// Gets the next entry for a set of sequential records in `bytes`.
///
/// On return, `bytes` will be pointing to the start of where a next record
/// would be.
fn next<'a, BV, R>(bytes: &mut BV, context: &mut R::Context) -> Result<Option<R::Record>, R::Error>
where
    R: RecordsImpl<'a>,
    BV: BufferView<&'a [u8]>,
{
    loop {
        match R::parse_with_context(bytes, context) {
            // `parse_with_context` cannot parse any more, return Ok(None) to
            // let the caller know that we have parsed all possible records for
            // a given `bytes`.
            Ok(None) => return Ok(None),

            // `parse_with_context` was unable to parse a record, not because
            // `bytes` was malformed but for other non fatal reasons, so we can
            // skip.
            Ok(Some(None)) => {}

            // `parse_with_context` was able to parse a record, so return it.
            Ok(Some(Some(o))) => return Ok(Some(o)),

            // `parse_with_context` had an error so pass that error to the
            // caller.
            Err(err) => return Err(err),
        }
    }
}

/// A wrapper around the implementation of `BufferView` for slices.
///
/// `LongLivedBuff` is a thin wrapper around `&[u8]` meant to provide an
/// implementation of `BufferView` that returns slices tied to the same lifetime
/// as the slice that `LongLivedBuff` was created with. This is in contrast to
/// the more widely used `&'b mut &'a [u8]` `BufferView` implementer that
/// returns slice references tied to lifetime `b`.
struct LongLivedBuff<'a>(&'a [u8]);

impl<'a> LongLivedBuff<'a> {
    /// Creates a new `LongLivedBuff` around a slice reference with lifetime
    /// `a`.
    ///
    /// All slices returned by the `BufferView` impl of `LongLivedBuff` are
    /// guaranteed to return slice references tied to the same lifetime `a`.
    fn new(data: &'a [u8]) -> LongLivedBuff<'a> {
        LongLivedBuff::<'a>(data)
    }
}

impl<'a> AsRef<[u8]> for LongLivedBuff<'a> {
    fn as_ref(&self) -> &[u8] {
        self.0
    }
}

impl<'a> BufferView<&'a [u8]> for LongLivedBuff<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.0.len() >= n {
            let (prefix, rest) = core::mem::replace(&mut self.0, &[]).split_at(n);
            core::mem::replace(&mut self.0, rest);
            Some(prefix)
        } else {
            None
        }
    }

    fn take_back(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.0.len() >= n {
            let (rest, suffix) = core::mem::replace(&mut self.0, &[]).split_at(n);
            core::mem::replace(&mut self.0, rest);
            Some(suffix)
        } else {
            None
        }
    }

    fn into_rest(self) -> &'a [u8] {
        self.0
    }
}

#[cfg(test)]
mod test {
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

    use super::*;

    const DUMMY_BYTES: [u8; 16] = [
        0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03,
        0x04,
    ];

    #[derive(Debug, AsBytes, FromBytes, Unaligned)]
    #[repr(C)]
    struct DummyRecord {
        a: [u8; 2],
        b: u8,
        c: u8,
    }

    fn parse_dummy_rec<'a, BV>(
        data: &mut BV,
    ) -> Result<Option<Option<LayoutVerified<&'a [u8], DummyRecord>>>, ()>
    where
        BV: BufferView<&'a [u8]>,
    {
        if data.is_empty() {
            return Ok(None);
        }

        match data.take_obj_front::<DummyRecord>() {
            Some(res) => Ok(Some(Some(res))),
            None => Err(()),
        }
    }

    //
    // Context-less records
    //

    #[derive(Debug)]
    struct ContextlessRecordImpl;

    impl RecordsImplLayout for ContextlessRecordImpl {
        type Context = ();
        type Error = ();
    }

    impl<'a> RecordsImpl<'a> for ContextlessRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            _context: &mut Self::Context,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            parse_dummy_rec(data)
        }
    }

    //
    // Limit context records
    //

    #[derive(Debug)]
    struct LimitContextRecordImpl;

    impl LimitedRecordsImplLayout for LimitContextRecordImpl {
        type Error = ();
    }

    impl<'a> LimitedRecordsImpl<'a> for LimitContextRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            parse_dummy_rec(data)
        }
    }

    //
    // Exact limit context records
    //

    #[derive(Debug)]
    struct ExactLimitContextRecordImpl;

    impl LimitedRecordsImplLayout for ExactLimitContextRecordImpl {
        type Error = ();

        const EXACT_LIMIT_ERROR: Option<()> = Some(());
    }

    impl<'a> LimitedRecordsImpl<'a> for ExactLimitContextRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            parse_dummy_rec(data)
        }
    }

    //
    // Filter context records
    //

    #[derive(Debug)]
    struct FilterContextRecordImpl;

    #[derive(Clone)]
    struct FilterContext {
        pub disallowed: [bool; 256],
    }

    impl RecordsContext for FilterContext {}

    impl RecordsImplLayout for FilterContextRecordImpl {
        type Context = FilterContext;
        type Error = ();
    }

    impl core::fmt::Debug for FilterContext {
        fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
            write!(f, "FilterContext{{disallowed:{:?}}}", &self.disallowed[..])
        }
    }

    impl<'a> RecordsImpl<'a> for FilterContextRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            bytes: &mut BV,
            context: &mut Self::Context,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            if bytes.len() < core::mem::size_of::<DummyRecord>() {
                Ok(None)
            } else if bytes.as_ref()[0..core::mem::size_of::<DummyRecord>()]
                .iter()
                .any(|x| context.disallowed[*x as usize])
            {
                Err(())
            } else {
                parse_dummy_rec(bytes)
            }
        }
    }

    //
    // Stateful context records
    //

    #[derive(Debug)]
    struct StatefulContextRecordImpl;

    #[derive(Clone, Debug)]
    struct StatefulContext {
        pub pre_parse_counter: usize,
        pub parse_counter: usize,
        pub post_parse_counter: usize,
        pub iter: bool,
    }

    impl RecordsImplLayout for StatefulContextRecordImpl {
        type Context = StatefulContext;
        type Error = ();
    }

    impl StatefulContext {
        pub fn new() -> StatefulContext {
            StatefulContext {
                pre_parse_counter: 0,
                parse_counter: 0,
                post_parse_counter: 0,
                iter: false,
            }
        }
    }

    impl RecordsContext for StatefulContext {
        fn clone_for_iter(&self) -> Self {
            let mut x = self.clone();
            x.iter = true;
            x
        }
    }

    impl<'a> RecordsImpl<'a> for StatefulContextRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Self::Context,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            if !context.iter {
                context.pre_parse_counter += 1;
            }

            let ret = parse_dummy_rec_with_context(data, context);

            match ret {
                Ok(Some(Some(_))) if !context.iter => {
                    context.post_parse_counter += 1;
                }
                _ => {}
            }

            ret
        }
    }

    impl<'a> RecordsRawImpl<'a> for StatefulContextRecordImpl {
        fn parse_raw_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Self::Context,
        ) -> Result<bool, Self::Error> {
            Self::parse_with_context(data, context).map(|p| p.is_some())
        }
    }

    fn parse_dummy_rec_with_context<'a, BV>(
        data: &mut BV,
        context: &mut StatefulContext,
    ) -> Result<Option<Option<LayoutVerified<&'a [u8], DummyRecord>>>, ()>
    where
        BV: BufferView<&'a [u8]>,
    {
        if data.is_empty() {
            return Ok(None);
        }

        if !context.iter {
            context.parse_counter += 1;
        }

        match data.take_obj_front::<DummyRecord>() {
            Some(res) => Ok(Some(Some(res))),
            None => Err(()),
        }
    }

    fn check_parsed_record(rec: &DummyRecord) {
        assert_eq!(rec.a[0], 0x01);
        assert_eq!(rec.a[1], 0x02);
        assert_eq!(rec.b, 0x03);
    }

    fn validate_parsed_stateful_context_records<B: ByteSlice>(
        records: Records<B, StatefulContextRecordImpl>,
        context: StatefulContext,
    ) {
        // Should be 5 because on the last iteration, we should realize that we
        // have no more bytes left and end before parsing (also explaining why
        // `parse_counter` should only be 4.
        assert_eq!(context.pre_parse_counter, 5);
        assert_eq!(context.parse_counter, 4);
        assert_eq!(context.post_parse_counter, 4);

        let mut iter = records.iter();
        let context = &iter.context;
        assert_eq!(context.pre_parse_counter, 0);
        assert_eq!(context.parse_counter, 0);
        assert_eq!(context.post_parse_counter, 0);
        assert_eq!(context.iter, true);

        // Manually iterate over `iter` so as to not move it.
        let mut count = 0;
        while let Some(_) = iter.next() {
            count += 1;
        }
        assert_eq!(count, 4);

        // Check to see that when iterating, the context doesn't update counters
        // as that is how we implemented our StatefulContextRecordImpl..
        let context = &iter.context;
        assert_eq!(context.pre_parse_counter, 0);
        assert_eq!(context.parse_counter, 0);
        assert_eq!(context.post_parse_counter, 0);
        assert_eq!(context.iter, true);
    }

    //
    // Utilities
    //

    impl<B, R> Records<B, R>
    where
        B: ByteSlice,
        R: for<'a> RecordsImpl<'a>,
    {
        /// Parse a sequence of records with a context, using a `BufferView`.
        ///
        /// See `parse_bv_with_mut_context` for details on `bytes`, `context`,
        /// and return value. `parse_bv_with_context` just calls
        /// `parse_bv_with_mut_context` with a mutable reference to the
        /// `context` which is passed by value to this function.
        pub fn parse_bv_with_context<BV: BufferView<B>>(
            bytes: &mut BV,
            mut context: R::Context,
        ) -> Result<Records<B, R>, R::Error> {
            Self::parse_bv_with_mut_context(bytes, &mut context)
        }

        /// Parse a sequence of records with a mutable context, using a
        /// `BufferView`.
        ///
        /// This function is exactly the same as `parse_with_mut_context` except
        /// instead of operating on a `ByteSlice`, we operate on a
        /// `BufferView<B>` where `B` is a `ByteSlice`.
        /// `parse_bv_with_mut_context` enables parsing records without knowing
        /// the size of all records beforehand (unlike `parse_with_mut_context`
        /// where callers need to pass in a `ByteSlice` of some predetermined
        /// sized). Since callers will provide a mutable reference to a
        /// `BufferView`, `parse_bv_with_mut_context` will take only the amount
        /// of bytes it needs to parse records, leaving the rest in the
        /// `BufferView` object. That is, when `parse_bv_with_mut_context`
        /// returns, the `BufferView` object provided will be x bytes smaller,
        /// where x is the number of bytes required to parse the records.
        pub fn parse_bv_with_mut_context<BV: BufferView<B>>(
            bytes: &mut BV,
            context: &mut R::Context,
        ) -> Result<Records<B, R>, R::Error> {
            let c = context.clone();
            let mut b = LongLivedBuff::new(bytes.as_ref());
            while next::<_, R>(&mut b, context)?.is_some() {}

            // When we get here, we know that whatever is left in `b` is not
            // needed so we only take the amount of bytes we actually need from
            // `bytes`, leaving the rest alone for the caller to continue
            // parsing with.
            let bytes_len = bytes.len();
            let b_len = b.len();
            Ok(Records { bytes: bytes.take_front(bytes_len - b_len).unwrap(), context: c })
        }
    }

    #[test]
    fn all_records_parsing() {
        let parsed = Records::<_, ContextlessRecordImpl>::parse(&DUMMY_BYTES[..]).unwrap();
        assert_eq!(parsed.iter().count(), 4);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }
    }

    #[test]
    fn limit_records_parsing() {
        // Test without mutable limit/context
        let limit = 2;
        let parsed = LimitedRecords::<_, LimitContextRecordImpl>::parse_with_context(
            &DUMMY_BYTES[..],
            limit,
        )
        .unwrap();
        assert_eq!(parsed.iter().count(), limit);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }

        // Test with mutable limit/context
        let mut mut_limit = limit;
        let parsed = LimitedRecords::<_, LimitContextRecordImpl>::parse_with_mut_context(
            &DUMMY_BYTES[..],
            &mut mut_limit,
        )
        .unwrap();
        assert_eq!(mut_limit, 0);
        assert_eq!(parsed.iter().count(), limit);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }
    }

    #[test]
    fn limit_records_parsing_with_bv() {
        // Test without mutable limit/context
        let limit = 2;
        let mut bv = &mut &DUMMY_BYTES[..];
        let parsed =
            LimitedRecords::<_, LimitContextRecordImpl>::parse_bv_with_context(&mut bv, limit)
                .unwrap();
        assert_eq!(bv.len(), DUMMY_BYTES.len() - std::mem::size_of::<DummyRecord>() * limit);
        assert_eq!(parsed.iter().count(), limit);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }

        // Test with mutable limit context
        let mut mut_limit = limit;
        let mut bv = &mut &DUMMY_BYTES[..];
        let parsed = LimitedRecords::<_, LimitContextRecordImpl>::parse_bv_with_mut_context(
            &mut bv,
            &mut mut_limit,
        )
        .unwrap();
        assert_eq!(mut_limit, 0);
        assert_eq!(bv.len(), DUMMY_BYTES.len() - std::mem::size_of::<DummyRecord>() * limit);
        assert_eq!(parsed.iter().count(), limit);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }
    }

    #[test]
    fn exact_limit_records_parsing() {
        LimitedRecords::<_, ExactLimitContextRecordImpl>::parse_with_context(&DUMMY_BYTES[..], 2)
            .expect_err("fails if all the buffer hasn't been parsed");
        LimitedRecords::<_, ExactLimitContextRecordImpl>::parse_with_context(&DUMMY_BYTES[..], 5)
            .expect_err("fails if can't extract enough records");
    }

    #[test]
    fn context_filtering_some_byte_records_parsing() {
        // Do not disallow any bytes
        let context = FilterContext { disallowed: [false; 256] };
        let parsed =
            Records::<_, FilterContextRecordImpl>::parse_with_context(&DUMMY_BYTES[..], context)
                .unwrap();
        assert_eq!(parsed.iter().count(), 4);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }

        // Do not allow byte value 0x01
        let mut context = FilterContext { disallowed: [false; 256] };
        context.disallowed[1] = true;
        Records::<_, FilterContextRecordImpl>::parse_with_context(&DUMMY_BYTES[..], context)
            .expect_err("fails if the buffer has an element with value 0x01");
    }

    #[test]
    fn context_filtering_some_byte_records_parsing_with_bv() {
        // Do not disallow any bytes
        let context = FilterContext { disallowed: [false; 256] };
        let mut bv = &mut &DUMMY_BYTES[..];
        let parsed =
            Records::<_, FilterContextRecordImpl>::parse_bv_with_context(&mut bv, context).unwrap();
        assert_eq!(bv.len(), 0);
        assert_eq!(parsed.iter().count(), 4);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }

        // Do not allow byte value 0x01
        let mut bv = &mut &DUMMY_BYTES[..];
        let mut context = FilterContext { disallowed: [false; 256] };
        context.disallowed[1] = true;
        Records::<_, FilterContextRecordImpl>::parse_bv_with_context(&mut bv, context)
            .expect_err("fails if the buffer has an element with value 0x01");
        assert_eq!(bv.len(), DUMMY_BYTES.len());
    }

    #[test]
    fn stateful_context_records_parsing() {
        let mut context = StatefulContext::new();
        let parsed = Records::<_, StatefulContextRecordImpl>::parse_with_mut_context(
            &DUMMY_BYTES[..],
            &mut context,
        )
        .unwrap();
        validate_parsed_stateful_context_records(parsed, context);
    }

    #[test]
    fn stateful_context_records_parsing_with_bv() {
        let mut context = StatefulContext::new();
        let mut bv = &mut &DUMMY_BYTES[..];
        let parsed = Records::<_, StatefulContextRecordImpl>::parse_bv_with_mut_context(
            &mut bv,
            &mut context,
        )
        .unwrap();
        assert_eq!(bv.len(), 0);
        validate_parsed_stateful_context_records(parsed, context);
    }

    #[test]
    fn raw_parse_success() {
        let mut context = StatefulContext::new();
        let mut bv = &mut &DUMMY_BYTES[..];
        let result = RecordsRaw::<_, StatefulContextRecordImpl>::parse_raw_with_mut_context(
            &mut bv,
            &mut context,
        )
        .unwrap();
        assert_eq!(result.bytes.len(), DUMMY_BYTES.len());
        let parsed = Records::try_from_raw(result).unwrap();
        validate_parsed_stateful_context_records(parsed, context);
    }

    #[test]
    fn raw_parse_failure() {
        let mut context = StatefulContext::new();
        let mut bv = &mut &DUMMY_BYTES[0..15];
        let (result, _) = RecordsRaw::<_, StatefulContextRecordImpl>::parse_raw_with_mut_context(
            &mut bv,
            &mut context,
        )
        .unwrap_incomplete();
        assert_eq!(result, &DUMMY_BYTES[0..12]);
    }
}

/// Utilities for parsing the options formats in protocols like IPv4, TCP, and
/// NDP.
///
/// This module provides parsing utilities for [type-length-value]-like records
/// encoding like those used by the options in an IPv4 or TCP header or an NDP
/// packet. These formats are not identical, but share enough in common that the
/// utilities provided here only need a small amount of customization by the
/// user to be fully functional.
///
/// [type-length-value]: https://en.wikipedia.org/wiki/Type-length-value
pub mod options {
    use super::*;

    /// A parsed sequence of options.
    ///
    /// `Options` represents a parsed sequence of options, for example from an
    /// IPv4 or TCP header or an NDP packet. `Options` uses [`Records`] under
    /// the hood.
    ///
    /// [`Records`]: crate::records::Records
    pub type Options<B, O> = Records<B, OptionsImplBridge<O>>;

    /// A not-yet-parsed sequence of options.
    ///
    /// `OptionsRaw` represents a not-yet-parsed and not-yet-validated sequence
    /// of options, for example from an IPv4 or TCP header or an NDP packet.
    /// `OptionsRaw` uses [`RecordsRaw`] under the hood.
    ///
    /// [`RecordsRaw`]: crate::records::RecordsRaw
    pub type OptionsRaw<B, O> = RecordsRaw<B, OptionsImplBridge<O>>;

    /// An instance of options serialization.
    ///
    /// `OptionsSerializer` is instantiated with an [`Iterator`] that provides
    /// items to be serialized by an [`OptionsSerializerImpl`].
    pub type OptionsSerializer<'a, S, O, I> = RecordsSerializer<'a, S, O, I>;

    /// Create a bridge to `RecordsImplLayout` and `RecordsImpl` from an `O`
    /// that implements `OptionsImpl`.
    ///
    /// (Note that this doc comment is written in terms of the `Options` type
    /// alias, but the same explanations apply to the `OptionsRaw` type alias as
    /// well).
    ///
    /// The obvious solution to this problem would be to define `Options` as
    /// follows, along with the following blanket impls:
    ///
    /// ```rust,ignore
    /// pub type Options<B, O> = Records<B, O>;
    ///
    /// impl<O: OptionsImplLayout> RecordsImplLayout for O { ... }
    ///
    /// impl<'a, O: OptionsImpl<'a>> RecordsImpl<'a> for O { ... }
    /// ```
    ///
    /// Unfortunately, we also provide a similar type alias in the parent
    /// `records` module, defining limited records parsing in terms of general
    /// records parsing. If we were to provide both these blanket impls and the
    /// similar blanket impls in terms of `LimitedRecordsImplLayout` and
    /// `LimitedRecordsImpl`, we would have conflicting blanket impls. Instead,
    /// we wrap the `OptionsImpl` type in an `OptionsImplBridge` in order to
    /// make it a distinct concrete type and avoid the conflicting blanket impls
    /// problem.
    ///
    /// Note that we could theoretically provide the blanket impl here and only
    /// use the newtype trick in the `records` module (or vice-versa), but that
    /// would just result in more patterns to keep track of.
    ///
    /// `OptionsImplBridge` is `#[doc(hidden)]`; it is only `pub` because it
    /// appears in the type aliases `Options` and `OptionsRaw`.
    #[derive(Debug)]
    #[doc(hidden)]
    pub struct OptionsImplBridge<O>(PhantomData<O>);

    impl<O> RecordsImplLayout for OptionsImplBridge<O>
    where
        O: OptionsImplLayout,
    {
        type Context = ();
        type Error = OptionParseErr<O::Error>;
    }

    impl<'a, O> RecordsImpl<'a> for OptionsImplBridge<O>
    where
        O: OptionsImpl<'a>,
    {
        type Record = O::Option;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            _context: &mut Self::Context,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            next::<_, O>(data)
        }
    }

    impl<'a, O> RecordsSerializerImpl<'a> for O
    where
        O: OptionsSerializerImpl<'a>,
    {
        type Record = O::Option;

        fn record_length(record: &Self::Record) -> usize {
            let base = 2 + O::option_length(record);

            // Pad up to option_len_multiplier:
            (base + O::OPTION_LEN_MULTIPLIER - 1) / O::OPTION_LEN_MULTIPLIER
                * O::OPTION_LEN_MULTIPLIER
        }

        fn serialize(data: &mut [u8], record: &Self::Record) {
            // NOTE(brunodalbo) we don't currently support serializing the two
            //  single-byte options used in TCP and IP: NOP and END_OF_OPTIONS.
            //  If it is necessary to support those as part of TLV options
            //  serialization, some changes will be required here.

            // Data not having enough space is a contract violation, so we panic
            // in that case.
            data[0] = O::option_kind(record);
            let length = Self::record_length(record) / O::OPTION_LEN_MULTIPLIER;
            // Option length not fitting in u8 is a contract violation. Without
            // debug assertions on, this will cause the packet to be malformed.
            debug_assert!(length <= std::u8::MAX.into());
            // The fact that we subtract after dividing by
            // O::OPTION_LEN_MULTIPLIER doesn't matter since LENGTH_ENCODING
            // cannot be ValueOnly when OPTION_LEN_MULTIPLIER is greater than
            // one, and thus byte_offset() cannot return a non-zero value when
            // OPTION_LEN_MULTIPLIER is greater than one. If we ever lift this
            // restriction, we may need to change this code.
            data[1] = (length - O::LENGTH_ENCODING.byte_offset()) as u8;
            // SECURITY: Because padding may have occurred, we zero-fill data
            // before passing it along in order to prevent leaking information
            // from packets previously stored in the buffer.
            for b in data[2..].iter_mut() {
                *b = 0;
            }
            O::serialize(&mut data[2..], record)
        }
    }

    impl<'a, O> AlignedRecordsSerializerImpl<'a> for O
    where
        O: AlignedOptionsSerializerImpl<'a>,
    {
        fn alignment_requirement(record: &Self::Record) -> (usize, usize) {
            // Use the underlying option's alignment requirement as the
            // alignment requirement for the record.
            O::alignment_requirement(record)
        }

        fn serialize_padding(buf: &mut [u8], length: usize) {
            O::serialize_padding(buf, length);
        }
    }

    /// Errors returned from parsing options.
    ///
    /// `OptionParseErr` is either `Internal`, which indicates that this module
    /// encountered a malformed sequence of options (likely with a length field
    /// larger than the remaining bytes in the options buffer), or `External`,
    /// which indicates that the [`OptionsImpl::parse`] callback returned an
    /// error.
    #[derive(Debug, Eq, PartialEq)]
    pub enum OptionParseErr<E> {
        Internal,
        External(E),
    }

    // End of Options List in both IPv4 and TCP.
    pub const END_OF_OPTIONS: u8 = 0;

    // NOP in both IPv4 and TCP.
    pub const NOP: u8 = 1;

    /// Whether the length field of an option encodes the length of the entire
    /// option (including type and length fields) or only of the value field.
    ///
    /// Note that a `LengthEncoding` of `TypeLengthValue` must not be combined
    /// with a greater-than-one value for the
    /// [`OptionsImplLayout::OPTION_LEN_MULTIPLIER`] constant.
    #[derive(Copy, Clone, Eq, PartialEq)]
    pub enum LengthEncoding {
        TypeLengthValue,
        ValueOnly,
    }

    impl LengthEncoding {
        /// The offset (in bytes) to subtract from the length of an option (when
        /// that length includes the type and length bytes) in order to get the
        /// value that should be encoded for the length byte.
        fn byte_offset(self) -> usize {
            match self {
                LengthEncoding::TypeLengthValue => 0,
                LengthEncoding::ValueOnly => 2,
            }
        }
    }

    /// Basic associated type and constants used by an [`OptionsImpl`].
    ///
    /// This trait is kept separate from `OptionsImpl` so that the associated
    /// type and constants do not depend on the lifetime parameter to
    /// `OptionsImpl`.
    pub trait OptionsImplLayout {
        /// The type of errors that may be returned by a call to
        /// [`OptionsImpl::parse`].
        type Error;

        /// The value to multiply read lengths by.
        ///
        /// Some formats (such as NDP) do not directly encode the length in
        /// bytes of each option, but instead encode a number which must be
        /// multiplied by a constant in order to get the length in bytes.
        ///
        /// By default, this constant has the value 1.
        const OPTION_LEN_MULTIPLIER: usize = 1;

        /// The End of options type (if one exists).
        const END_OF_OPTIONS: Option<u8> = Some(END_OF_OPTIONS);

        /// The No-op type (if one exists).
        const NOP: Option<u8> = Some(NOP);

        /// The encoding of the length byte.
        ///
        /// Some formats (such as IPv4) use the length field to encode the
        /// length of the entire option, including the type and length bytes.
        /// Other formats (such as IPv6) use the length field to encode the
        /// length of only the value. This constant specifies which encoding is
        /// used.
        ///
        /// Note that if `LENGTH_ENCODING == ValueOnly`, then
        /// [`OPTION_LEN_MULTIPLIER`] must be 1. This invariant is checked by a
        /// link-time assertion; if it is not upheld, linking will fail due to
        /// the missing symbol
        /// `packet_records_options_impl_layout_length_encoding_option_len_multiplier`.
        ///
        /// [`OPTION_LEN_MULTIPLIER`]: crate::records::options::OptionsImplLayout::OPTION_LEN_MULTIPLIER
        const LENGTH_ENCODING: LengthEncoding = LengthEncoding::TypeLengthValue;

        // TODO(joshlf): Once const generics are stable, turn this link-time
        // assertion into a compile-time assertion using the `static_assertions`
        // crate.

        #[doc(hidden)]
        fn __assert_length_encoding_option_len_multiplier() {
            if Self::LENGTH_ENCODING == LengthEncoding::ValueOnly && Self::OPTION_LEN_MULTIPLIER > 1
            {
                extern "C" {
                    fn packet_records_options_impl_layout_length_encoding_option_len_multiplier();
                }
                unsafe {
                    packet_records_options_impl_layout_length_encoding_option_len_multiplier()
                };
            }
        }
    }

    /// An implementation of an options parser.
    ///
    /// `OptionsImpl` provides functions to parse fixed- and variable-length
    /// options. It is required in order to construct an [`Options`].
    pub trait OptionsImpl<'a>: OptionsImplLayout {
        /// The type of an option; the output from the [`parse`] function.
        ///
        /// For long or variable-length data, implementers advised to make
        /// `Option` a reference into the bytes passed to `parse`. Such a
        /// reference will need to carry the lifetime `'a`, which is the same
        /// lifetime that is passed to `parse`, and is also the lifetime
        /// parameter to this trait.
        ///
        /// [`parse`]: crate::records::options::OptionsImpl::parse
        type Option;

        /// Parses a record with some context.
        ///
        /// `parse_with_context` takes a variable-length `data` and a `context` to
        /// maintain state, and returns `Ok(Some(Some(o)))` if the record is
        /// successfully parsed as `o`, `Ok(Some(None))` if the record is
        /// well-formed but the implementer can't extract a concrete object (e.g.
        /// the record is an unimplemented enumeration, but it can be safely
        /// "skipped"), `Ok(None)` if `parse_with_context` is unable to parse more
        /// records, and `Err(err)` if the `data` is malformed for the attempted
        /// record parsing.
        ///
        /// `data` may be empty. It is up to the implementer to handle an exhausted
        /// `data`.
        ///
        /// When returning `Ok(Some(None))` it's the implementer's responsibility to
        /// consume the bytes of the record from `data`. If this doesn't happen,
        /// then `parse_with_context` will be called repeatedly on the same `data`,
        /// and the program will be stuck in an infinite loop. If the implementation
        /// is unable to know how many bytes to consume from `data` in order to skip
        /// the record, `parse_with_context` must return `Err`.
        ///
        /// `parse_with_context` must be deterministic, or else
        /// [`Records::parse_with_context`] cannot guarantee that future iterations
        /// will not produce errors (and panic).

        /// Parses an option.
        ///
        /// `parse` takes a kind byte and variable-length data and returns
        /// `Ok(Some(o))` if the option successfully parsed as `o`, `Ok(None)`
        /// if the kind byte was unrecognized, and `Err(err)` if the kind byte
        /// was recognized but `data` was malformed for that option kind.
        ///
        /// `parse` is allowed to not recognize certain option kinds, as the
        /// length field can still be used to safely skip over them, but it must
        /// recognize all single-byte options (if it didn't, a single-byte
        /// option would be spuriously interpreted as a multi-byte option, and
        /// the first byte of the next option byte would be spuriously
        /// interpreted as the option's length byte).
        ///
        /// `parse` must be deterministic, or else [`Options::parse`] cannot
        /// guarantee that future iterations will not produce errors (and
        /// panic).
        ///
        /// [`Options::parse`]: crate::records::Records::parse
        fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Option>, Self::Error>;
    }

    /// An implementation of an options serializer.
    ///
    /// `OptionsSerializerImpl` provides to functions to serialize fixed- and
    /// variable-length options. It is required in order to construct an
    /// `OptionsSerializer`.
    pub trait OptionsSerializerImpl<'a>: OptionsImplLayout {
        /// The input type to this serializer.
        ///
        /// This is the serialization analogue of [`OptionsImpl::Option`].
        /// Options serialization expects an [`Iterator`] of `Option`s.
        type Option;

        /// Returns the serialized length, in bytes, of the given `option`.
        ///
        /// Implementers must return the length, in bytes, of the **data***
        /// portion of the option field (not counting the type and length
        /// bytes). The internal machinery of options serialization takes care
        /// of aligning options to their [`OPTION_LEN_MULTIPLIER`] boundaries,
        /// adding padding bytes if necessary.
        ///
        /// [`OPTION_LEN_MULTIPLIER`]: crate::records::options::OptionsImplLayout::OPTION_LEN_MULTIPLIER
        fn option_length(option: &Self::Option) -> usize;

        /// Returns the wire value for this option kind.
        fn option_kind(option: &Self::Option) -> u8;

        /// Serializes `option` into `data`.
        ///
        /// `data` will be exactly `Self::option_length(option)` bytes long.
        /// Implementers must write the **data** portion of `option` into `data`
        /// (not the type or length bytes).
        ///
        /// # Panics
        ///
        /// If `data` is not exactly `Self::option_length(option)` bytes long,
        /// `serialize` may panic.
        fn serialize(data: &mut [u8], option: &Self::Option);
    }

    pub trait AlignedOptionsSerializerImpl<'a>: OptionsSerializerImpl<'a> {
        /// Returns the alignment requirement of `option`.
        ///
        /// `alignment_requirement(option)` returns `(x, y)`, which means that
        /// the serialized encoding of `option` must be aligned at `x * n + y`
        /// bytes from the beginning of the options sequence for some
        /// non-negative `n`. For example, the IPv6 Router Alert Hop-by-Hop
        /// option has alignment (2, 0), while the Jumbo Payload option has
        /// alignment (4, 2). (1, 0) means there is no alignment requirement.
        ///
        /// `x` must be non-zero and `y` must be smaller than `x`.
        fn alignment_requirement(option: &Self::Option) -> (usize, usize);

        /// Serialize the padding between subsequent aligned options.
        ///
        /// Some formats require that padding bytes have particular content.
        /// This function serializes padding bytes as required by the format.
        fn serialize_padding(buf: &mut [u8], length: usize);
    }

    fn next<'a, BV, O>(
        bytes: &mut BV,
    ) -> Result<Option<Option<O::Option>>, OptionParseErr<O::Error>>
    where
        BV: BufferView<&'a [u8]>,
        O: OptionsImpl<'a>,
    {
        // For an explanation of this format, see the "Options" section of
        // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure
        loop {
            let kind = match bytes.take_byte_front() {
                None => return Ok(None),
                Some(k) => {
                    // Can't do pattern matching with associated constants, so
                    // do it the good-ol' way:
                    if Some(k) == O::NOP {
                        continue;
                    } else if Some(k) == O::END_OF_OPTIONS {
                        return Ok(None);
                    }
                    k
                }
            };
            let len = match bytes.take_byte_front() {
                None => return Err(OptionParseErr::Internal),
                Some(len) => (len as usize) * O::OPTION_LEN_MULTIPLIER,
            };

            if len < 2 || (len - 2) > bytes.len() {
                return Err(OptionParseErr::Internal);
            }

            // We can safely unwrap here since we verified the correct length
            // above.
            let option_data = bytes.take_front(len - 2).unwrap();
            match O::parse(kind, option_data) {
                Ok(Some(o)) => return Ok(Some(Some(o))),
                Ok(None) => {}
                Err(err) => return Err(OptionParseErr::External(err)),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use super::*;
        use crate::Serializer;

        #[derive(Debug)]
        struct DummyOptionsImpl;

        impl OptionsImplLayout for DummyOptionsImpl {
            type Error = ();
        }

        impl<'a> OptionsImpl<'a> for DummyOptionsImpl {
            type Option = (u8, Vec<u8>);

            fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Option>, Self::Error> {
                let mut v = Vec::new();
                v.extend_from_slice(data);
                Ok(Some((kind, v)))
            }
        }

        impl<'a> OptionsSerializerImpl<'a> for DummyOptionsImpl {
            type Option = (u8, Vec<u8>);

            fn option_length(option: &Self::Option) -> usize {
                option.1.len()
            }

            fn option_kind(option: &Self::Option) -> u8 {
                option.0
            }

            fn serialize(data: &mut [u8], option: &Self::Option) {
                data.copy_from_slice(&option.1);
            }
        }

        impl<'a> AlignedOptionsSerializerImpl<'a> for DummyOptionsImpl {
            // For our `DummyOption`, we simply regard (length, kind) as their
            // alignment requirement.
            fn alignment_requirement(option: &Self::Option) -> (usize, usize) {
                (option.1.len(), option.0 as usize)
            }

            fn serialize_padding(buf: &mut [u8], length: usize) {
                assert!(length <= buf.len());
                assert!(length <= (std::u8::MAX as usize) + 2);

                if length == 1 {
                    // Use Pad1
                    buf[0] = 0
                } else if length > 1 {
                    // Use PadN
                    buf[0] = 1;
                    buf[1] = (length - 2) as u8;
                    for i in 2..length {
                        buf[i] = 0
                    }
                }
            }
        }

        #[derive(Debug)]
        struct AlwaysErrOptionsImpl;

        impl OptionsImplLayout for AlwaysErrOptionsImpl {
            type Error = ();
        }

        impl<'a> OptionsImpl<'a> for AlwaysErrOptionsImpl {
            type Option = ();

            fn parse(_kind: u8, _data: &'a [u8]) -> Result<Option<()>, ()> {
                Err(())
            }
        }

        #[derive(Debug)]
        struct DummyNdpOptionsImpl;

        impl OptionsImplLayout for DummyNdpOptionsImpl {
            type Error = ();

            const OPTION_LEN_MULTIPLIER: usize = 8;

            const END_OF_OPTIONS: Option<u8> = None;

            const NOP: Option<u8> = None;
        }

        impl<'a> OptionsImpl<'a> for DummyNdpOptionsImpl {
            type Option = (u8, Vec<u8>);

            fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Option>, Self::Error> {
                let mut v = Vec::with_capacity(data.len());
                v.extend_from_slice(data);
                Ok(Some((kind, v)))
            }
        }

        impl<'a> OptionsSerializerImpl<'a> for DummyNdpOptionsImpl {
            type Option = (u8, Vec<u8>);

            fn option_length(option: &Self::Option) -> usize {
                option.1.len()
            }

            fn option_kind(option: &Self::Option) -> u8 {
                option.0
            }

            fn serialize(data: &mut [u8], option: &Self::Option) {
                data.copy_from_slice(&option.1)
            }
        }

        #[test]
        fn test_empty_options() {
            // all END_OF_OPTIONS
            let bytes = [END_OF_OPTIONS; 64];
            let options = Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap();
            assert_eq!(options.iter().count(), 0);

            // all NOP
            let bytes = [NOP; 64];
            let options = Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap();
            assert_eq!(options.iter().count(), 0);
        }

        #[test]
        fn test_parse() {
            // Construct byte sequences in the pattern [3, 2], [4, 3, 2], [5, 4,
            // 3, 2], etc. The second byte is the length byte, so these are all
            // valid options (with data [], [2], [3, 2], etc).
            let mut bytes = Vec::new();
            for i in 4..16 {
                // from the user's perspective, these NOPs should be transparent
                bytes.push(NOP);
                for j in (2..i).rev() {
                    bytes.push(j);
                }
                // from the user's perspective, these NOPs should be transparent
                bytes.push(NOP);
            }

            let options = Options::<_, DummyOptionsImpl>::parse(bytes.as_slice()).unwrap();
            for (idx, (kind, data)) in options.iter().enumerate() {
                assert_eq!(kind as usize, idx + 3);
                assert_eq!(data.len(), idx);
                let mut bytes = Vec::new();
                for i in (2..(idx + 2)).rev() {
                    bytes.push(i as u8);
                }
                assert_eq!(data, bytes);
            }

            // Test that we get no parse errors so long as
            // AlwaysErrOptionsImpl::parse is never called.
            let bytes = [NOP; 64];
            let options = Options::<_, AlwaysErrOptionsImpl>::parse(&bytes[..]).unwrap();
            assert_eq!(options.iter().count(), 0);
        }

        #[test]
        fn test_parse_ndp_options() {
            let mut bytes = Vec::new();
            for i in 0..16 {
                bytes.push(i);
                // NDP uses len*8 for the actual length.
                bytes.push(i + 1);
                // Write remaining 6 bytes.
                for j in 2..((i + 1) * 8) {
                    bytes.push(j)
                }
            }

            let options = Options::<_, DummyNdpOptionsImpl>::parse(bytes.as_slice()).unwrap();
            for (idx, (kind, data)) in options.iter().enumerate() {
                assert_eq!(kind as usize, idx);
                assert_eq!(data.len(), ((idx + 1) * 8) - 2);
                let mut bytes = Vec::new();
                for i in 2..((idx + 1) * 8) {
                    bytes.push(i as u8);
                }
                assert_eq!(data, bytes);
            }
        }

        #[test]
        fn test_parse_err() {
            // the length byte is too short
            let bytes = [2, 1];
            assert_eq!(
                Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::Internal
            );

            // the length byte is 0 (similar check to above, but worth
            // explicitly testing since this was a bug in the Linux kernel:
            // https://bugzilla.redhat.com/show_bug.cgi?id=1622404)
            let bytes = [2, 0];
            assert_eq!(
                Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::Internal
            );

            // the length byte is too long
            let bytes = [2, 3];
            assert_eq!(
                Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::Internal
            );

            // the buffer is fine, but the implementation returns a parse error
            let bytes = [2, 2];
            assert_eq!(
                Options::<_, AlwaysErrOptionsImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr::External(())
            );
        }

        #[test]
        fn test_missing_length_bytes() {
            // Construct a sequence with a valid record followed by an
            // incomplete one, where `kind` is specified but `len` is missing.
            // So we can assert that we'll fail cleanly in that case.
            //
            // Added as part of Change-Id
            // Ibd46ac7384c7c5e0d74cb344b48c88876c351b1a
            //
            // Before the small refactor in the Change-Id above, there was a
            // check during parsing that guaranteed that the length of the
            // remaining buffer was >= 1, but it should've been a check for
            // >= 2, and the case below would have caused it to panic while
            // trying to access the length byte, which was a DoS vulnerability.
            Options::<_, DummyOptionsImpl>::parse(&[0x03, 0x03, 0x01, 0x03][..])
                .expect_err("Can detect malformed length bytes");
        }

        #[test]
        fn test_parse_and_serialize() {
            // Construct byte sequences in the pattern [3, 2], [4, 3, 2], [5, 4,
            // 3, 2], etc. The second byte is the length byte, so these are all
            // valid options (with data [], [2], [3, 2], etc).
            let mut bytes = Vec::new();
            for i in 4..16 {
                // from the user's perspective, these NOPs should be transparent
                for j in (2..i).rev() {
                    bytes.push(j);
                }
            }

            let options = Options::<_, DummyOptionsImpl>::parse(bytes.as_slice()).unwrap();

            let collected = options
                .iter()
                .collect::<Vec<<DummyOptionsImpl as OptionsSerializerImpl<'_>>::Option>>();
            let ser = OptionsSerializer::<DummyOptionsImpl, _, _>::new(collected.iter());

            let serialized = ser.into_serializer().serialize_vec_outer().unwrap().as_ref().to_vec();

            assert_eq!(serialized, bytes);
        }

        #[test]
        fn test_parse_and_serialize_ndp() {
            let mut bytes = Vec::new();
            for i in 0..16 {
                bytes.push(i);
                // NDP uses len*8 for the actual length.
                bytes.push(i + 1);
                // Write remaining 6 bytes.
                for j in 2..((i + 1) * 8) {
                    bytes.push(j)
                }
            }
            let options = Options::<_, DummyNdpOptionsImpl>::parse(bytes.as_slice()).unwrap();
            let collected = options
                .iter()
                .collect::<Vec<<DummyNdpOptionsImpl as OptionsSerializerImpl<'_>>::Option>>();
            let ser = OptionsSerializer::<DummyNdpOptionsImpl, _, _>::new(collected.iter());

            let serialized = ser.into_serializer().serialize_vec_outer().unwrap().as_ref().to_vec();

            assert_eq!(serialized, bytes);
        }

        #[test]
        fn test_align_up_to() {
            // We are doing some sort of property testing here:
            // We generate a random alignment requirement (x, y) and a random offset `pos`.
            // The resulting `new_pos` must:
            //   - 1. be at least as large as the original `pos`.
            //   - 2. be in form of x * n + y for some integer n.
            //   - 3. for any number in between, they shouldn't be in form of x * n + y.
            use rand::{thread_rng, Rng};
            let mut rng = thread_rng();
            for _ in 0..100_000 {
                let x = rng.gen_range(1usize, 256);
                let y = rng.gen_range(0, x);
                let pos = rng.gen_range(0usize, 65536);
                let new_pos = align_up_to(pos, x, y);
                // 1)
                assert!(new_pos >= pos);
                // 2)
                assert_eq!((new_pos - y) % x, 0);
                // 3) Note: `p` is not guaranteed to be bigger than `y`, plus `x` to avoid overflow.
                assert!((pos..new_pos).all(|p| (p + x - y) % x != 0))
            }
        }

        #[test]
        #[rustfmt::skip]
        fn test_aligned_dummy_options_serializer() {
            // testing for cases: 2n+{0,1}, 3n+{1,2}, 1n+0, 4n+2
            let dummy_options = [
                // alignment requirement: 2 * n + 1,
                //
                (1, vec![42, 42]),
                (0, vec![42, 42]),
                (1, vec![1, 2, 3]),
                (2, vec![3, 2, 1]),
                (0, vec![42]),
                (2, vec![9, 9, 9, 9]),
            ];
            let ser = AlignedRecordsSerializer::<'_, DummyOptionsImpl, (u8, Vec<u8>), _>::new(
                0,
                dummy_options.iter(),
            );
            assert_eq!(ser.records_bytes_len(), 32);
            let mut buf = [0u8; 32];
            ser.serialize_records(&mut buf[..]);
            assert_eq!(
                &buf[..],
                &[
                    0, // Pad1 padding
                    1, 4, 42, 42, // (1, [42, 42]) starting at 2 * 0 + 1 = 3
                    0,  // Pad1 padding
                    0, 4, 42, 42, // (0, [42, 42]) starting at 2 * 3 + 0 = 6
                    1, 5, 1, 2, 3, // (1, [1, 2, 3]) starting at 3 * 2 + 1 = 7
                    1, 0, // PadN padding
                    2, 5, 3, 2, 1, // (2, [3, 2, 1]) starting at 3 * 4 + 2 = 14
                    0, 3, 42, // (0, [42]) starting at 1 * 19 + 0 = 19
                    0,  // PAD1 padding
                    2, 6, 9, 9, 9, 9 // (2, [9, 9, 9, 9]) starting at 4 * 6 + 2 = 26
                    // total length: 32
                ]
            );
        }
    }
}
