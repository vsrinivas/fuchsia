// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities for parsing and serializing sequential records.
//!
//! This module provides utilities for parsing and serializing repeated,
//! sequential records. Examples of packet formats which include such records
//! include IPv4, IPv6, TCP, NDP, and IGMP.
//!
//! The utilities in this module are very flexible and generic. The user must
//! supply a number of details about the format in order for parsing and
//! serializing to work.
//!
//! Some packet formats use a [type-length-value]-like encoding for options.
//! Examples include IPv4, TCP, and NDP options. Special support for these
//! formats is provided by the [`options`] submodule.
//!
//! [type-length-value]: https://en.wikipedia.org/wiki/Type-length-value

use core::borrow::Borrow;
use core::convert::Infallible as Never;
use core::marker::PhantomData;
use core::ops::Deref;

use zerocopy::ByteSlice;

use crate::serialize::InnerPacketBuilder;
use crate::util::{FromRaw, MaybeParsed};
use crate::{BufferView, BufferViewMut};

/// A type that encapsuates the result of a record parsing operation.
pub type RecordParseResult<T, E> = core::result::Result<ParsedRecord<T>, E>;

/// A type that encapsulates the successful result of a parsing operation.
pub enum ParsedRecord<T> {
    /// A record was successfully consumed and parsed.
    Parsed(T),

    /// A record was consumed but not parsed for non-fatal reasons.
    ///
    /// The caller should attempt to parse the next record to get a successfully
    /// parsed record.
    ///
    /// An example of a record that is skippable is a record used for padding.
    Skipped,

    /// All possible records have been already been consumed; there is nothing
    /// left to parse.
    ///
    /// The behavior is unspecified if callers attempt to parse another record.
    Done,
}

impl<T> ParsedRecord<T> {
    /// Does this result indicate that a record was consumed?
    ///
    /// Returns `true` for `Parsed` and `Skipped` and `false` for `Done`.
    pub fn consumed(&self) -> bool {
        match self {
            ParsedRecord::Parsed(_) | ParsedRecord::Skipped => true,
            ParsedRecord::Done => false,
        }
    }
}

/// A parsed sequence of records.
///
/// `Records` represents a pre-parsed sequence of records whose structure is
/// enforced by the impl in `R`.
#[derive(Debug, PartialEq)]
pub struct Records<B, R: RecordsImplLayout> {
    bytes: B,
    record_count: usize,
    context: R::Context,
}

/// An unchecked sequence of records.
///
/// `RecordsRaw` represents a not-yet-parsed and not-yet-validated sequence of
/// records, whose structure is enforced by the impl in `R`.
///
/// [`Records`] provides an implementation of [`FromRaw`] that can be used to
/// validate a `RecordsRaw`.
#[derive(Debug)]
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
    /// Raw-parses a sequence of records with a context.
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

    /// Raw-parses a sequence of records with a mutable context.
    ///
    /// `parse_raw_with_mut_context` shallowly parses `bytes` as a sequence of
    /// records. `context` may be used by implementers to maintain state.
    ///
    /// `parse_raw_with_mut_context` performs a single pass over all of the
    /// records to be able to find the end of the records list and update
    /// `bytes` accordingly. Upon return with [`MaybeParsed::Complete`],
    /// `bytes` will include only those bytes which are not part of the records
    /// list. Upon return with [`MaybeParsed::Incomplete`], `bytes` will still
    /// contain the bytes which could not be parsed, and all subsequent bytes.
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
}

impl<B, R> RecordsRaw<B, R>
where
    R: for<'a> RecordsRawImpl<'a> + RecordsImplLayout<Context = ()>,
    B: ByteSlice,
{
    /// Raw-parses a sequence of records.
    ///
    /// Equivalent to calling [`RecordsRaw::parse_raw_with_context`] with
    /// `context = ()`.
    pub fn parse_raw<BV: BufferView<B>>(bytes: &mut BV) -> MaybeParsed<Self, (B, R::Error)> {
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

impl<B: Deref<Target = [u8]>, R: RecordsImplLayout> RecordsRaw<B, R> {
    /// Gets the underlying bytes.
    ///
    /// `bytes` returns a reference to the byte slice backing this `RecordsRaw`.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}

/// An iterator over the records contained inside a [`Records`] instance.
#[derive(Copy, Clone, Debug)]
pub struct RecordsIter<'a, R: RecordsImpl<'a>> {
    bytes: &'a [u8],
    records_left: usize,
    context: R::Context,
}

/// The error returned when fewer records were found than expected.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct TooFewRecordsErr;

/// A counter used to keep track of how many records are remaining to be parsed.
///
/// Some record sequence formats include an indication of how many records
/// should be expected. For example, the [IGMPv3 Membership Report Message]
/// includes a "Number of Group Records" field in its header which indicates how
/// many Group Records are present following the header. A `RecordsCounter` is a
/// type used by these protocols to keep track of how many records are remaining
/// to be parsed. It is implemented for all unsigned numeric primitive types
/// (`usize`, `u8`, `u16`, `u32`, `u64`, and `u128`). A no-op implementation
/// which does not track the number of remaining records is provided for `()`.
///
/// [IGMPv3 Membership Report Message]: https://www.rfc-editor.org/rfc/rfc3376#section-4.2
pub trait RecordsCounter: Sized {
    /// The error returned from [`result_for_end_of_records`] when fewer records
    /// were found than expected.
    ///
    /// Some formats which store the number of records out-of-band consider it
    /// an error to provide fewer records than this out-of-band value.
    /// `TooFewRecordsErr` is the error returned by
    /// [`result_for_end_of_records`] when this condition is encountered. If the
    /// number of records is not tracked (usually, when `Self = ()`) or if it is
    /// not an error to provide fewer records than expected, it is recommended
    /// that `TooFewRecordsErr` be set to an uninhabited type like [`Never`].
    ///
    /// [`result_for_end_of_records`]: RecordsCounter::result_for_end_of_records
    type TooFewRecordsErr;

    /// Gets the next lowest value unless the counter is already at 0.
    ///
    /// During parsing, this value will be queried prior to parsing a record. If
    /// the counter has already reached zero (`next_lowest_value` returns
    /// `None`), parsing will be terminated. If the counter has not yet reached
    /// zero and a record is successfully parsed, the previous counter value
    /// will be overwritten with the one provided by `next_lowest_value`. In
    /// other words, the parsing logic will look something like the following
    /// pseudocode:
    ///
    /// ```rust,ignore
    /// let next = counter.next_lowest_value()?;
    /// let record = parse()?;
    /// *counter = next;
    /// ```
    ///
    /// If `Self` is a type which does not impose a limit on the number of
    /// records parsed (usually, `()`), `next_lowest_value` must always return
    /// `Some`. The value contained in the `Some` is irrelevant - it will just
    /// be written back verbatim after a record is successfully parsed.
    fn next_lowest_value(&self) -> Option<Self>;

    /// Gets a result which can be used to determine whether it is an error that
    /// there are no more records left to parse.
    ///
    /// Some formats which store the number of records out-of-band consider it
    /// an error to provide fewer records than this out-of-band value.
    /// `result_for_end_of_records` is called when there are no more records
    /// left to parse. If the counter is still at a non-zero value, and the
    /// protocol considers this to be an error, `result_for_end_of_records`
    /// should return an appropriate error. Otherwise, it should return
    /// `Ok(())`.
    fn result_for_end_of_records(&self) -> Result<(), Self::TooFewRecordsErr> {
        Ok(())
    }
}

/// The context kept while performing records parsing.
///
/// Types which implement `RecordsContext` can be used as the long-lived context
/// which is kept during records parsing. This context allows parsers to keep
/// running computations over the span of multiple records.
pub trait RecordsContext: Sized + Clone {
    /// A counter used to keep track of how many records are left to parse.
    ///
    /// See the documentation on [`RecordsCounter`] for more details.
    type Counter: RecordsCounter;

    /// Clones a context for iterator purposes.
    ///
    /// `clone_for_iter` is useful for cloning a context to be used by
    /// [`RecordsIter`]. Since [`Records::parse_with_context`] will do a full
    /// pass over all the records to check for errors, a `RecordsIter` should
    /// never error. Therefore, instead of doing checks when iterating (if a
    /// context was used for checks), a clone of a context can be made
    /// specifically for iterator purposes that does not do checks (which may be
    /// expensive).
    ///
    /// The default implementation of this method is equivalent to
    /// [`Clone::clone`].
    fn clone_for_iter(&self) -> Self {
        self.clone()
    }

    /// Gets the counter mutably.
    fn counter_mut(&mut self) -> &mut Self::Counter;
}

macro_rules! impl_records_counter_and_context_for_uxxx {
    ($ty:ty) => {
        impl RecordsCounter for $ty {
            type TooFewRecordsErr = TooFewRecordsErr;

            fn next_lowest_value(&self) -> Option<Self> {
                self.checked_sub(1)
            }

            fn result_for_end_of_records(&self) -> Result<(), TooFewRecordsErr> {
                if *self == 0 {
                    Ok(())
                } else {
                    Err(TooFewRecordsErr)
                }
            }
        }

        impl RecordsContext for $ty {
            type Counter = $ty;

            fn counter_mut(&mut self) -> &mut $ty {
                self
            }
        }
    };
}

impl_records_counter_and_context_for_uxxx!(usize);
impl_records_counter_and_context_for_uxxx!(u128);
impl_records_counter_and_context_for_uxxx!(u64);
impl_records_counter_and_context_for_uxxx!(u32);
impl_records_counter_and_context_for_uxxx!(u16);
impl_records_counter_and_context_for_uxxx!(u8);

impl RecordsCounter for () {
    type TooFewRecordsErr = Never;

    fn next_lowest_value(&self) -> Option<()> {
        Some(())
    }
}

impl RecordsContext for () {
    type Counter = ();

    fn counter_mut(&mut self) -> &mut () {
        self
    }
}

/// Basic associated types used by a [`RecordsImpl`].
///
/// This trait is kept separate from `RecordsImpl` so that the associated types
/// do not depend on the lifetime parameter to `RecordsImpl`.
pub trait RecordsImplLayout {
    // TODO(https://github.com/rust-lang/rust/issues/29661): Give the `Context`
    // type a default of `()`.

    /// A context type that can be used to maintain state while parsing multiple
    /// records.
    type Context: RecordsContext;

    /// The type of errors that may be returned by a call to
    /// [`RecordsImpl::parse_with_context`].
    type Error: From<
        <<Self::Context as RecordsContext>::Counter as RecordsCounter>::TooFewRecordsErr,
    >;
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
    /// [`parse_with_context`]: RecordsImpl::parse_with_context
    type Record;

    /// Parses a record with some context.
    ///
    /// `parse_with_context` takes a variable-length `data` and a `context` to
    /// maintain state.
    ///
    /// `data` may be empty. It is up to the implementer to handle an exhausted
    /// `data`.
    ///
    /// When returning `Ok(ParsedRecord::Skipped)`, it's the implementer's
    /// responsibility to consume the bytes of the record from `data`. If this
    /// doesn't happen, then `parse_with_context` will be called repeatedly on
    /// the same `data`, and the program will be stuck in an infinite loop. If
    /// the implementation is unable to determine how many bytes to consume from
    /// `data` in order to skip the record, `parse_with_context` must return
    /// `Err`.
    ///
    /// `parse_with_context` must be deterministic, or else
    /// [`Records::parse_with_context`] cannot guarantee that future iterations
    /// will not produce errors (and thus panic).
    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> RecordParseResult<Self::Record, Self::Error>;
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

/// A builder capable of serializing a record.
///
/// Given `R: RecordBuilder`, an iterator of `R` can be used with a
/// [`RecordSequenceBuilder`] to serialize a sequence of records.
pub trait RecordBuilder {
    /// Provides the serialized length of a record.
    ///
    /// Returns the total length, in bytes, of the serialized encoding of
    /// `self`.
    fn serialized_len(&self) -> usize;

    /// Serializes `self` into a buffer.
    ///
    /// `data` will be exactly `self.serialized_len()` bytes long.
    ///
    /// # Panics
    ///
    /// May panic if `data` is not exactly `self.serialized_len()` bytes long.
    fn serialize_into(&self, data: &mut [u8]);
}

/// A builder capable of serializing a record with an alignment requirement.
///
/// Given `R: AlignedRecordBuilder`, an iterator of `R` can be used with an
/// [`AlignedRecordSequenceBuilder`] to serialize a sequence of aligned records.
pub trait AlignedRecordBuilder: RecordBuilder {
    /// Returns the alignment requirement of `self`.
    ///
    /// The alignment requirement is returned as `(x, y)`, which means that the
    /// record must be aligned at  `x * n + y` bytes from the beginning of the
    /// records sequence for some non-negative `n`.
    ///
    /// It is guaranteed that `x > 0` and that `x > y`.
    fn alignment_requirement(&self) -> (usize, usize);

    /// Serializes the padding between subsequent aligned records.
    ///
    /// Some formats require that padding bytes have particular content. This
    /// function serializes padding bytes as required by the format.
    fn serialize_padding(buf: &mut [u8], length: usize);
}

/// A builder capable of serializing a sequence of records.
///
/// A `RecordSequenceBuilder` is instantiated with an [`Iterator`] that provides
/// [`RecordBuilder`]s to be serialized. The item produced by the iterator can
/// be any type which implements `Borrow<R>` for `R: RecordBuilder`.
///
/// `RecordSequenceBuilder` implements [`InnerPacketBuilder`].
#[derive(Debug)]
pub struct RecordSequenceBuilder<R, I> {
    records: I,
    _marker: PhantomData<R>,
}

impl<R, I> RecordSequenceBuilder<R, I> {
    /// Creates a new `RecordSequenceBuilder` with the given `records`.
    ///
    /// `records` must produce the same sequence of values from every iteration,
    /// even if cloned. Serialization is typically performed with two passes on
    /// `records`: one to calculate the total length in bytes (`serialized_len`)
    /// and another one to serialize to a buffer (`serialize_into`). Violating
    /// this rule may result in panics or malformed serialized record sequences.
    pub fn new(records: I) -> Self {
        Self { records, _marker: PhantomData }
    }
}

impl<R, I> RecordSequenceBuilder<R, I>
where
    R: RecordBuilder,
    I: Iterator + Clone,
    I::Item: Borrow<R>,
{
    /// Returns the total length, in bytes, of the serialized encoding of the
    /// records contained within `self`.
    pub fn serialized_len(&self) -> usize {
        self.records.clone().map(|r| r.borrow().serialized_len()).sum()
    }

    /// Serializes all the records contained within `self` into the given
    /// buffer.
    ///
    /// # Panics
    ///
    /// `serialize_into` expects that `buffer` has enough bytes to serialize the
    /// contained records (as obtained from `serialized_len`), otherwise it's
    /// considered a violation of the API contract and the call may panic.
    pub fn serialize_into(&self, buffer: &mut [u8]) {
        let mut b = &mut &mut buffer[..];
        for r in self.records.clone() {
            // SECURITY: Take a zeroed buffer from b to prevent leaking
            // information from packets previously stored in this buffer.
            r.borrow().serialize_into(b.take_front_zero(r.borrow().serialized_len()).unwrap());
        }
    }
}

impl<R, I> InnerPacketBuilder for RecordSequenceBuilder<R, I>
where
    R: RecordBuilder,
    I: Iterator + Clone,
    I::Item: Borrow<R>,
{
    fn bytes_len(&self) -> usize {
        self.serialized_len()
    }

    fn serialize(&self, buffer: &mut [u8]) {
        self.serialize_into(buffer)
    }
}

/// A builder capable of serializing a sequence of aligned records.
///
/// An `AlignedRecordSequenceBuilder` is instantiated with an [`Iterator`] that
/// provides [`AlignedRecordBuilder`]s to be serialized. The item produced by
/// the iterator can be any type which implements `Borrow<R>` for `R:
/// AlignedRecordBuilder`.
///
/// `AlignedRecordSequenceBuilder` implements [`InnerPacketBuilder`].
#[derive(Debug)]
pub struct AlignedRecordSequenceBuilder<R, I> {
    start_pos: usize,
    records: I,
    _marker: PhantomData<R>,
}

impl<R, I> AlignedRecordSequenceBuilder<R, I> {
    /// Creates a new `AlignedRecordSequenceBuilder` with given `records` and
    /// `start_pos`.
    ///
    /// `records` must produce the same sequence of values from every iteration,
    /// even if cloned. See [`RecordSequenceBuilder`] for more details.
    ///
    /// Alignment is calculated relative to the beginning of a virtual space of
    /// bytes. If non-zero, `start_pos` instructs the serializer to consider the
    /// buffer passed to [`serialize_into`] to start at the byte `start_pos`
    /// within this virtual space, and to calculate alignment and padding
    /// accordingly. For example, in the IPv6 Hop-by-Hop extension header, a
    /// fixed header of two bytes precedes that extension header's options, but
    /// alignment is calculated relative to the beginning of the extension
    /// header, not relative to the beginning of the options. Thus, when
    /// constructing an `AlignedRecordSequenceBuilder` to serialize those
    /// options, `start_pos` would be 2.
    ///
    /// [`serialize_into`]: AlignedRecordSequenceBuilder::serialize_into
    pub fn new(start_pos: usize, records: I) -> Self {
        Self { start_pos, records, _marker: PhantomData }
    }
}

impl<R, I> AlignedRecordSequenceBuilder<R, I>
where
    R: AlignedRecordBuilder,
    I: Iterator + Clone,
    I::Item: Borrow<R>,
{
    /// Returns the total length, in bytes, of the serialized records contained
    /// within `self`.
    ///
    /// Note that this length includes all padding required to ensure that all
    /// records satisfy their alignment requirements.
    pub fn serialized_len(&self) -> usize {
        let mut pos = self.start_pos;
        self.records
            .clone()
            .map(|r| {
                let (x, y) = r.borrow().alignment_requirement();
                let new_pos = align_up_to(pos, x, y) + r.borrow().serialized_len();
                let result = new_pos - pos;
                pos = new_pos;
                result
            })
            .sum()
    }

    /// Serializes all the records contained within `self` into the given
    /// buffer.
    ///
    /// # Panics
    ///
    /// `serialize_into` expects that `buffer` has enough bytes to serialize the
    /// contained records (as obtained from `serialized_len`), otherwise it's
    /// considered a violation of the API contract and the call may panic.
    pub fn serialize_into(&self, buffer: &mut [u8]) {
        let mut b = &mut &mut buffer[..];
        let mut pos = self.start_pos;
        for r in self.records.clone() {
            let (x, y) = r.borrow().alignment_requirement();
            let aligned = align_up_to(pos, x, y);
            let pad_len = aligned - pos;
            let pad = b.take_front_zero(pad_len).unwrap();
            R::serialize_padding(pad, pad_len);
            pos = aligned;
            // SECURITY: Take a zeroed buffer from b to prevent leaking
            // information from packets previously stored in this buffer.
            r.borrow().serialize_into(b.take_front_zero(r.borrow().serialized_len()).unwrap());
            pos += r.borrow().serialized_len();
        }
        // we have to pad the containing header to 8-octet boundary.
        let padding = b.take_rest_front_zero();
        R::serialize_padding(padding, padding.len());
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

impl<B, R> Records<B, R>
where
    B: ByteSlice,
    R: for<'a> RecordsImpl<'a>,
{
    /// Parses a sequence of records with a context.
    ///
    /// See [`parse_with_mut_context`] for details on `bytes`, `context`, and
    /// return value. `parse_with_context` just calls `parse_with_mut_context`
    /// with a mutable reference to the `context` which is passed by value to
    /// this function.
    ///
    /// [`parse_with_mut_context`]: Records::parse_with_mut_context
    pub fn parse_with_context(
        bytes: B,
        mut context: R::Context,
    ) -> Result<Records<B, R>, R::Error> {
        Self::parse_with_mut_context(bytes, &mut context)
    }

    /// Parses a sequence of records with a mutable context.
    ///
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
        let mut record_count = 0;
        while next::<_, R>(&mut b, context)?.is_some() {
            record_count += 1;
        }
        Ok(Records { bytes, record_count, context: c })
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
    /// [`parse_with_context`]: Records::parse_with_context
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
    /// Iterates over options.
    ///
    /// Since the records were validated in [`parse`], then so long as
    /// [`R::parse_with_context`] is deterministic, the iterator is infallible.
    ///
    /// [`parse`]: Records::parse
    /// [`R::parse_with_context`]: RecordsImpl::parse_with_context
    pub fn iter(&'a self) -> RecordsIter<'a, R> {
        RecordsIter {
            bytes: &self.bytes,
            records_left: self.record_count,
            context: self.context.clone_for_iter(),
        }
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
        if result.is_some() {
            self.records_left -= 1;
        }
        self.bytes = bytes.into_rest();
        result
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.records_left, Some(self.records_left))
    }
}

impl<'a, R> ExactSizeIterator for RecordsIter<'a, R>
where
    R: RecordsImpl<'a>,
{
    fn len(&self) -> usize {
        self.records_left
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
        // If we're already at 0, don't attempt to parse any more records.
        let next_lowest_counter_val = match context.counter_mut().next_lowest_value() {
            Some(val) => val,
            None => return Ok(None),
        };
        match R::parse_with_context(bytes, context)? {
            ParsedRecord::Done => {
                return context
                    .counter_mut()
                    .result_for_end_of_records()
                    .map_err(Into::into)
                    .map(|()| None);
            }
            ParsedRecord::Skipped => {}
            ParsedRecord::Parsed(o) => {
                *context.counter_mut() = next_lowest_counter_val;
                return Ok(Some(o));
            }
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
    /// `'a`.
    ///
    /// All slices returned by the `BufferView` impl of `LongLivedBuff` are
    /// guaranteed to return slice references tied to the same lifetime `'a`.
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
            self.0 = rest;
            Some(prefix)
        } else {
            None
        }
    }

    fn take_back(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.0.len() >= n {
            let (rest, suffix) = core::mem::replace(&mut self.0, &[]).split_at(n);
            self.0 = rest;
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
mod tests {
    use test_case::test_case;
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

    use super::*;

    const DUMMY_BYTES: [u8; 16] = [
        0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03,
        0x04,
    ];

    fn get_empty_tuple_mut_ref<'a>() -> &'a mut () {
        // This is a hack since `&mut ()` is invalid.
        let bytes: &mut [u8] = &mut [];
        zerocopy::LayoutVerified::<_, ()>::new_unaligned(bytes).unwrap().into_mut()
    }

    #[derive(Debug, AsBytes, FromBytes, Unaligned)]
    #[repr(C)]
    struct DummyRecord {
        a: [u8; 2],
        b: u8,
        c: u8,
    }

    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    enum DummyRecordErr {
        Parse,
        TooFewRecords,
    }

    impl From<Never> for DummyRecordErr {
        fn from(err: Never) -> DummyRecordErr {
            match err {}
        }
    }

    impl From<TooFewRecordsErr> for DummyRecordErr {
        fn from(_: TooFewRecordsErr) -> DummyRecordErr {
            DummyRecordErr::TooFewRecords
        }
    }

    fn parse_dummy_rec<'a, BV>(
        data: &mut BV,
    ) -> RecordParseResult<LayoutVerified<&'a [u8], DummyRecord>, DummyRecordErr>
    where
        BV: BufferView<&'a [u8]>,
    {
        if data.is_empty() {
            return Ok(ParsedRecord::Done);
        }

        match data.take_obj_front::<DummyRecord>() {
            Some(res) => Ok(ParsedRecord::Parsed(res)),
            None => Err(DummyRecordErr::Parse),
        }
    }

    //
    // Context-less records
    //

    #[derive(Debug)]
    struct ContextlessRecordImpl;

    impl RecordsImplLayout for ContextlessRecordImpl {
        type Context = ();
        type Error = DummyRecordErr;
    }

    impl<'a> RecordsImpl<'a> for ContextlessRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            _context: &mut Self::Context,
        ) -> RecordParseResult<Self::Record, Self::Error> {
            parse_dummy_rec(data)
        }
    }

    //
    // Limit context records
    //

    #[derive(Debug)]
    struct LimitContextRecordImpl;

    impl RecordsImplLayout for LimitContextRecordImpl {
        type Context = usize;
        type Error = DummyRecordErr;
    }

    impl<'a> RecordsImpl<'a> for LimitContextRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            _context: &mut usize,
        ) -> RecordParseResult<Self::Record, Self::Error> {
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

    impl RecordsContext for FilterContext {
        type Counter = ();
        fn counter_mut(&mut self) -> &mut () {
            get_empty_tuple_mut_ref()
        }
    }

    impl RecordsImplLayout for FilterContextRecordImpl {
        type Context = FilterContext;
        type Error = DummyRecordErr;
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
        ) -> RecordParseResult<Self::Record, Self::Error> {
            if bytes.len() < core::mem::size_of::<DummyRecord>() {
                Ok(ParsedRecord::Done)
            } else if bytes.as_ref()[0..core::mem::size_of::<DummyRecord>()]
                .iter()
                .any(|x| context.disallowed[*x as usize])
            {
                Err(DummyRecordErr::Parse)
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
        type Error = DummyRecordErr;
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
        type Counter = ();

        fn clone_for_iter(&self) -> Self {
            let mut x = self.clone();
            x.iter = true;
            x
        }

        fn counter_mut(&mut self) -> &mut () {
            get_empty_tuple_mut_ref()
        }
    }

    impl<'a> RecordsImpl<'a> for StatefulContextRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Self::Context,
        ) -> RecordParseResult<Self::Record, Self::Error> {
            if !context.iter {
                context.pre_parse_counter += 1;
            }

            let ret = parse_dummy_rec_with_context(data, context);

            if let Ok(ParsedRecord::Parsed(_)) = ret {
                if !context.iter {
                    context.post_parse_counter += 1;
                }
            }

            ret
        }
    }

    impl<'a> RecordsRawImpl<'a> for StatefulContextRecordImpl {
        fn parse_raw_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            context: &mut Self::Context,
        ) -> Result<bool, Self::Error> {
            Self::parse_with_context(data, context).map(|r| r.consumed())
        }
    }

    fn parse_dummy_rec_with_context<'a, BV>(
        data: &mut BV,
        context: &mut StatefulContext,
    ) -> RecordParseResult<LayoutVerified<&'a [u8], DummyRecord>, DummyRecordErr>
    where
        BV: BufferView<&'a [u8]>,
    {
        if data.is_empty() {
            return Ok(ParsedRecord::Done);
        }

        if !context.iter {
            context.parse_counter += 1;
        }

        match data.take_obj_front::<DummyRecord>() {
            Some(res) => Ok(ParsedRecord::Parsed(res)),
            None => Err(DummyRecordErr::Parse),
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

    #[test]
    fn all_records_parsing() {
        let parsed = Records::<_, ContextlessRecordImpl>::parse(&DUMMY_BYTES[..]).unwrap();
        let mut iter = parsed.iter();
        // Test ExactSizeIterator implementation.
        assert_eq!(iter.len(), 4);
        let mut cnt = 4;
        while let Some(_) = iter.next() {
            cnt -= 1;
            assert_eq!(iter.len(), cnt);
        }
        assert_eq!(iter.len(), 0);
        for rec in parsed.iter() {
            check_parsed_record(rec.deref());
        }
    }

    // `expect` is either the number of records that should have been parsed or
    // the error returned from the `Records` constructor.
    //
    // If there are more records than the limit, then we just truncate (not
    // parsing all of them) and don't return an error.
    #[test_case(0, Ok(0))]
    #[test_case(1, Ok(1))]
    #[test_case(2, Ok(2))]
    #[test_case(3, Ok(3))]
    // If there are the same number of records as the limit, then we
    // succeed.
    #[test_case(4, Ok(4))]
    // If there are fewer records than the limit, then we fail.
    #[test_case(5, Err(DummyRecordErr::TooFewRecords))]
    fn limit_records_parsing(limit: usize, expect: Result<usize, DummyRecordErr>) {
        // Test without mutable limit/context
        let check_result =
            |result: Result<Records<_, LimitContextRecordImpl>, _>| match (expect, result) {
                (Ok(expect_parsed), Ok(records)) => {
                    assert_eq!(records.iter().count(), expect_parsed);
                    for rec in records.iter() {
                        check_parsed_record(rec.deref());
                    }
                }
                (Err(expect), Err(got)) => assert_eq!(expect, got),
                (Ok(expect_parsed), Err(err)) => {
                    panic!("wanted {expect_parsed} successfully-parsed records; got error {err:?}")
                }
                (Err(expect), Ok(records)) => panic!(
                    "wanted error {expect:?}, got {} successfully-parsed records",
                    records.iter().count()
                ),
            };

        check_result(Records::<_, LimitContextRecordImpl>::parse_with_context(
            &DUMMY_BYTES[..],
            limit,
        ));
        let mut mut_limit = limit;
        check_result(Records::<_, LimitContextRecordImpl>::parse_with_mut_context(
            &DUMMY_BYTES[..],
            &mut mut_limit,
        ));
        if let Ok(expect_parsed) = expect {
            assert_eq!(limit - mut_limit, expect_parsed);
        }
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
        assert_eq!(
            Records::<_, FilterContextRecordImpl>::parse_with_context(&DUMMY_BYTES[..], context)
                .expect_err("fails if the buffer has an element with value 0x01"),
            DummyRecordErr::Parse
        );
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
    fn raw_parse_success() {
        let mut context = StatefulContext::new();
        let mut bv = &mut &DUMMY_BYTES[..];
        let result = RecordsRaw::<_, StatefulContextRecordImpl>::parse_raw_with_mut_context(
            &mut bv,
            &mut context,
        )
        .complete()
        .unwrap();
        let RecordsRaw { bytes, context: _ } = &result;
        assert_eq!(*bytes, &DUMMY_BYTES[..]);
        let parsed = Records::try_from_raw(result).unwrap();
        validate_parsed_stateful_context_records(parsed, context);
    }

    #[test]
    fn raw_parse_failure() {
        let mut context = StatefulContext::new();
        let mut bv = &mut &DUMMY_BYTES[0..15];
        let result = RecordsRaw::<_, StatefulContextRecordImpl>::parse_raw_with_mut_context(
            &mut bv,
            &mut context,
        )
        .incomplete()
        .unwrap();
        assert_eq!(result, (&DUMMY_BYTES[0..12], DummyRecordErr::Parse));
    }
}

/// Utilities for parsing the options formats in protocols like IPv4, TCP, and
/// NDP.
///
/// This module provides parsing utilities for [type-length-value]-like records
/// encodings like those used by the options in an IPv4 or TCP header or an NDP
/// packet. These formats are not identical, but share enough in common that the
/// utilities provided here only need a small amount of customization by the
/// user to be fully functional.
///
/// [type-length-value]: https://en.wikipedia.org/wiki/Type-length-value
pub mod options {
    use core::convert::TryFrom;
    use core::mem;
    use core::num::{NonZeroUsize, TryFromIntError};

    use nonzero_ext::nonzero;
    use zerocopy::{byteorder::ByteOrder, AsBytes, FromBytes, Unaligned};

    use super::*;

    /// A parsed sequence of options.
    ///
    /// `Options` represents a parsed sequence of options, for example from an
    /// IPv4 or TCP header or an NDP packet. `Options` uses [`Records`] under
    /// the hood.
    ///
    /// [`Records`]: crate::records::Records
    pub type Options<B, O> = Records<B, O>;

    /// A not-yet-parsed sequence of options.
    ///
    /// `OptionsRaw` represents a not-yet-parsed and not-yet-validated sequence
    /// of options, for example from an IPv4 or TCP header or an NDP packet.
    /// `OptionsRaw` uses [`RecordsRaw`] under the hood.
    ///
    /// [`RecordsRaw`]: crate::records::RecordsRaw
    pub type OptionsRaw<B, O> = RecordsRaw<B, O>;

    /// A builder capable of serializing a sequence of options.
    ///
    /// An `OptionSequenceBuilder` is instantiated with an [`Iterator`] that
    /// provides [`OptionBuilder`]s to be serialized. The item produced by the
    /// iterator can be any type which implements `Borrow<O>` for `O:
    /// OptionBuilder`.
    ///
    /// `OptionSequenceBuilder` implements [`InnerPacketBuilder`].
    pub type OptionSequenceBuilder<R, I> = RecordSequenceBuilder<R, I>;

    /// A builder capable of serializing a sequence of aligned options.
    ///
    /// An `AlignedOptionSequenceBuilder` is instantiated with an [`Iterator`]
    /// that provides [`AlignedOptionBuilder`]s to be serialized. The item
    /// produced by the iterator can be any type which implements `Borrow<O>`
    /// for `O: AlignedOptionBuilder`.
    ///
    /// `AlignedOptionSequenceBuilder` implements [`InnerPacketBuilder`].
    pub type AlignedOptionSequenceBuilder<R, I> = AlignedRecordSequenceBuilder<R, I>;

    impl<'a, O: OptionsImpl<'a>> RecordsImplLayout for O {
        type Context = ();
        type Error = O::Error;
    }

    impl<'a, O: OptionsImpl<'a>> RecordsImpl<'a> for O {
        type Record = O::Option;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            data: &mut BV,
            _context: &mut Self::Context,
        ) -> RecordParseResult<Self::Record, Self::Error> {
            next::<_, O>(data)
        }
    }

    impl<O: OptionBuilder> RecordBuilder for O {
        fn serialized_len(&self) -> usize {
            // TODO(https://fxbug.dev/77981): Remove this `.expect`
            <O::Layout as OptionLayout>::LENGTH_ENCODING
                .record_length::<<O::Layout as OptionLayout>::KindLenField>(
                    OptionBuilder::serialized_len(self),
                )
                .expect("integer overflow while computing record length")
        }

        fn serialize_into(&self, mut data: &mut [u8]) {
            // NOTE(brunodalbo) we don't currently support serializing the two
            //  single-byte options used in TCP and IP: NOP and END_OF_OPTIONS.
            //  If it is necessary to support those as part of TLV options
            //  serialization, some changes will be required here.

            // So that `data` implements `BufferViewMut`.
            let mut data = &mut data;

            // Data not having enough space is a contract violation, so we panic
            // in that case.
            *BufferView::<&mut [u8]>::take_obj_front::<<O::Layout as OptionLayout>::KindLenField>(&mut data)
                .expect("buffer too short") = self.option_kind();
            let body_len = OptionBuilder::serialized_len(self);
            // TODO(https://fxbug.dev/77981): Remove this `.expect`
            let length = <O::Layout as OptionLayout>::LENGTH_ENCODING
                .encode_length::<<O::Layout as OptionLayout>::KindLenField>(body_len)
                .expect("integer overflow while encoding length");
            // Length overflowing `O::Layout::KindLenField` is a contract
            // violation, so we panic in that case.
            *BufferView::<&mut [u8]>::take_obj_front::<<O::Layout as OptionLayout>::KindLenField>(&mut data)
                .expect("buffer too short") = length;
            // SECURITY: Because padding may have occurred, we zero-fill data
            // before passing it along in order to prevent leaking information
            // from packets previously stored in the buffer.
            let data = data.into_rest_zero();
            // Pass exactly `body_len` bytes even if there is padding.
            OptionBuilder::serialize_into(self, &mut data[..body_len]);
        }
    }

    impl<O: AlignedOptionBuilder> AlignedRecordBuilder for O {
        fn alignment_requirement(&self) -> (usize, usize) {
            // Use the underlying option's alignment requirement as the
            // alignment requirement for the record.
            AlignedOptionBuilder::alignment_requirement(self)
        }

        fn serialize_padding(buf: &mut [u8], length: usize) {
            <O as AlignedOptionBuilder>::serialize_padding(buf, length);
        }
    }

    /// Whether the length field of an option encodes the length of the entire
    /// option (including kind and length fields) or only of the value field.
    ///
    /// For the `TypeLengthValue` variant, an `option_len_multiplier` may also
    /// be specified. Some formats (such as NDP) do not directly encode the
    /// length in bytes of each option, but instead encode a number which must
    /// be multiplied by `option_len_multiplier` in order to get the length in
    /// bytes.
    #[derive(Copy, Clone, Eq, PartialEq)]
    pub enum LengthEncoding {
        TypeLengthValue { option_len_multiplier: NonZeroUsize },
        ValueOnly,
    }

    impl LengthEncoding {
        /// Computes the length of an entire option record - including kind and
        /// length fields - from the length of an option body.
        ///
        /// `record_length` takes into account the length of the kind and length
        /// fields and also adds any padding required to reach a multiple of
        /// `option_len_multiplier`, returning `None` if the value cannot be
        /// stored in a `usize`.
        fn record_length<F: KindLenField>(self, option_body_len: usize) -> Option<usize> {
            let unpadded_len = option_body_len.checked_add(2 * mem::size_of::<F>())?;
            match self {
                LengthEncoding::TypeLengthValue { option_len_multiplier } => {
                    round_up(unpadded_len, option_len_multiplier)
                }
                LengthEncoding::ValueOnly => Some(unpadded_len),
            }
        }

        /// Encodes the length of an option's body.
        ///
        /// `option_body_len` is the length in bytes of the body option as
        /// returned from [`OptionsSerializerImpl::option_length`]. This value
        /// does not include the kind, length, or padding bytes.
        ///
        /// `encode_length` computes the value which should be stored in the
        /// length field, returning `None` if the value cannot be stored in an
        /// `F`.
        fn encode_length<F: KindLenField>(self, option_body_len: usize) -> Option<F> {
            let len = match self {
                LengthEncoding::TypeLengthValue { option_len_multiplier } => {
                    let unpadded_len = (2 * mem::size_of::<F>()).checked_add(option_body_len)?;
                    let padded_len = round_up(unpadded_len, option_len_multiplier)?;
                    padded_len / option_len_multiplier.get()
                }
                LengthEncoding::ValueOnly => option_body_len,
            };
            match F::try_from(len) {
                Ok(len) => Some(len),
                Err(TryFromIntError { .. }) => None,
            }
        }

        /// Decodes the length of an option's body.
        ///
        /// `length_field` is the value of the length field. `decode_length`
        /// computes the length of the option's body which this value encodes,
        /// returning an error if `length_field` is invalid or if integer
        /// overflow occurs. `length_field` is invalid if it encodes a total
        /// length smaller than the header (specifically, if `self` is
        /// LengthEncoding::TypeLengthValue { option_len_multiplier }` and
        /// `length_field * option_len_multiplier < 2 * size_of::<F>()`).
        fn decode_length<F: KindLenField>(self, length_field: F) -> Option<usize> {
            let length_field = length_field.into();
            match self {
                LengthEncoding::TypeLengthValue { option_len_multiplier } => length_field
                    .checked_mul(option_len_multiplier.get())
                    .and_then(|product| product.checked_sub(2 * mem::size_of::<F>())),
                LengthEncoding::ValueOnly => Some(length_field),
            }
        }
    }

    /// Rounds up `x` to the next multiple of `mul` unless `x` is already a
    /// multiple of `mul`.
    fn round_up(x: usize, mul: NonZeroUsize) -> Option<usize> {
        let mul = mul.get();
        // - Subtracting 1 can't underflow because we just added `mul`, which is
        //   at least 1, and the addition didn't overflow
        // - Dividing by `mul` can't overflow (and can't divide by 0 because
        //   `mul` is nonzero)
        // - Multiplying by `mul` can't overflow because division rounds down,
        //   so the result of the multiplication can't be any larger than the
        //   numerator in `(x_times_mul - 1) / mul`, which we already know
        //   didn't overflow
        x.checked_add(mul).map(|x_times_mul| ((x_times_mul - 1) / mul) * mul)
    }

    /// The type of the "kind" and "length" fields in an option.
    ///
    /// See the docs for [`OptionLayout::KindLenField`] for more information.
    pub trait KindLenField:
        FromBytes
        + AsBytes
        + Unaligned
        + Into<usize>
        + TryFrom<usize, Error = TryFromIntError>
        + Eq
        + Copy
        + crate::sealed::Sealed
    {
    }

    impl crate::sealed::Sealed for u8 {}
    impl KindLenField for u8 {}
    impl<O: ByteOrder> crate::sealed::Sealed for zerocopy::U16<O> {}
    impl<O: ByteOrder> KindLenField for zerocopy::U16<O> {}

    /// Information about an option's layout.
    ///
    /// It is recommended that this trait be implemented for an uninhabited type
    /// since it never needs to be instantiated:
    ///
    /// ```rust
    /// # use packet::records::options::{OptionLayout, LengthEncoding};
    /// /// A carrier for information about the layout of the IPv4 option
    /// /// format.
    /// ///
    /// /// This type exists only at the type level, and does not need to be
    /// /// constructed.
    /// pub enum Ipv4OptionLayout {}
    ///
    /// impl OptionLayout for Ipv4OptionLayout {
    ///     type KindLenField = u8;
    /// }
    /// ```
    pub trait OptionLayout {
        /// The type of the "kind" and "length" fields in an option.
        ///
        /// For most protocols, this is simply `u8`, as the "kind" and "length"
        /// fields are each a single byte. For protocols which use two bytes for
        /// these fields, this is [`zerocopy::U16`].
        // TODO(https://github.com/rust-lang/rust/issues/29661): Have
        // `KindLenField` default to `u8`.
        type KindLenField: KindLenField;

        /// The encoding of the length byte.
        ///
        /// Some formats (such as IPv4) use the length field to encode the
        /// length of the entire option, including the kind and length bytes.
        /// Other formats (such as IPv6) use the length field to encode the
        /// length of only the value. This constant specifies which encoding is
        /// used.
        ///
        /// Additionally, some formats (such as NDP) do not directly encode the
        /// length in bytes of each option, but instead encode a number which
        /// must be multiplied by a constant in order to get the length in
        /// bytes. This is set using the [`TypeLengthValue`] variant's
        /// `option_len_multiplier` field, and it defaults to 1.
        ///
        /// [`TypeLengthValue`]: LengthEncoding::TypeLengthValue
        const LENGTH_ENCODING: LengthEncoding =
            LengthEncoding::TypeLengthValue { option_len_multiplier: nonzero!(1usize) };
    }

    /// An error encountered while parsing an option or sequence of options.
    pub trait OptionParseError: From<Never> {
        /// An error encountered while parsing a sequence of options.
        ///
        /// If an error is encountered while parsing a sequence of [`Options`],
        /// this is the error that will be emitted. This is the only type of
        /// error that can be generated by the [`Options`] parser itself. All
        /// other errors come from the user-provided [`OptionsImpl::parse`],
        /// which parses the data of a single option.
        const SEQUENCE_FORMAT_ERROR: Self;
    }

    /// An error encountered while parsing an option or sequence of options.
    ///
    /// `OptionParseErr` is a simple implementation of [`OptionParseError`] that
    /// doesn't carry information other than the fact that an error was
    /// encountered.
    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    pub struct OptionParseErr;

    impl From<Never> for OptionParseErr {
        fn from(err: Never) -> OptionParseErr {
            match err {}
        }
    }

    impl OptionParseError for OptionParseErr {
        const SEQUENCE_FORMAT_ERROR: OptionParseErr = OptionParseErr;
    }

    /// Information about an option's layout required in order to parse it.
    pub trait OptionParseLayout: OptionLayout {
        /// The type of errors that may be returned by a call to
        /// [`OptionsImpl::parse`].
        type Error: OptionParseError;

        /// The End of options kind (if one exists).
        const END_OF_OPTIONS: Option<Self::KindLenField>;

        /// The No-op kind (if one exists).
        const NOP: Option<Self::KindLenField>;
    }

    /// An implementation of an options parser.
    ///
    /// `OptionsImpl` provides functions to parse fixed- and variable-length
    /// options. It is required in order to construct an [`Options`].
    pub trait OptionsImpl<'a>: OptionParseLayout {
        /// The type of an option; the output from the [`parse`] function.
        ///
        /// For long or variable-length data, implementers are advised to make
        /// `Option` a reference into the bytes passed to `parse`. Such a
        /// reference will need to carry the lifetime `'a`, which is the same
        /// lifetime that is passed to `parse`, and is also the lifetime
        /// parameter to this trait.
        ///
        /// [`parse`]: crate::records::options::OptionsImpl::parse
        type Option;

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
        /// guarantee that future iterations will not produce errors (and thus
        /// panic).
        ///
        /// [`Options::parse`]: crate::records::Records::parse
        fn parse(
            kind: Self::KindLenField,
            data: &'a [u8],
        ) -> Result<Option<Self::Option>, Self::Error>;
    }

    /// A builder capable of serializing an option.
    ///
    /// Given `O: OptionBuilder`, an iterator of `O` can be used with a
    /// [`OptionSequenceBuilder`] to serialize a sequence of options.
    pub trait OptionBuilder {
        /// Information about the option's layout.
        type Layout: OptionLayout;

        /// Returns the serialized length, in bytes, of `self`.
        ///
        /// Implementers must return the length, in bytes, of the **data***
        /// portion of the option field (not counting the kind and length
        /// bytes). The internal machinery of options serialization takes care
        /// of aligning options to their [`option_len_multiplier`] boundaries,
        /// adding padding bytes if necessary.
        ///
        /// [`option_len_multiplier`]: LengthEncoding::TypeLengthValue::option_len_multiplier
        fn serialized_len(&self) -> usize;

        /// Returns the wire value for this option kind.
        fn option_kind(&self) -> <Self::Layout as OptionLayout>::KindLenField;

        /// Serializes `self` into `data`.
        ///
        /// `data` will be exactly `self.serialized_len()` bytes long.
        /// Implementers must write the **data** portion of `self` into `data`
        /// (not the kind or length fields).
        ///
        /// # Panics
        ///
        /// May panic if `data` is not exactly `self.serialized_len()` bytes
        /// long.
        fn serialize_into(&self, data: &mut [u8]);
    }

    /// A builder capable of serializing an option with an alignment
    /// requirement.
    ///
    /// Given `O: AlignedOptionBuilder`, an iterator of `O` can be used with an
    /// [`AlignedOptionSequenceBuilder`] to serialize a sequence of aligned
    /// options.
    pub trait AlignedOptionBuilder: OptionBuilder {
        /// Returns the alignment requirement of `self`.
        ///
        /// `option.alignment_requirement()` returns `(x, y)`, which means that
        /// the serialized encoding of `option` must be aligned at `x * n + y`
        /// bytes from the beginning of the options sequence for some
        /// non-negative `n`. For example, the IPv6 Router Alert Hop-by-Hop
        /// option has alignment (2, 0), while the Jumbo Payload option has
        /// alignment (4, 2). (1, 0) means there is no alignment requirement.
        ///
        /// `x` must be non-zero and `y` must be smaller than `x`.
        fn alignment_requirement(&self) -> (usize, usize);

        /// Serializes the padding between subsequent aligned options.
        ///
        /// Some formats require that padding bytes have particular content.
        /// This function serializes padding bytes as required by the format.
        fn serialize_padding(buf: &mut [u8], length: usize);
    }

    fn next<'a, BV, O>(bytes: &mut BV) -> RecordParseResult<O::Option, O::Error>
    where
        BV: BufferView<&'a [u8]>,
        O: OptionsImpl<'a>,
    {
        // For an explanation of this format, see the "Options" section of
        // https://en.wikipedia.org/wiki/Transmission_Control_Protocol#TCP_segment_structure
        loop {
            if bytes.len() == 0 {
                return Ok(ParsedRecord::Done);
            }
            let kind = match bytes.take_obj_front::<O::KindLenField>() {
                // Thanks to the preceding `if`, we know at this point that
                // `bytes.len() > 0`. If `take_obj_front` returns `None`, that
                // means that `bytes.len()` is shorter than `O::KindLenField`.
                None => return Err(O::Error::SEQUENCE_FORMAT_ERROR),
                Some(k) => {
                    // Can't do pattern matching with associated constants, so
                    // do it the good-ol' way:
                    if Some(*k) == O::NOP {
                        continue;
                    } else if Some(*k) == O::END_OF_OPTIONS {
                        return Ok(ParsedRecord::Done);
                    }
                    k
                }
            };
            let body_len = match bytes.take_obj_front::<O::KindLenField>() {
                None => return Err(O::Error::SEQUENCE_FORMAT_ERROR),
                Some(len) => O::LENGTH_ENCODING
                    .decode_length::<O::KindLenField>(*len)
                    .ok_or(O::Error::SEQUENCE_FORMAT_ERROR)?,
            };

            let option_data = bytes.take_front(body_len).ok_or(O::Error::SEQUENCE_FORMAT_ERROR)?;
            match O::parse(*kind, option_data) {
                Ok(Some(o)) => return Ok(ParsedRecord::Parsed(o)),
                Ok(None) => {}
                Err(err) => return Err(err),
            }
        }
    }

    #[cfg(test)]
    mod tests {
        use core::convert::TryInto as _;
        use core::fmt::Debug;

        use nonzero_ext::nonzero;
        use zerocopy::byteorder::network_endian::U16;

        use super::*;
        use crate::Serializer;

        #[derive(Debug)]
        struct DummyOptionsImpl;

        #[derive(Debug)]
        struct DummyOption {
            kind: u8,
            data: Vec<u8>,
        }

        impl OptionLayout for DummyOptionsImpl {
            type KindLenField = u8;
        }

        impl OptionParseLayout for DummyOptionsImpl {
            type Error = OptionParseErr;
            const END_OF_OPTIONS: Option<u8> = Some(0);
            const NOP: Option<u8> = Some(1);
        }

        impl<'a> OptionsImpl<'a> for DummyOptionsImpl {
            type Option = DummyOption;

            fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Option>, OptionParseErr> {
                let mut v = Vec::new();
                v.extend_from_slice(data);
                Ok(Some(DummyOption { kind, data: v }))
            }
        }

        impl OptionBuilder for DummyOption {
            type Layout = DummyOptionsImpl;

            fn serialized_len(&self) -> usize {
                self.data.len()
            }

            fn option_kind(&self) -> u8 {
                self.kind
            }

            fn serialize_into(&self, data: &mut [u8]) {
                assert_eq!(data.len(), OptionBuilder::serialized_len(self));
                data.copy_from_slice(&self.data);
            }
        }

        impl AlignedOptionBuilder for DummyOption {
            // For our `DummyOption`, we simply regard (length, kind) as their
            // alignment requirement.
            fn alignment_requirement(&self) -> (usize, usize) {
                (self.data.len(), self.kind as usize)
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

        #[derive(Debug, Eq, PartialEq)]
        enum AlwaysErrorErr {
            Sequence,
            Option,
        }

        impl From<Never> for AlwaysErrorErr {
            fn from(err: Never) -> AlwaysErrorErr {
                match err {}
            }
        }

        impl OptionParseError for AlwaysErrorErr {
            const SEQUENCE_FORMAT_ERROR: AlwaysErrorErr = AlwaysErrorErr::Sequence;
        }

        #[derive(Debug)]
        struct AlwaysErrOptionsImpl;

        impl OptionLayout for AlwaysErrOptionsImpl {
            type KindLenField = u8;
        }

        impl OptionParseLayout for AlwaysErrOptionsImpl {
            type Error = AlwaysErrorErr;
            const END_OF_OPTIONS: Option<u8> = Some(0);
            const NOP: Option<u8> = Some(1);
        }

        impl<'a> OptionsImpl<'a> for AlwaysErrOptionsImpl {
            type Option = ();

            fn parse(_kind: u8, _data: &'a [u8]) -> Result<Option<()>, AlwaysErrorErr> {
                Err(AlwaysErrorErr::Option)
            }
        }

        #[derive(Debug)]
        struct DummyNdpOptionsImpl;

        #[derive(Debug)]
        struct NdpOption {
            kind: u8,
            data: Vec<u8>,
        }

        impl OptionLayout for NdpOption {
            type KindLenField = u8;

            const LENGTH_ENCODING: LengthEncoding =
                LengthEncoding::TypeLengthValue { option_len_multiplier: nonzero!(8usize) };
        }

        impl OptionLayout for DummyNdpOptionsImpl {
            type KindLenField = u8;

            const LENGTH_ENCODING: LengthEncoding =
                LengthEncoding::TypeLengthValue { option_len_multiplier: nonzero!(8usize) };
        }

        impl OptionParseLayout for DummyNdpOptionsImpl {
            type Error = OptionParseErr;

            const END_OF_OPTIONS: Option<u8> = None;

            const NOP: Option<u8> = None;
        }

        impl<'a> OptionsImpl<'a> for DummyNdpOptionsImpl {
            type Option = NdpOption;

            fn parse(kind: u8, data: &'a [u8]) -> Result<Option<Self::Option>, OptionParseErr> {
                let mut v = Vec::with_capacity(data.len());
                v.extend_from_slice(data);
                Ok(Some(NdpOption { kind, data: v }))
            }
        }

        impl OptionBuilder for NdpOption {
            type Layout = DummyNdpOptionsImpl;

            fn serialized_len(&self) -> usize {
                self.data.len()
            }

            fn option_kind(&self) -> u8 {
                self.kind
            }

            fn serialize_into(&self, data: &mut [u8]) {
                assert_eq!(data.len(), OptionBuilder::serialized_len(self));
                data.copy_from_slice(&self.data)
            }
        }

        #[derive(Debug)]
        struct DummyMultiByteKindOptionsImpl;

        #[derive(Debug)]
        struct MultiByteOption {
            kind: U16,
            data: Vec<u8>,
        }

        impl OptionLayout for MultiByteOption {
            type KindLenField = U16;
        }

        impl OptionLayout for DummyMultiByteKindOptionsImpl {
            type KindLenField = U16;
        }

        impl OptionParseLayout for DummyMultiByteKindOptionsImpl {
            type Error = OptionParseErr;

            const END_OF_OPTIONS: Option<U16> = None;

            const NOP: Option<U16> = None;
        }

        impl<'a> OptionsImpl<'a> for DummyMultiByteKindOptionsImpl {
            type Option = MultiByteOption;

            fn parse(kind: U16, data: &'a [u8]) -> Result<Option<Self::Option>, OptionParseErr> {
                let mut v = Vec::with_capacity(data.len());
                v.extend_from_slice(data);
                Ok(Some(MultiByteOption { kind, data: v }))
            }
        }

        impl OptionBuilder for MultiByteOption {
            type Layout = DummyMultiByteKindOptionsImpl;

            fn serialized_len(&self) -> usize {
                self.data.len()
            }

            fn option_kind(&self) -> U16 {
                self.kind
            }

            fn serialize_into(&self, data: &mut [u8]) {
                data.copy_from_slice(&self.data)
            }
        }

        #[test]
        fn test_length_encoding() {
            const TLV_1: LengthEncoding =
                LengthEncoding::TypeLengthValue { option_len_multiplier: nonzero!(1usize) };
            const TLV_2: LengthEncoding =
                LengthEncoding::TypeLengthValue { option_len_multiplier: nonzero!(2usize) };

            // Test LengthEncoding::record_length

            // For `ValueOnly`, `record_length` should always add 2 or 4 for the kind
            // and length bytes, but never add padding.
            assert_eq!(LengthEncoding::ValueOnly.record_length::<u8>(0), Some(2));
            assert_eq!(LengthEncoding::ValueOnly.record_length::<u8>(1), Some(3));
            assert_eq!(LengthEncoding::ValueOnly.record_length::<u8>(2), Some(4));
            assert_eq!(LengthEncoding::ValueOnly.record_length::<u8>(3), Some(5));

            assert_eq!(LengthEncoding::ValueOnly.record_length::<U16>(0), Some(4));
            assert_eq!(LengthEncoding::ValueOnly.record_length::<U16>(1), Some(5));
            assert_eq!(LengthEncoding::ValueOnly.record_length::<U16>(2), Some(6));
            assert_eq!(LengthEncoding::ValueOnly.record_length::<U16>(3), Some(7));

            // For `TypeLengthValue` with `option_len_multiplier = 1`,
            // `record_length` should always add 2 or 4 for the kind and length
            // bytes, but never add padding.
            assert_eq!(TLV_1.record_length::<u8>(0), Some(2));
            assert_eq!(TLV_1.record_length::<u8>(1), Some(3));
            assert_eq!(TLV_1.record_length::<u8>(2), Some(4));
            assert_eq!(TLV_1.record_length::<u8>(3), Some(5));

            assert_eq!(TLV_1.record_length::<U16>(0), Some(4));
            assert_eq!(TLV_1.record_length::<U16>(1), Some(5));
            assert_eq!(TLV_1.record_length::<U16>(2), Some(6));
            assert_eq!(TLV_1.record_length::<U16>(3), Some(7));

            // For `TypeLengthValue` with `option_len_multiplier = 2`,
            // `record_length` should always add 2 or 4 for the kind and length
            // bytes, and add padding if necessary to reach a multiple of 2.
            assert_eq!(TLV_2.record_length::<u8>(0), Some(2)); // (0 + 2)
            assert_eq!(TLV_2.record_length::<u8>(1), Some(4)); // (1 + 2 + 1)
            assert_eq!(TLV_2.record_length::<u8>(2), Some(4)); // (2 + 2)
            assert_eq!(TLV_2.record_length::<u8>(3), Some(6)); // (3 + 2 + 1)

            assert_eq!(TLV_2.record_length::<U16>(0), Some(4)); // (0 + 4)
            assert_eq!(TLV_2.record_length::<U16>(1), Some(6)); // (1 + 4 + 1)
            assert_eq!(TLV_2.record_length::<U16>(2), Some(6)); // (2 + 4)
            assert_eq!(TLV_2.record_length::<U16>(3), Some(8)); // (3 + 4 + 1)

            // Test LengthEncoding::encode_length

            fn encode_length<K: KindLenField>(
                length_encoding: LengthEncoding,
                option_body_len: usize,
            ) -> Option<usize> {
                length_encoding.encode_length::<K>(option_body_len).map(Into::into)
            }

            // For `ValueOnly`, `encode_length` should always return the
            // argument unmodified.
            assert_eq!(encode_length::<u8>(LengthEncoding::ValueOnly, 0), Some(0));
            assert_eq!(encode_length::<u8>(LengthEncoding::ValueOnly, 1), Some(1));
            assert_eq!(encode_length::<u8>(LengthEncoding::ValueOnly, 2), Some(2));
            assert_eq!(encode_length::<u8>(LengthEncoding::ValueOnly, 3), Some(3));

            assert_eq!(encode_length::<U16>(LengthEncoding::ValueOnly, 0), Some(0));
            assert_eq!(encode_length::<U16>(LengthEncoding::ValueOnly, 1), Some(1));
            assert_eq!(encode_length::<U16>(LengthEncoding::ValueOnly, 2), Some(2));
            assert_eq!(encode_length::<U16>(LengthEncoding::ValueOnly, 3), Some(3));

            // For `TypeLengthValue` with `option_len_multiplier = 1`,
            // `encode_length` should always add 2 or 4 for the kind and length
            // bytes.
            assert_eq!(encode_length::<u8>(TLV_1, 0), Some(2));
            assert_eq!(encode_length::<u8>(TLV_1, 1), Some(3));
            assert_eq!(encode_length::<u8>(TLV_1, 2), Some(4));
            assert_eq!(encode_length::<u8>(TLV_1, 3), Some(5));

            assert_eq!(encode_length::<U16>(TLV_1, 0), Some(4));
            assert_eq!(encode_length::<U16>(TLV_1, 1), Some(5));
            assert_eq!(encode_length::<U16>(TLV_1, 2), Some(6));
            assert_eq!(encode_length::<U16>(TLV_1, 3), Some(7));

            // For `TypeLengthValue` with `option_len_multiplier = 2`,
            // `encode_length` should always add 2 or 4 for the kind and length
            // bytes, add padding if necessary to reach a multiple of 2, and
            // then divide by 2.
            assert_eq!(encode_length::<u8>(TLV_2, 0), Some(1)); // (0 + 2)     / 2
            assert_eq!(encode_length::<u8>(TLV_2, 1), Some(2)); // (1 + 2 + 1) / 2
            assert_eq!(encode_length::<u8>(TLV_2, 2), Some(2)); // (2 + 2)     / 2
            assert_eq!(encode_length::<u8>(TLV_2, 3), Some(3)); // (3 + 2 + 1) / 2

            assert_eq!(encode_length::<U16>(TLV_2, 0), Some(2)); // (0 + 4)     / 2
            assert_eq!(encode_length::<U16>(TLV_2, 1), Some(3)); // (1 + 4 + 1) / 2
            assert_eq!(encode_length::<U16>(TLV_2, 2), Some(3)); // (2 + 4)     / 2
            assert_eq!(encode_length::<U16>(TLV_2, 3), Some(4)); // (3 + 4 + 1) / 2

            // Test LengthEncoding::decode_length

            fn decode_length<K: KindLenField>(
                length_encoding: LengthEncoding,
                length_field: usize,
            ) -> Option<usize> {
                length_encoding.decode_length::<K>(length_field.try_into().unwrap())
            }

            // For `ValueOnly`, `decode_length` should always return the
            // argument unmodified.
            assert_eq!(decode_length::<u8>(LengthEncoding::ValueOnly, 0), Some(0));
            assert_eq!(decode_length::<u8>(LengthEncoding::ValueOnly, 1), Some(1));
            assert_eq!(decode_length::<u8>(LengthEncoding::ValueOnly, 2), Some(2));
            assert_eq!(decode_length::<u8>(LengthEncoding::ValueOnly, 3), Some(3));

            assert_eq!(decode_length::<U16>(LengthEncoding::ValueOnly, 0), Some(0));
            assert_eq!(decode_length::<U16>(LengthEncoding::ValueOnly, 1), Some(1));
            assert_eq!(decode_length::<U16>(LengthEncoding::ValueOnly, 2), Some(2));
            assert_eq!(decode_length::<U16>(LengthEncoding::ValueOnly, 3), Some(3));

            // For `TypeLengthValue` with `option_len_multiplier = 1`,
            // `decode_length` should always subtract 2 or 4 for the kind and
            // length bytes.
            assert_eq!(decode_length::<u8>(TLV_1, 0), None);
            assert_eq!(decode_length::<u8>(TLV_1, 1), None);
            assert_eq!(decode_length::<u8>(TLV_1, 2), Some(0));
            assert_eq!(decode_length::<u8>(TLV_1, 3), Some(1));

            assert_eq!(decode_length::<U16>(TLV_1, 0), None);
            assert_eq!(decode_length::<U16>(TLV_1, 1), None);
            assert_eq!(decode_length::<U16>(TLV_1, 2), None);
            assert_eq!(decode_length::<U16>(TLV_1, 3), None);
            assert_eq!(decode_length::<U16>(TLV_1, 4), Some(0));
            assert_eq!(decode_length::<U16>(TLV_1, 5), Some(1));

            // For `TypeLengthValue` with `option_len_multiplier = 2`,
            // `decode_length` should always multiply by 2 or 4 and then
            // subtract 2 for the kind and length bytes.
            assert_eq!(decode_length::<u8>(TLV_2, 0), None);
            assert_eq!(decode_length::<u8>(TLV_2, 1), Some(0));
            assert_eq!(decode_length::<u8>(TLV_2, 2), Some(2));
            assert_eq!(decode_length::<u8>(TLV_2, 3), Some(4));

            assert_eq!(decode_length::<U16>(TLV_2, 0), None);
            assert_eq!(decode_length::<U16>(TLV_2, 1), None);
            assert_eq!(decode_length::<U16>(TLV_2, 2), Some(0));
            assert_eq!(decode_length::<U16>(TLV_2, 3), Some(2));

            // Test end-to-end by creating options implementation with different
            // length encodings.

            /// Declare a new options impl type with a custom `LENGTH_ENCODING`.
            macro_rules! declare_options_impl {
                ($opt:ident, $impl:ident, $encoding:expr) => {
                    #[derive(Debug)]
                    enum $impl {}

                    #[derive(Debug, PartialEq)]
                    struct $opt {
                        kind: u8,
                        data: Vec<u8>,
                    }

                    impl<'a> From<&'a (u8, Vec<u8>)> for $opt {
                        fn from((kind, data): &'a (u8, Vec<u8>)) -> $opt {
                            $opt { kind: *kind, data: data.clone() }
                        }
                    }

                    impl OptionLayout for $opt {
                        const LENGTH_ENCODING: LengthEncoding = $encoding;
                        type KindLenField = u8;
                    }

                    impl OptionLayout for $impl {
                        const LENGTH_ENCODING: LengthEncoding = $encoding;
                        type KindLenField = u8;
                    }

                    impl OptionParseLayout for $impl {
                        type Error = OptionParseErr;
                        const END_OF_OPTIONS: Option<u8> = Some(0);
                        const NOP: Option<u8> = Some(1);
                    }

                    impl<'a> OptionsImpl<'a> for $impl {
                        type Option = $opt;

                        fn parse(
                            kind: u8,
                            data: &'a [u8],
                        ) -> Result<Option<Self::Option>, OptionParseErr> {
                            let mut v = Vec::new();
                            v.extend_from_slice(data);
                            Ok(Some($opt { kind, data: v }))
                        }
                    }

                    impl OptionBuilder for $opt {
                        type Layout = $impl;

                        fn serialized_len(&self) -> usize {
                            self.data.len()
                        }

                        fn option_kind(&self) -> u8 {
                            self.kind
                        }

                        fn serialize_into(&self, data: &mut [u8]) {
                            assert_eq!(data.len(), OptionBuilder::serialized_len(self));
                            data.copy_from_slice(&self.data);
                        }
                    }
                };
            }

            declare_options_impl!(
                DummyImplValueOnly,
                DummyImplValueOnlyImpl,
                LengthEncoding::ValueOnly
            );
            declare_options_impl!(DummyImplTlv1, DummyImplTlv1Impl, TLV_1);
            declare_options_impl!(DummyImplTlv2, DummyImplTlv2Impl, TLV_2);

            /// Tests that a given option is parsed from different byte
            /// sequences for different options layouts.
            ///
            /// Since some options cannot be parsed from any byte sequence using
            /// the `DummyImplTlv2` layout (namely, those whose lengths are not
            /// a multiple of 2), `tlv_2` may be `None`.
            fn test_parse(
                (expect_kind, expect_data): (u8, Vec<u8>),
                value_only: &[u8],
                tlv_1: &[u8],
                tlv_2: Option<&[u8]>,
            ) {
                let options = Options::<_, DummyImplValueOnlyImpl>::parse(value_only)
                    .unwrap()
                    .iter()
                    .collect::<Vec<_>>();
                let data = expect_data.clone();
                assert_eq!(options, [DummyImplValueOnly { kind: expect_kind, data }]);

                let options = Options::<_, DummyImplTlv1Impl>::parse(tlv_1)
                    .unwrap()
                    .iter()
                    .collect::<Vec<_>>();
                let data = expect_data.clone();
                assert_eq!(options, [DummyImplTlv1 { kind: expect_kind, data }]);

                if let Some(tlv_2) = tlv_2 {
                    let options = Options::<_, DummyImplTlv2Impl>::parse(tlv_2)
                        .unwrap()
                        .iter()
                        .collect::<Vec<_>>();
                    assert_eq!(options, [DummyImplTlv2 { kind: expect_kind, data: expect_data }]);
                }
            }

            // 0-byte body
            test_parse((0xFF, vec![]), &[0xFF, 0], &[0xFF, 2], Some(&[0xFF, 1]));
            // 1-byte body
            test_parse((0xFF, vec![0]), &[0xFF, 1, 0], &[0xFF, 3, 0], None);
            // 2-byte body
            test_parse(
                (0xFF, vec![0, 1]),
                &[0xFF, 2, 0, 1],
                &[0xFF, 4, 0, 1],
                Some(&[0xFF, 2, 0, 1]),
            );
            // 3-byte body
            test_parse((0xFF, vec![0, 1, 2]), &[0xFF, 3, 0, 1, 2], &[0xFF, 5, 0, 1, 2], None);
            // 4-byte body
            test_parse(
                (0xFF, vec![0, 1, 2, 3]),
                &[0xFF, 4, 0, 1, 2, 3],
                &[0xFF, 6, 0, 1, 2, 3],
                Some(&[0xFF, 3, 0, 1, 2, 3]),
            );

            /// Tests that an option can be serialized and then parsed in each
            /// option layout.
            ///
            /// In some cases (when the body length is not a multiple of 2), the
            /// `DummyImplTlv2` layout will parse a different option than was
            /// originally serialized. In this case, `expect_tlv_2` can be used
            /// to provide a different value to expect as the result of parsing.
            fn test_serialize_parse(opt: (u8, Vec<u8>), expect_tlv_2: Option<(u8, Vec<u8>)>) {
                let opts = [opt.clone()];

                fn test_serialize_parse_inner<
                    O: OptionBuilder + Debug + PartialEq + for<'a> From<&'a (u8, Vec<u8>)>,
                    I: for<'a> OptionsImpl<'a, Error = OptionParseErr, Option = O> + std::fmt::Debug,
                >(
                    opts: &[(u8, Vec<u8>)],
                    expect: &[(u8, Vec<u8>)],
                ) {
                    let opts = opts.iter().map(Into::into).collect::<Vec<_>>();
                    let expect = expect.iter().map(Into::into).collect::<Vec<_>>();

                    let ser = OptionSequenceBuilder::<O, _>::new(opts.iter());
                    let serialized =
                        ser.into_serializer().serialize_vec_outer().unwrap().as_ref().to_vec();
                    let options = Options::<_, I>::parse(serialized.as_slice())
                        .unwrap()
                        .iter()
                        .collect::<Vec<_>>();
                    assert_eq!(options, expect);
                }

                test_serialize_parse_inner::<DummyImplValueOnly, DummyImplValueOnlyImpl>(
                    &opts, &opts,
                );
                test_serialize_parse_inner::<DummyImplTlv1, DummyImplTlv1Impl>(&opts, &opts);
                let expect = if let Some(expect) = expect_tlv_2 { expect } else { opt };
                test_serialize_parse_inner::<DummyImplTlv2, DummyImplTlv2Impl>(&opts, &[expect]);
            }

            // 0-byte body
            test_serialize_parse((0xFF, vec![]), None);
            // 1-byte body
            test_serialize_parse((0xFF, vec![0]), Some((0xFF, vec![0, 0])));
            // 2-byte body
            test_serialize_parse((0xFF, vec![0, 1]), None);
            // 3-byte body
            test_serialize_parse((0xFF, vec![0, 1, 2]), Some((0xFF, vec![0, 1, 2, 0])));
            // 4-byte body
            test_serialize_parse((0xFF, vec![0, 1, 2, 3]), None);
        }

        #[test]
        fn test_empty_options() {
            // all END_OF_OPTIONS
            let bytes = [0; 64];
            let options = Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap();
            assert_eq!(options.iter().count(), 0);

            // all NOP
            let bytes = [1; 64];
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
                bytes.push(1);
                for j in (2..i).rev() {
                    bytes.push(j);
                }
                // from the user's perspective, these NOPs should be transparent
                bytes.push(1);
            }

            let options = Options::<_, DummyOptionsImpl>::parse(bytes.as_slice()).unwrap();
            for (idx, DummyOption { kind, data }) in options.iter().enumerate() {
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
            //
            // `bytes` is a sequence of NOPs.
            let bytes = [1; 64];
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
            for (idx, NdpOption { kind, data }) in options.iter().enumerate() {
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
                OptionParseErr
            );

            // the length byte is 0 (similar check to above, but worth
            // explicitly testing since this was a bug in the Linux kernel:
            // https://bugzilla.redhat.com/show_bug.cgi?id=1622404)
            let bytes = [2, 0];
            assert_eq!(
                Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr
            );

            // the length byte is too long
            let bytes = [2, 3];
            assert_eq!(
                Options::<_, DummyOptionsImpl>::parse(&bytes[..]).unwrap_err(),
                OptionParseErr
            );

            // the buffer is fine, but the implementation returns a parse error
            let bytes = [2, 2];
            assert_eq!(
                Options::<_, AlwaysErrOptionsImpl>::parse(&bytes[..]).unwrap_err(),
                AlwaysErrorErr::Option,
            );
        }

        #[test]
        fn test_missing_length_bytes() {
            // Construct a sequence with a valid record followed by an
            // incomplete one, where `kind` is specified but `len` is missing.
            // So we can assert that we'll fail cleanly in that case.
            //
            // Added as part of Change-Id
            // Ibd46ac7384c7c5e0d74cb344b48c88876c351b1a.
            //
            // Before the small refactor in the Change-Id above, there was a
            // check during parsing that guaranteed that the length of the
            // remaining buffer was >= 1, but it should've been a check for
            // >= 2, and the case below would have caused it to panic while
            // trying to access the length byte, which was a DoS vulnerability.
            assert_matches::assert_matches!(
                Options::<_, DummyOptionsImpl>::parse(&[0x03, 0x03, 0x01, 0x03][..]),
                Err(OptionParseErr)
            );
        }

        #[test]
        fn test_partial_kind_field() {
            // Construct a sequence with only one byte where a two-byte kind
            // field is expected.
            //
            // Added as part of Change-Id
            // I468121f5712b73c4e704460f580f166c876ee7d6.
            //
            // Before the small refactor in the Change-Id above, we treated any
            // failure to consume the kind field from the byte slice as
            // indicating that there were no bytes left, and we would stop
            // parsing successfully. This logic was correct when we only
            // supported 1-byte kind fields, but it became incorrect once we
            // introduced multi-byte kind fields.
            assert_matches::assert_matches!(
                Options::<_, DummyMultiByteKindOptionsImpl>::parse(&[0x00][..]),
                Err(OptionParseErr)
            );
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

            let collected = options.iter().collect::<Vec<_>>();
            // Pass `collected.iter()` instead of `options.iter()` since we need
            // an iterator over references, and `options.iter()` produces an
            // iterator over values.
            let ser = OptionSequenceBuilder::<DummyOption, _>::new(collected.iter());

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
            let collected = options.iter().collect::<Vec<_>>();
            // Pass `collected.iter()` instead of `options.iter()` since we need
            // an iterator over references, and `options.iter()` produces an
            // iterator over values.
            let ser = OptionSequenceBuilder::<NdpOption, _>::new(collected.iter());

            let serialized = ser.into_serializer().serialize_vec_outer().unwrap().as_ref().to_vec();

            assert_eq!(serialized, bytes);
        }

        #[test]
        fn test_parse_and_serialize_multi_byte_fields() {
            let mut bytes = Vec::new();
            for i in 4..16 {
                // Push kind U16<NetworkEndian>.
                bytes.push(0);
                bytes.push(i);
                // Push length U16<NetworkEndian>.
                bytes.push(0);
                bytes.push(i);
                // Write `i` - 4 bytes.
                for j in 4..i {
                    bytes.push(j);
                }
            }

            let options =
                Options::<_, DummyMultiByteKindOptionsImpl>::parse(bytes.as_slice()).unwrap();
            for (idx, MultiByteOption { kind, data }) in options.iter().enumerate() {
                assert_eq!(usize::from(kind), idx + 4);
                let idx: u8 = idx.try_into().unwrap();
                let bytes: Vec<_> = (4..(idx + 4)).collect();
                assert_eq!(data, bytes);
            }

            let collected = options.iter().collect::<Vec<_>>();
            // Pass `collected.iter()` instead of `options.iter()` since we need
            // an iterator over references, and `options.iter()` produces an
            // iterator over values.
            let ser = OptionSequenceBuilder::<MultiByteOption, _>::new(collected.iter());
            let mut output = vec![0u8; ser.serialized_len()];
            ser.serialize_into(output.as_mut_slice());
            assert_eq!(output, bytes);
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
                let x = rng.gen_range(1usize..256);
                let y = rng.gen_range(0..x);
                let pos = rng.gen_range(0usize..65536);
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
                DummyOption { kind: 1, data: vec![42, 42] },
                DummyOption { kind: 0, data: vec![42, 42] },
                DummyOption { kind: 1, data: vec![1, 2, 3] },
                DummyOption { kind: 2, data: vec![3, 2, 1] },
                DummyOption { kind: 0, data: vec![42] },
                DummyOption { kind: 2, data: vec![9, 9, 9, 9] },
            ];
            let ser = AlignedRecordSequenceBuilder::<DummyOption, _>::new(
                0,
                dummy_options.iter(),
            );
            assert_eq!(ser.serialized_len(), 32);
            let mut buf = [0u8; 32];
            ser.serialize_into(&mut buf[..]);
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
