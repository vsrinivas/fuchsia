// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A copy of `src/connectivity/network/netstack3/core/src/wire/util/records.rs`,
// until it can be pulled out.

#![allow(missing_docs)]

//! Utilities for parsing sequential records.
//!
//! This module provides utilities for parsing sequential records. IGMP message
//! parsing is built using these utilities, as is another set of general
//! utilities for IPv4, TCP, and NDP options parsing, provided in the
//! [`options`] submodule.
//!
//! [`options`]: crate::wire::util::records::options

use fuchsia_syslog::macros::*;
use packet::{BufferView, BufferViewMut, InnerPacketBuilder};
use std::marker::PhantomData;
use std::ops::Deref;
use zerocopy::ByteSlice;

/// A parsed set of arbitrary sequential records.
///
/// `Records` represents a pre-parsed set of records whose structure is enforced
/// by the impl in `O`.
#[derive(Debug)]
pub struct Records<B, R: RecordsImplLayout> {
    bytes: B,
    context: R::Context,
}

/// An iterator over the records contained inside a `Records` instance.
pub struct RecordsIter<'a, R: RecordsImpl<'a>> {
    bytes: &'a [u8],
    context: R::Context,
}

/// The context kept while performing records parsing.
///
/// Types which implement `RecordsContext` can be used as the long-lived
/// context which is kept during records parsing. This context allows
/// parsers to keep running computations over the span of multiple records.
pub trait RecordsContext: Sized + Clone {
    /// Clone a context for iterator purposes.
    ///
    /// `clone_for_iter` is useful for cloning a context to be
    /// used by `RecordsIter`. Since `Records::parse_with_context`
    /// will do a full pass over all the records to check for errors,
    /// a `RecordsIter` should never error. Thereforce, instead of doing
    /// checks when iterating (if a context was used for checks), a
    /// clone of a context can be made specifically for iterator purposes
    /// that does not do checks (which may be expensive).
    ///
    /// By default, just do a normal clone.
    fn clone_for_iter(&self) -> Self {
        self.clone()
    }
}

// Implement the `RecordsContext` trait for `usize` which will be used by
// record limiting contexts (see [`LimitedRecordsImpl`]) and for `()`
// which is to represent an empty/no context-type.
impl RecordsContext for usize {}
impl RecordsContext for () {}

/// Basic associated types used by a `RecordsImpl`.
///
/// This trait is kept separate from `RecordsImpl` to keep the lifetimes
/// separated.
pub trait RecordsImplLayout {
    /// The type of errors that may be returned by a `RecordsImpl::parse_with_context`.
    type Error;

    /// A context type that can be used to maintain state or do checks.
    type Context: RecordsContext;
}

/// An implementation of a records parser.
///
/// `RecordsImpl` provides functions to parse sequential records. It is required
///  in order to construct a `Records` or `RecordsIter`.
pub trait RecordsImpl<'a>: RecordsImplLayout {
    /// The type of a single record; the output from the `parse_with_context` function.
    ///
    /// For long or variable-length data, the user is advised to make `Record` a
    /// reference into the bytes passed to `parse_with_context`. This is achievable
    /// because of the lifetime parameter to this trait.
    type Record;

    /// Parse a record with some context.
    ///
    /// `parse_with_context` takes a variable-length `data` and a `context` to
    /// maintain state, and returns `Ok(Some(Some(o)))` if the the record is
    /// successfully parsed as `o`, `Ok(Some(None))` if data is not malformed
    /// but the implementer can't extract a concrete object (e.g. record is an
    /// unimplemented enumeration, but we can still safely "skip" it), Ok(None)
    /// if `parse_with_context` is unable to parse more records, and `Err(err)`
    /// if the `data` was malformed for the attempted record parsing.
    ///
    /// `data` MAY be empty. It is up to the implementer to handle an exhausted
    /// `data`.
    ///
    /// When returning `Ok(Some(None))` it's the implementer's responsibility to
    /// nonetheless skip the record (which may not be possible for some
    /// implementations, in which case it should return an `Err`).
    ///
    /// `parse_with_context` must be deterministic, or else
    /// `Records::parse_with_context` cannot guarantee that future iterations
    /// will not produce errors (and panic).
    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        data: &mut BV,
        context: &mut Self::Context,
    ) -> Result<Option<Option<Self::Record>>, Self::Error>;
}

/// A limited parsed set of records.
///
/// `LimitedRecords` represents a parsed set of records that can be limited to a
/// certain number of records. Unlike records with accepts a `RecordsImpl`,
/// `LimitedRecords` accepts a type that implements `LimitedRecordsImpl` for `O`.
pub type LimitedRecords<B, O> = Records<B, LimitedRecordsImplBridge<O>>;

/// Create a bridge to `RecordsImplLayout` and `RecordsImpl` from an `O` that
/// implements `LimitedRecordsImplLayout`. This is required so we can have a single
/// implementation of `parse_with_context` and definition of `Context` that
/// all implementers of `LimitedRecordsImpl` will get for free.
#[derive(Debug)]
pub struct LimitedRecordsImplBridge<O>(PhantomData<O>);

impl<O> RecordsImplLayout for LimitedRecordsImplBridge<O>
where
    O: LimitedRecordsImplLayout,
{
    type Error = O::Error;

    // All LimitedRecords get a context type of usize.
    type Context = usize;
}

impl<'a, O> RecordsImpl<'a> for LimitedRecordsImplBridge<O>
where
    O: LimitedRecordsImpl<'a>,
{
    type Record = O::Record;

    /// Parse some bytes with a given `context` as a limit.
    ///
    /// `parse_with_context` accepts a `bytes` buffer and limit `context`
    /// and verifies that the limit has not been reached and that bytes is not empty.
    /// See [`EXACT_LIMIT_ERROR`] for information about exact limiting. If the limit
    /// has not been reached and `bytes` has not been exhausted, `LimitedRecordsImpl::parse`
    /// will be called to do the actual parsing of a record.
    ///
    /// [`EXACT_LIMIT_ERROR`]: LimitedRecordsImplLayout::EXACT_LIMIT_ERROR
    fn parse_with_context<BV: BufferView<&'a [u8]>>(
        bytes: &mut BV,
        context: &mut Self::Context,
    ) -> Result<Option<Option<Self::Record>>, Self::Error> {
        let limit_hit = *context <= 0;

        if bytes.is_empty() || limit_hit {
            return match O::EXACT_LIMIT_ERROR {
                Some(_) if bytes.is_empty() ^ limit_hit => Err(O::EXACT_LIMIT_ERROR.unwrap()),
                _ => Ok(None),
            };
        }

        *context = context.checked_sub(1).expect("Can't decrement counter below 0");

        O::parse(bytes)
    }
}

/// Trait that provides implementations to limit the amount of records read from
/// a buffer. Some protocols will have some sort of header preceding the records
/// that will indicate the number of records to follow (e.g. IGMP), while others
/// will have that information inline (e.g. IPv4 options).
///
/// If the implementer of this trait wishes to impose an Exact Limit constraint,
/// they should supply a value for `EXACT_LIMIT_ERROR`.
///
/// Note that implementations of `LimitedRecordsImpl` cannot be used in place of
/// implementations of `RecordsImpl` directly as this does not implement
/// `RecordsImpl`. Implementers will need to use the `LimitedRecordsImplBridge`
/// to create bindings to a `RecordsImpl`. Alternatively, instead of using
/// `Records<_, O>` where `O` is a type that implements `RecordsImpl`, implementers
/// can use `LimitedRecords<_, P>` where `P` is a type that implements
/// `LimitedRecordsImpl`. See [`LimitedRecords`].
pub trait LimitedRecordsImplLayout {
    /// See `RecordsImplLayout::Error` as this will be bound to a `RecordsImplLayout::Error`
    /// associated type directly.
    type Error;

    /// If `Some(E)`, `parse_with_context` of `LimitedRecordsImplBridge` will emit the
    /// provided constant as an error if the provided buffer is exhausted while `context`
    /// is not 0, or if the `context` reaches 0 but the provided buffer is not empty.
    const EXACT_LIMIT_ERROR: Option<Self::Error> = None;
}

pub trait LimitedRecordsImpl<'a>: LimitedRecordsImplLayout {
    /// See [`RecordsImpl::Record`] as this will be bound to a `RecordsImpl::Record`
    /// associated type directly.
    type Record;

    /// Parse a record after limit check has completed.
    ///
    /// `parse` will be called by a `LimitedRecordsImpl::parse_with_context` after
    /// doing limit checks. When this method is called, it is guaranteed by
    /// `LimitedRecordsImpl::parse_with_context` that the limit has not been reached,
    /// so `parse` should parse exactly one record (if possible).
    ///
    /// For information about return values, see [`RecordsImpl::parse_with_context`].
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
    /// This is the analogous serializing version of `Record` in
    /// [`RecordsImpl`]. Records serialization expects an `Iterator` of objects
    /// of type `Record`.
    type Record;
    /// Provides the serialized length of a record.
    ///
    /// Returns the total length, in bytes, of `record`.
    fn record_length(record: &Self::Record) -> usize;
    /// Serializes `record`. into buffer `data`.
    ///
    /// The provided `data` buffer will **always** be sized to the value
    /// returned by `record_length`.
    fn serialize(data: &mut [u8], record: &Self::Record);
}

/// An instance of records serialization.
///
/// `RecordsSerializer` is instantiated with an `Iterator` that provides
/// items to be serialized by a `RecordsSerializerImpl`.
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
    /// even if cloned. Serialization typically performed with two passes on
    /// `records`: one to calculate the total length in bytes
    /// (`records_bytes_len`) and another one to serialize to a buffer
    /// (`serialize_records`). Violating this rule may cause panics or malformed
    /// packets.
    pub fn new(records: I) -> Self {
        Self { records, _marker: PhantomData }
    }

    /// Returns the total length, in bytes, of the serialized records contained
    /// within the `RecordsSerializer`.
    fn records_bytes_len(&self) -> usize {
        self.records.clone().map(|r| S::record_length(r)).sum()
    }

    /// `serialize_records` serializes all the records contained within the
    /// `RecordsSerializer`.
    ///
    /// # Panics
    ///
    /// `serialize_records` expects that `buffer` has enough bytes to serialize
    /// the contained records (as obtained from `records_bytes_len`, otherwise
    /// it's considered a violation of the API contract and the call will panic.
    fn serialize_records(&self, buffer: &mut [u8]) {
        let mut b = &mut &mut buffer[..];
        for r in self.records.clone() {
            // SECURITY: Take a zeroed buffer from b to prevent leaking
            // information from packets previously stored in this buffer.
            S::serialize(b.take_front_zero(S::record_length(r)).unwrap(), r);
        }
    }
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
    /// Parse a set of records with a context.
    ///
    /// See `parse_with_mut_context` for details on `bytes`, `context`, and
    /// return value. `parse_with_context` just calls `parse_with_mut_context`
    /// with a mutable reference to the `context` (which is owned).
    pub fn parse_with_context(
        bytes: B,
        mut context: R::Context,
    ) -> Result<Records<B, R>, R::Error> {
        Self::parse_with_mut_context(bytes, &mut context)
    }

    /// Parse a set of records with a mutable context.
    ///
    /// `parse_with_mut_context` parses `bytes` as a sequence of records. `context`
    /// may be used by implementers to maintain state and do checks.
    ///
    /// `parse_with_mut_context` performs a single pass over all of the records to
    /// verify that they are well-formed. Once `parse_with_context` returns
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
        // would just result in a runtime panic.
        // - B could return different bytes each time
        // - R::parse could be non-deterministic
        let c = context.clone();
        let mut b = LongLivedBuff::new(bytes.deref());
        while next::<_, R>(&mut b, context)?.is_some() {}
        Ok(Records { bytes, context: c })
    }

    /// Parse a set of records with a context, using a `BufferView`.
    ///
    /// See `parse_bv_with_mut_context` for details on `bytes`, `context`, and
    /// return value. `parse_bv_with_context` just calls `parse_bv_with_mut_context`
    /// with a mutable reference to the `context` (which is owned).
    pub fn parse_bv_with_context<BV: BufferView<B>>(
        bytes: &mut BV,
        mut context: R::Context,
    ) -> Result<Records<B, R>, R::Error> {
        Self::parse_bv_with_mut_context(bytes, &mut context)
    }

    /// Parse a set of records with a mutable context, using a `BufferView`.
    ///
    /// This function is exactly the same as `parse_with_mut_context` except instead
    /// of operating on a `ByteSlice`, we operate on a `BufferView<B>` where `B`
    /// is a `ByteSlice`. `parse_bv_with_mut_context` enables parsing records without
    /// knowing the size of all records beforehand (unlike `parse_with_mut_context`
    /// where callers need to pass in a `ByteSlice` of some predetermined sized).
    /// Since callers will provide a mutable reference to a `BufferView`,
    /// `parse_bv_with_mut_context` will take only the amount of bytes it needs to
    /// parse records, leaving the rest in the `BufferView` object. That is, when
    /// `parse_bv_with_mut_context` returns, the `BufferView` object provided will be
    /// x bytes smaller, where x is the number of bytes required to parse the records.
    pub fn parse_bv_with_mut_context<BV: BufferView<B>>(
        bytes: &mut BV,
        context: &mut R::Context,
    ) -> Result<Records<B, R>, R::Error> {
        let c = context.clone();
        let mut b = LongLivedBuff::new(bytes.as_ref());
        while next::<_, R>(&mut b, context)?.is_some() {}

        // When we get here, we know that whatever is left in `b` is not needed
        // so we only take the amount of bytes we actually need from `bytes`,
        // leaving the rest alone for the caller to continue parsing with.
        let bytes_len = bytes.len();
        let b_len = b.len();
        Ok(Records { bytes: bytes.take_front(bytes_len - b_len).unwrap(), context: c })
    }
}

impl<B, R> Records<B, R>
where
    B: ByteSlice,
    R: for<'a> RecordsImpl<'a, Context = ()>,
{
    /// Parses a set of records.
    ///
    /// Equivalent to calling `parse_with_context` with `context = ()`.
    pub fn parse(bytes: B) -> Result<Records<B, R>, R::Error> {
        Self::parse_with_context(bytes, ())
    }
}

impl<B: Deref<Target = [u8]>, R> Records<B, R>
where
    R: for<'a> RecordsImpl<'a>,
{
    /// Get the underlying bytes.
    ///
    /// `bytes` returns a reference to the byte slice backing this `Options`.
    pub fn bytes(&self) -> &[u8] {
        &self.bytes
    }
}

impl<'a, B, R> Records<B, R>
where
    B: 'a + ByteSlice,
    R: RecordsImpl<'a>,
{
    /// Create an iterator over options.
    ///
    /// `iter` constructs an iterator over the records. Since the records were
    /// validated in `parse`, then so long as `from_kind` and `from_data` are
    /// deterministic, the iterator is infallible.
    pub fn iter(&'a self) -> RecordsIter<'a, R> {
        RecordsIter { bytes: &self.bytes, context: self.context.clone_for_iter() }
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
            // `parse_with_context` cannot parse any more, return
            // Ok(None) to let the caller know that we have parsed
            // all possible records for a given `bytes`.
            Ok(None) => return Ok(None),

            // `parse_with_context` was unable to parse a record, not
            // because `bytes` was malformed but for other non fatal
            // reasons, so we can skip.
            Ok(Some(None)) => {}

            // `parse_with_context` was able to parse a record, so
            // return it.
            Ok(Some(Some(o))) => return Ok(Some(o)),

            // `parse_with_context` had an error so pass that error
            // to the caller.
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
    pub fn new(data: &'a [u8]) -> LongLivedBuff<'a> {
        LongLivedBuff::<'a>(data)
    }
}

impl<'a> AsRef<[u8]> for LongLivedBuff<'a> {
    fn as_ref(&self) -> &[u8] {
        self.0
    }
}

impl<'a> packet::BufferView<&'a [u8]> for LongLivedBuff<'a> {
    fn take_front(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.0.len() >= n {
            let (prefix, rest) = std::mem::replace(&mut self.0, &[]).split_at(n);
            std::mem::replace(&mut self.0, rest);
            Some(prefix)
        } else {
            None
        }
    }

    fn take_back(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.0.len() >= n {
            let (rest, suffix) = std::mem::replace(&mut self.0, &[]).split_at(n);
            std::mem::replace(&mut self.0, rest);
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
    use super::*;
    use packet::BufferView;
    use zerocopy::{AsBytes, FromBytes, LayoutVerified, Unaligned};

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

    impl std::fmt::Debug for FilterContext {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "FilterContext{{disallowed:{:?}}}", &self.disallowed[..])
        }
    }

    impl<'a> RecordsImpl<'a> for FilterContextRecordImpl {
        type Record = LayoutVerified<&'a [u8], DummyRecord>;

        fn parse_with_context<BV: BufferView<&'a [u8]>>(
            bytes: &mut BV,
            context: &mut Self::Context,
        ) -> Result<Option<Option<Self::Record>>, Self::Error> {
            if bytes.len() < std::mem::size_of::<DummyRecord>() {
                Ok(None)
            } else if bytes.as_ref()[0..std::mem::size_of::<DummyRecord>()]
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
        // Should be 5 because on the last iteration, we should realize
        // that we have no more bytes left and end before parsing (also
        // explaining why `parse_counter` should only be 4.
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
        let _test = Records::<_, ContextlessRecordImpl>::parse(&DUMMY_BYTES[..]);
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
}

/// Header options for IPv4 and TCP, and NDP.
///
/// This module provides a parser for the options formats used by IPv4, TCP, and
/// NDP. These formats are not identical, but share enough in common that they
/// can be implemented using the same utility with a bit of customization.
pub mod options {
    use super::*;
    use packet::BufferView;

    /// A parsed set of header options.
    ///
    /// `Options` represents a parsed set of options from an IPv4 or TCP header
    /// or an NDP packet. `Options` uses [`Records`] below the surface.
    ///
    /// [`Records`]: crate::wire::util::records::Records
    pub type Options<B, O> = Records<B, OptionsImplBridge<O>>;

    /// An instance of options serialization.
    ///
    /// `OptionsSerializer` is instantiated with an `Iterator` that provides
    /// items to be serialized by an [`OptionsSerializerImpl`].
    pub type OptionsSerializer<'a, S, O, I> = RecordsSerializer<'a, S, O, I>;

    /// Create a bridge to `RecordsImplLayout` and `RecordsImpl` traits from an `O`
    /// that implements `OptionsImpl`. This is required so we can have a single
    /// implementation of `parse_with_context` and definition of `Context` that
    /// all implementers of `OptionsImpl` will get for free.
    #[derive(Debug)]
    pub struct OptionsImplBridge<O>(PhantomData<O>);

    impl<O> RecordsImplLayout for OptionsImplBridge<O>
    where
        O: OptionsImplLayout,
    {
        type Error = OptionParseErr<O::Error>;
        type Context = ();
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
            let base = 2 + O::get_option_length(record);

            // Pad up to option_len_multiplier:
            (base + O::OPTION_LEN_MULTIPLIER - 1) / O::OPTION_LEN_MULTIPLIER
                * O::OPTION_LEN_MULTIPLIER
        }

        fn serialize(data: &mut [u8], record: &Self::Record) {
            // NOTE(brunodalbo) we don't currently support serializing the two
            //  single-byte options used in tcp and ip: NOP and END_OF_OPTIONS.
            //  If it is necessary to support those as part of TLV options
            //  serialization, some changes will be required here.

            // data not having enough space is a contract violation, so we
            // panic in that case.
            data[0] = O::get_option_kind(record);
            let length = Self::record_length(record) / O::OPTION_LEN_MULTIPLIER;
            // option length not fitting in u8 is a contract violation. Without
            // debug assertions on, this will cause the packet to be malformed.
            debug_assert!(length <= std::u8::MAX.into());
            data[1] = length as u8;
            // because padding may have occurred, we zero-fill data before
            // passing it along
            for b in data[2..].iter_mut() {
                *b = 0;
            }
            O::serialize(&mut data[2..], record)
        }
    }

    /// Errors returned from parsing options.
    ///
    /// `OptionParseErr` is either `Internal`, which indicates that this module
    /// encountered a malformed sequence of options (likely with a length field
    /// larger than the remaining bytes in the options buffer), or `External`,
    /// which indicates that the `OptionsImpl::parse` callback returned an error.
    #[derive(Debug, Eq, PartialEq)]
    pub enum OptionParseErr<E> {
        Internal,
        External(E),
    }

    // End of Options List in both IPv4 and TCP
    const END_OF_OPTIONS: u8 = 0;

    // NOP in both IPv4 and TCP
    const NOP: u8 = 1;

    /// Common traits of option parsing and serialization.
    ///
    /// This is split from `OptionsImpl` and `OptionsSerializerImpl` so that
    /// the associated types do not depend on the lifetime parameter to
    /// `OptionsImpl` and provide common behavior to parsers and serializers.
    pub trait OptionsImplLayout {
        /// The error type that can be returned in Options parsing.
        type Error;

        /// The value to multiply read lengths by.
        ///
        /// By default, this value is 1, but for some protocols (such as NDP)
        /// this may be different.
        const OPTION_LEN_MULTIPLIER: usize = 1;

        /// The End of options type (if one exists).
        const END_OF_OPTIONS: Option<u8> = Some(END_OF_OPTIONS);

        /// The No-op type (if one exists).
        const NOP: Option<u8> = Some(NOP);
    }

    /// An implementation of an options parser.
    ///
    /// `OptionsImpl` provides functions to parse fixed- and variable-length
    /// options. It is required in order to construct an `Options`.
    pub trait OptionsImpl<'a>: OptionsImplLayout {
        /// The type of an option; the output from the `parse` function.
        ///
        /// For long or variable-length data, the user is advised to make
        /// `Option` a reference into the bytes passed to `parse`. This is
        /// achievable because of the lifetime parameter to this trait.
        type Option;

        /// Parse an option.
        ///
        /// `parse` takes a kind byte and variable-length data associated and
        /// returns `Ok(Some(o))` if the option successfully parsed as `o`,
        /// `Ok(None)` if the kind byte was unrecognized, and `Err(err)` if the
        /// kind byte was recognized but `data` was malformed for that option
        /// kind. `parse` is allowed to not recognize certain option kinds, as
        /// the length field can still be used to safely skip over them.
        ///
        /// `parse` must be deterministic, or else `Options::parse` cannot
        /// guarantee that future iterations will not produce errors (and
        /// panic).
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
        /// This is the analogous serializing version of `Option` in
        /// [`OptionsImpl`]. Options serialization expects an `Iterator` of
        /// objects of type `Option`.
        type Option;

        /// Returns the serialized length, in bytes, of the given `option`.
        ///
        ///
        /// Implementers must return the length, in bytes, of the **data***
        /// portion of the option field (not counting the type and length
        /// bytes). The internal machinery of options serialization takes care
        /// of aligning options to their `OPTION_LEN_MULTIPLIER` boundaries,
        /// adding padding bytes if necessary.
        fn get_option_length(option: &Self::Option) -> usize;

        /// Returns the wire value for this option kind.
        fn get_option_kind(option: &Self::Option) -> u8;

        /// Serializes `option` into `data`.
        ///
        /// Implementers must write the **data** portion of `option` into
        /// `data` (not the type or length octets, those are extracted through
        /// calls to `get_option_kind` and `get_option_length`, respectively).
        /// `data` is guaranteed to be long enough to fit `option` based on the
        /// value returned by `get_option_length`.
        fn serialize(data: &mut [u8], option: &Self::Option);
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
            let kind = match bytes.take_front(1).map(|x| x[0]) {
                None => return Ok(None),
                Some(k) => {
                    // Can't do pattern matching with associated constants,
                    // so do it the good-ol' way:
                    if Some(k) == O::NOP {
                        continue;
                    } else if Some(k) == O::END_OF_OPTIONS {
                        return Ok(None);
                    }
                    k
                }
            };
            let len = match bytes.take_front(1).map(|x| x[0]) {
                None => return Err(OptionParseErr::Internal),
                Some(len) => (len as usize) * O::OPTION_LEN_MULTIPLIER,
            };

            if len < 2 || (len - 2) > bytes.len() {
                fx_log_err!("option length {} is either too short or longer than the total buffer length of {}", len, bytes.len());
                return Err(OptionParseErr::Internal);
            }

            // we can safely unwrap here since we verified the correct length above
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
        use packet::Serializer;

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

            fn get_option_length(option: &Self::Option) -> usize {
                option.1.len()
            }

            fn get_option_kind(option: &Self::Option) -> u8 {
                option.0
            }

            fn serialize(data: &mut [u8], option: &Self::Option) {
                data.copy_from_slice(&option.1);
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

            fn get_option_length(option: &Self::Option) -> usize {
                option.1.len()
            }

            fn get_option_kind(option: &Self::Option) -> u8 {
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
    }
}
