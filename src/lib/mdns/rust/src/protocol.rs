// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Protocol contains functions and traits for mDNS protocol parsing and packet
//! creation.

use std::collections::HashSet;
use std::convert::{From, TryFrom, TryInto};
use std::fmt::{Debug, Display, Formatter};
use std::mem;

use byteorder::NetworkEndian;
use packet::{BufferView, BufferViewMut, InnerPacketBuilder, ParsablePacket, ParseMetadata};
use zerocopy::{AsBytes, ByteSlice, ByteSliceMut, FromBytes, LayoutVerified, Unaligned};

type U16 = zerocopy::U16<NetworkEndian>;
type U32 = zerocopy::U32<NetworkEndian>;

const IPV4_SIZE: usize = 4;
const IPV6_SIZE: usize = 16;
const SRV_PAYLOAD_SIZE_OCTETS: u16 = 6;
const DOMAIN_COMPRESSION_MASK_U8: u8 = 0xc0;
const DOMAIN_COMPRESSION_MASK_U16: u16 = 0xc000;
const IS_RESPONSE_MASK: u16 = 0x8000;

// https://tools.ietf.org/html/rfc1035#section-3.1
const MAX_DOMAIN_SIZE: usize = 255;
const MAX_LABEL_SIZE: usize = 63;

fn is_compression_byte(b: u8) -> bool {
    b & DOMAIN_COMPRESSION_MASK_U8 == DOMAIN_COMPRESSION_MASK_U8
}

fn unwrap_domain_pointer(i: u16) -> u16 {
    i ^ DOMAIN_COMPRESSION_MASK_U16
}

/// Packet builder that doesn't do any auxiliary storage.
pub trait EmbeddedPacketBuilder {
    fn bytes_len(&self) -> usize;

    fn serialize<B: ByteSliceMut, BV: BufferViewMut<B>>(&self, bv: &mut BV);
}

struct BufferViewWrapper<B>(B);

impl<B: ByteSlice + Clone> BufferView<B> for BufferViewWrapper<B> {
    fn into_rest(self) -> B {
        self.0
    }

    fn take_front(&mut self, n: usize) -> Option<B> {
        if self.len() >= n {
            let (ret, next) = self.0.clone().split_at(n);
            self.0 = next;
            Some(ret)
        } else {
            None
        }
    }

    /// This isn't implemented as it currently is not used in this
    /// implementation.
    fn take_back(&mut self, _n: usize) -> Option<B> {
        unimplemented!()
    }
}

impl<B: ByteSlice> AsRef<[u8]> for BufferViewWrapper<B> {
    fn as_ref(&self) -> &[u8] {
        self.0.as_ref().clone().as_ref()
    }
}

/// Determines which error was run into during parsing.
///
/// For ones that contain lengths, this tells which length was encountered during
/// parsing. For example, `RDataLen` is a pretty general error relating to the
/// RData being the wrong size for the included type (both the size and type
/// are included. More below).
///
/// For `RData` errors:
/// -- `Type::A` the size was not found to be an IPv4 address.
/// -- `Type::Aaaa` the size was not found to be an IPv6 address.
/// -- `Type::Srv` the size of RData was not large enough to fit a SRV record
///    header as well as a payload.
///
/// `BadPointerIndex` returns the last encountered pointer that attempted to
/// reference data beyond the bounds of the available packet buffer.
///
/// `LabelTooLong` refers to there being too long of a label byte when parsing.
///
/// `DomainTooLong` refers to overrunning the maximum size of a domain
/// (255 bytes) when parsing.
#[derive(Debug, PartialEq)]
pub enum ParseError {
    RDataLen(Type, u16),
    Malformed,
    UnexpectedZeroCharacter,
    PointerCycle,
    BadPointerIndex(u16),
    DomainTooLong(usize),
    LabelTooLong(usize),
    UnknownType(u16),
    UnknownClass(u16),
}

/// Standard mDNS types supported in this protocol library.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum Type {
    A,
    Aaaa,
    Ptr,
    Srv,
    Txt,
}

impl From<Type> for u16 {
    fn from(value: Type) -> u16 {
        match value {
            Type::A => 1,
            Type::Aaaa => 28,
            Type::Ptr => 12,
            Type::Srv => 33,
            Type::Txt => 16,
        }
    }
}

impl TryFrom<u16> for Type {
    type Error = ParseError;

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Type::A),
            28 => Ok(Type::Aaaa),
            12 => Ok(Type::Ptr),
            33 => Ok(Type::Srv),
            16 => Ok(Type::Txt),
            v => Err(ParseError::UnknownType(v)),
        }
    }
}

/// Standard DNS classes supported by this protocol library.
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum Class {
    In,
    Any,
}

impl Class {
    /// Used for mapping with flush bool and unicast bool.
    fn into_u16_with_bool(self, b: bool) -> u16 {
        u16::from(self) | (b as u16) << 15
    }
}

impl From<Class> for u16 {
    fn from(value: Class) -> u16 {
        match value {
            Class::In => 1,
            Class::Any => 255,
        }
    }
}

impl TryFrom<u16> for Class {
    type Error = ParseError;

    fn try_from(value: u16) -> Result<Self, Self::Error> {
        match value {
            1 => Ok(Class::In),
            255 => Ok(Class::Any),
            v => Err(ParseError::UnknownClass(v)),
        }
    }
}

/// Represents an mDNS packet header.
#[repr(C)]
#[derive(FromBytes, AsBytes, Unaligned)]
pub struct Header {
    id: U16,
    flags: U16,
    question_count: U16,
    answer_count: U16,
    authority_count: U16,
    additional_count: U16,
}

impl Header {
    /// Returns true if this is a query (the first bit is zero).
    pub fn is_query(&self) -> bool {
        !self.is_response()
    }

    /// Returns true if this is a response (the first bit is 1).
    pub fn is_response(&self) -> bool {
        self.flags.get() & IS_RESPONSE_MASK != 0
    }

    /// Returns the question count of this header.
    pub fn question_count(&self) -> usize {
        self.question_count.get().into()
    }

    /// Returns the answer count of this header.
    pub fn answer_count(&self) -> usize {
        self.answer_count.get().into()
    }

    /// Returns the authority count of this header.
    pub fn authority_count(&self) -> usize {
        self.authority_count.get().into()
    }

    /// Returns the additional record count of this header.
    pub fn additional_count(&self) -> usize {
        self.additional_count.get().into()
    }
}

/// Represents a parsed mDNS question.
pub struct Question<B: ByteSlice> {
    pub domain: Domain<B>,
    pub qtype: Type,
    pub class: Class,
    pub unicast: bool,
}

impl<B: ByteSlice + Clone> Question<B> {
    fn parse<BV: BufferView<B>>(buffer: &mut BV, parent: Option<&B>) -> Result<Self, ParseError> {
        let domain = Domain::parse(buffer, parent)?;
        let qtype = buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?;
        let class_and_ucast = buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?;
        let unicast: bool = class_and_ucast.get() & (1u16 << 15) != 0;
        let class: u16 = class_and_ucast.get() & 0x7fff;
        Ok(Self { domain, qtype: qtype.get().try_into()?, class: class.try_into()?, unicast })
    }
}

impl<B: ByteSlice + Clone> Display for Question<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl<B: ByteSlice + Clone> Debug for Question<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}: type {:?}, class {:?}, unicast {}",
            self.domain, self.qtype, self.class, self.unicast,
        )
    }
}

/// A packet builder for an mDNS question.
pub struct QuestionBuilder {
    domain: DomainBuilder,
    qtype: Type,
    class: Class,
    unicast: bool,
}

impl QuestionBuilder {
    /// Constructs a QuestionBuilder.
    pub fn new(domain: DomainBuilder, qtype: Type, class: Class, unicast: bool) -> Self {
        Self { domain, qtype, class, unicast }
    }
}

impl EmbeddedPacketBuilder for QuestionBuilder {
    fn bytes_len(&self) -> usize {
        self.domain.bytes_len()
            + mem::size_of::<U16>() // type
            + mem::size_of::<U16>() // class + unicast
    }

    fn serialize<B: ByteSliceMut, BV: BufferViewMut<B>>(&self, bv: &mut BV) {
        self.domain.serialize(bv);
        bv.take_obj_front::<U16>().unwrap().set(self.qtype.into());
        bv.take_obj_front::<U16>().unwrap().set(self.class.into_u16_with_bool(self.unicast));
    }
}

/// A parsed SRV type record.
pub struct SrvRecord<B: ByteSlice> {
    priority: u16,
    weight: u16,
    port: u16,
    domain: Domain<B>,
}

impl<B: ByteSlice + Clone> SrvRecord<B> {
    fn parse<BV: BufferView<B>>(
        buffer: &mut BV,
        parent: Option<&B>,
        len_limit: u16,
    ) -> Result<Self, ParseError> {
        let priority = buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?.get();
        let weight = buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?.get();
        let port = buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?.get();
        let domain_buf = buffer.take_front(len_limit as usize).ok_or(ParseError::Malformed)?;
        // Needs a length limit as the SRV record necessitates it.
        let mut bv = BufferViewWrapper(domain_buf);
        let domain = Domain::parse(&mut bv, parent)?;
        // The domain should have consumed the entire buffer view.
        if bv.as_ref().len() != 0 {
            return Err(ParseError::Malformed);
        }
        Ok(Self { priority, weight, port, domain })
    }
}

/// A parsed RData (can be one of several types). If this has been parsed in a
/// PTR type, this will always be a `RData::Domain`. In a SRV type packet, this
/// will always be a `RData::Srv`, anything else, currently, will be converted
/// into `RData::Bytes`.
pub enum RData<B: ByteSlice> {
    Bytes(B),
    Srv(SrvRecord<B>),
    Domain(Domain<B>),
}

impl<B: ByteSlice> RData<B> {
    /// Returns a reference to a `SrvRecord` if possible `None` otherwise.
    fn srv(&self) -> Option<&SrvRecord<B>> {
        match self {
            RData::Srv(s) => Some(s),
            _ => None,
        }
    }

    /// Returns a `Domain` if possible, `None` otherwise. If this is a
    /// `RData::Srv` then this returns a reference to its internal `Domain`.
    fn domain(&self) -> Option<&Domain<B>> {
        match self {
            RData::Domain(d) => Some(d),
            RData::Srv(s) => Some(&s.domain),
            _ => None,
        }
    }

    // TODO(awdavies): This is used in tests, and will be useful for getting
    // strings out of Txt data later when there is an actual client
    // implementation.
    #[allow(unused)]
    fn bytes(&self) -> Option<&B> {
        match self {
            RData::Bytes(b) => Some(b),
            _ => None,
        }
    }
}

/// Record is the catch-all container for Answer, Authority, and Additional
/// Records sections of an MDNS packet. This is the parsed version that is
/// created when parsing a packet.
pub struct Record<B: ByteSlice> {
    pub domain: Domain<B>,
    pub rtype: Type,
    pub class: Class,
    pub ttl: u32,
    pub flush: bool,
    pub rdata: RData<B>,
}

impl<B: ByteSlice + Clone> Display for Record<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl<B: ByteSlice + Clone> Debug for Record<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: type {:?}, class {:?}", self.domain, self.rtype, self.class)?;
        match self.rtype {
            Type::Srv => {
                let srv = self.rdata.srv().unwrap();
                write!(
                    f,
                    ", priority {}, weight {}, port {}, target {}",
                    srv.priority, srv.weight, srv.port, srv.domain
                )
            }
            Type::Ptr => write!(f, ", {}", self.rdata.domain().unwrap()),
            // Don't print anything unless it's guaranteed that a certain type
            // will have certain data (Srv and Ptr will always be their
            // respective types for now).
            _ => Ok(()),
        }
    }
}

fn valid_rdata_len(r: Type, len: u16) -> Result<u16, ParseError> {
    match r {
        Type::A => {
            if len != IPV4_SIZE as u16 {
                return Err(ParseError::RDataLen(r, len));
            }
        }
        Type::Aaaa => {
            if len != IPV6_SIZE as u16 {
                return Err(ParseError::RDataLen(r, len));
            }
        }
        Type::Srv => {
            // Minimum size of SRV is the payload and enough for a domain
            // pointer or domain of a single character.
            if len < SRV_PAYLOAD_SIZE_OCTETS + 2 {
                return Err(ParseError::RDataLen(r, len));
            }
        }
        _ => (),
    }

    Ok(len)
}

impl<B: ByteSlice + Clone> Record<B> {
    fn parse<BV: BufferView<B>>(buffer: &mut BV, parent: Option<&B>) -> Result<Self, ParseError> {
        let domain = Domain::parse(buffer, parent)?;
        let rtype: Type =
            buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?.get().try_into()?;
        let class_and_flush = buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?;
        let flush = class_and_flush.get() & (1u16 << 15) != 0;
        let class: Class = (class_and_flush.get() & 0x7fff).try_into()?;
        let ttl = buffer.take_obj_front::<U32>().ok_or(ParseError::Malformed)?.get();
        let rdata_len = valid_rdata_len(
            rtype,
            buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?.get(),
        )?;
        let rdata = match rtype {
            Type::Srv => {
                RData::Srv(SrvRecord::parse(buffer, parent, rdata_len - SRV_PAYLOAD_SIZE_OCTETS)?)
            }
            Type::Ptr => {
                let ptr_domain_buf =
                    buffer.take_front(rdata_len.into()).ok_or(ParseError::Malformed)?;
                let mut ptr_domain_bv = BufferViewWrapper(ptr_domain_buf);
                let ptr_domain = Domain::parse(&mut ptr_domain_bv, parent)?;
                // Must consume the whole buffer.
                if ptr_domain_bv.as_ref().len() != 0 {
                    return Err(ParseError::Malformed);
                }
                RData::Domain(ptr_domain)
            }
            _ => RData::Bytes(buffer.take_front(rdata_len.into()).ok_or(ParseError::Malformed)?),
        };

        Ok(Self { domain, rtype, class, ttl, flush, rdata })
    }
}

/// A record builder for creating a serialized version of an mDNS Record, which
/// is the catch-all type for Answers, Additional Records, and Authority
/// records.
pub struct RecordBuilder<'a> {
    domain: DomainBuilder,
    rtype: Type,
    class: Class,
    flush: bool,
    ttl: u32,
    rdata: &'a [u8],
}

impl<'a> RecordBuilder<'a> {
    /// Constructs a `RecordBuilder`. Inputs must be valid for constructing an
    /// mDNS message or this will panic.
    ///
    /// # Panics
    ///
    /// Will panic if `rdata` is too large to have its length stored in a `u16`,
    /// which is necessary for successful serialization.
    ///
    /// Will panic if `rdata` is empty, as this is not supported.
    pub fn new(
        domain: DomainBuilder,
        rtype: Type,
        class: Class,
        flush: bool,
        ttl: u32,
        rdata: &'a [u8],
    ) -> Self {
        // Will panic if attempting to create too large of a message.
        let len = u16::try_from(rdata.len()).unwrap();
        if len == 0 {
            panic!("empty rdata not supported");
        }
        Self { domain, rtype, class, ttl, flush, rdata }
    }
}

impl EmbeddedPacketBuilder for RecordBuilder<'_> {
    fn bytes_len(&self) -> usize {
        self.domain.bytes_len()
            + mem::size_of::<U16>()  // type
            + mem::size_of::<U16>()  // class + flush
            + mem::size_of::<U32>()  // ttl
            + mem::size_of::<U16>()  // rdata_len
            + self.rdata.len()
    }

    fn serialize<B: ByteSliceMut, BV: BufferViewMut<B>>(&self, bv: &mut BV) {
        self.domain.serialize(bv);
        bv.take_obj_front::<U16>().unwrap().set(self.rtype.into());
        bv.take_obj_front::<U16>().unwrap().set(self.class.into_u16_with_bool(self.flush));
        bv.take_obj_front::<U32>().unwrap().set(self.ttl);
        bv.take_obj_front::<U16>().unwrap().set(u16::try_from(self.rdata.len()).unwrap());
        bv.take_front(self.rdata.len()).unwrap().copy_from_slice(self.rdata);
    }
}

/// A parsed mDNS message in its entirety.
pub struct Message<B: ByteSlice> {
    pub header: LayoutVerified<B, Header>,
    pub questions: Vec<Question<B>>,
    pub answers: Vec<Record<B>>,
    pub authority: Vec<Record<B>>,
    pub additional: Vec<Record<B>>,
}

impl<B: ByteSlice + Clone> Message<B> {
    #[inline]
    fn parse_records<BV: BufferView<B>>(
        buffer: &mut BV,
        parent: Option<&B>,
        count: usize,
    ) -> Result<Vec<Record<B>>, ParseError> {
        let mut records = Vec::<Record<B>>::with_capacity(count);
        for _ in 0..count {
            records.push(Record::parse(buffer, parent)?);
        }
        Ok(records)
    }
}

impl<B: ByteSlice + Clone> ParsablePacket<B, ()> for Message<B> {
    type Error = ParseError;

    fn parse<BV: BufferView<B>>(buffer: BV, _args: ()) -> Result<Self, Self::Error> {
        let body = buffer.into_rest();
        let mut buffer = BufferViewWrapper(body.clone());

        let header = buffer.take_obj_front::<Header>().ok_or(ParseError::Malformed)?;
        let mut questions: Vec<Question<B>> = Vec::with_capacity(header.question_count());
        for _ in 0..header.question_count.get() {
            questions.push(Question::parse(&mut buffer, Some(&body))?);
        }
        let answers = Message::parse_records(&mut buffer, Some(&body), header.answer_count())?;
        let authority = Message::parse_records(&mut buffer, Some(&body), header.authority_count())?;
        let additional =
            Message::parse_records(&mut buffer, Some(&body), header.additional_count())?;
        Ok(Self { header, questions, answers, authority, additional })
    }

    fn parse_metadata(&self) -> ParseMetadata {
        // ParseMetadata is only needed if we do undo parse.
        unimplemented!()
    }
}

/// A builder for creating an mDNS message.
pub struct MessageBuilder<'a> {
    pub id: u16,
    pub flags: u16,

    questions: Vec<QuestionBuilder>,
    answers: Vec<RecordBuilder<'a>>,
    authority: Vec<RecordBuilder<'a>>,
    additional: Vec<RecordBuilder<'a>>,
}

impl<'a> MessageBuilder<'a> {
    pub fn new(id: u16, is_query: bool) -> Self {
        let mut flags = 0u16;
        if !is_query {
            flags |= IS_RESPONSE_MASK;
        }
        Self {
            id,
            flags,
            questions: Vec::new(),
            answers: Vec::new(),
            authority: Vec::new(),
            additional: Vec::new(),
        }
    }

    pub fn add_question(&mut self, q: QuestionBuilder) {
        self.questions.push(q);
    }

    pub fn add_answer(&mut self, a: RecordBuilder<'a>) {
        self.answers.push(a);
    }

    pub fn add_authority(&mut self, a: RecordBuilder<'a>) {
        self.authority.push(a);
    }

    pub fn add_additional(&mut self, a: RecordBuilder<'a>) {
        self.additional.push(a);
    }
}

impl InnerPacketBuilder for MessageBuilder<'_> {
    fn bytes_len(&self) -> usize {
        mem::size_of::<Header>()
            + self.questions.iter().fold(0, |r, s| r + s.bytes_len())
            + self.answers.iter().fold(0, |r, s| r + s.bytes_len())
            + self.authority.iter().fold(0, |r, s| r + s.bytes_len())
            + self.additional.iter().fold(0, |r, s| r + s.bytes_len())
    }

    fn serialize(&self, mut buffer: &mut [u8]) {
        // Inherits BufferViewMut trait.
        let mut bv = &mut buffer;
        let mut header = bv.take_obj_front_zero::<Header>().unwrap();
        header.id.set(self.id);
        header.flags.set(self.flags);
        header.question_count.set(self.questions.len() as u16);
        header.answer_count.set(self.answers.len() as u16);
        header.authority_count.set(self.authority.len() as u16);
        header.additional_count.set(self.additional.len() as u16);
        self.questions.iter().for_each(|e| e.serialize(&mut bv));
        self.answers.iter().for_each(|e| e.serialize(&mut bv));
        self.authority.iter().for_each(|e| e.serialize(&mut bv));
        self.additional.iter().for_each(|e| e.serialize(&mut bv));
    }
}

/// A parsed mDNS domain. There is no need to worry about message compression
/// when comparing against a string, and can be treated as a contiguous domain.
#[derive(PartialEq, Eq)]
pub struct Domain<B: ByteSlice> {
    fragments: Vec<B>,
}

enum DomainData<B: ByteSlice> {
    Domain(B),
    Pointer(Option<B>, u16),
}

impl<B: ByteSlice + Clone> Domain<B> {
    fn fmt_byte_slice(f: &mut Formatter<'_>, b: &B) -> std::fmt::Result {
        let mut iter = b.as_ref().iter();
        let mut idx = 0;
        loop {
            let opt = iter.next();
            // Here it's possible that there's no null terminator (in the case
            // of pointers), but for any valid domain that has passed the
            // `parse` method, this should not be an issue.
            let c = match opt {
                Some(v) => v,
                None => break,
            };
            if *c == 0 {
                break;
            }
            if idx > 0 {
                f.write_str(".")?;
            }
            let skip = *c as usize;
            for _ in 0..skip {
                write!(f, "{}", *iter.next().unwrap() as char)?;
            }
            idx += 1;
        }
        Ok(())
    }

    fn partial_eq_helper_slice<BV: BufferView<B>>(
        other_bv: &mut BV,
        b: &B,
    ) -> Result<bool, ParseError> {
        // TODO(awdavies): This comparison and builder logic should probably
        // abstracted a bit.
        let mut dref = &mut b.as_ref();
        // Gets BufferView trait.
        let bv = &mut dref;
        loop {
            let domain_len = match bv.take_byte_front() {
                Some(d) => d,
                None => break,
            };
            if domain_len == 0 {
                break;
            }
            let mut other_len = 0u8;
            loop {
                match other_bv.take_byte_front() {
                    // At end of string or a '.' symbol.
                    Some(46) | None => {
                        if domain_len != other_len {
                            return Ok(false);
                        }
                        break;
                    }
                    Some(c) => {
                        if c != bv.take_byte_front().ok_or(ParseError::Malformed)? {
                            return Ok(false);
                        }
                        other_len += 1;
                    }
                }
            }
        }
        if bv.len() > 0 {
            return Ok(false);
        }
        Ok(true)
    }

    fn partial_eq_helper_str(&self, other: &str) -> Result<bool, ParseError> {
        let mut domain_bv = BufferViewWrapper(other.as_bytes());
        for d in self.fragments.iter() {
            if !Domain::partial_eq_helper_slice(&mut domain_bv, &d.as_ref())? {
                return Ok(false);
            }
        }
        return Ok(domain_bv.as_ref().len() == 0);
    }

    fn parse_domain_helper<BV: BufferView<B>>(
        buffer: &mut BV,
    ) -> Result<DomainData<B>, ParseError> {
        let mut iter = buffer.as_ref().iter();
        let mut idx = 0;
        loop {
            let domain_len = *iter.next().ok_or(ParseError::Malformed)?;
            idx += 1;
            // If this is a compression byte, then either we're at the end of
            // the domain, or this is the first byte of the domain and the whole
            // thing is determined by the pointer.
            if is_compression_byte(domain_len) {
                return match idx {
                    1 => {
                        let location =
                            buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?.get();
                        Ok(DomainData::Pointer(None, unwrap_domain_pointer(location)))
                    }
                    _ => {
                        let data = buffer.take_front(idx - 1).ok_or(ParseError::Malformed)?;
                        let location =
                            buffer.take_obj_front::<U16>().ok_or(ParseError::Malformed)?.get();
                        Ok(DomainData::Pointer(Some(data), unwrap_domain_pointer(location)))
                    }
                };
            }
            // If this is the null terminator, then we're either done iterating,
            // or this is a malformed label (if it is the first byte).
            if domain_len == 0 {
                if idx == 1 {
                    return Err(ParseError::UnexpectedZeroCharacter);
                }
                break;
            }
            if domain_len as usize > MAX_LABEL_SIZE {
                return Err(ParseError::LabelTooLong(domain_len.into()));
            }
            for _ in 0..domain_len {
                if *iter.next().ok_or(ParseError::Malformed)? == 0 {
                    return Err(ParseError::UnexpectedZeroCharacter)?;
                }
                idx += 1;
            }
        }
        if idx > MAX_DOMAIN_SIZE {
            return Err(ParseError::DomainTooLong(idx))?;
        }
        Ok(DomainData::Domain(buffer.take_front(idx).ok_or(ParseError::Malformed)?))
    }

    pub fn parse<BV: BufferView<B>>(
        buffer: &mut BV,
        parent: Option<&B>,
    ) -> Result<Self, ParseError> {
        let mut fragments = Vec::<B>::new();
        let mut pointer_set = HashSet::<u16>::new();
        let mut result = Domain::parse_domain_helper(buffer)?;
        loop {
            match result {
                DomainData::Domain(data) => {
                    fragments.push(data);
                    return Ok(Self { fragments });
                }
                DomainData::Pointer(data, pointer) => {
                    if let Some(d) = data {
                        fragments.push(d);
                    }
                    if pointer_set.contains(&pointer) {
                        return Err(ParseError::PointerCycle);
                    }
                    pointer_set.insert(pointer);
                    let mut bv = BufferViewWrapper(parent.unwrap().clone());
                    bv.take_front(pointer.into()).ok_or(ParseError::BadPointerIndex(pointer))?;
                    result = Domain::parse_domain_helper(&mut bv)?;
                }
            }
        }
    }
}

/// Implementation of PartialEq to make it possible to compare a parsed domain
/// with the initial string that was used to construct it.
impl<B: ByteSlice + Clone> PartialEq<&str> for Domain<B> {
    fn eq(&self, other: &&str) -> bool {
        self.partial_eq_helper_str(other).or_else::<bool, _>(|_| Ok(false)).unwrap()
    }
}

impl<B: ByteSlice + Clone> Display for Domain<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

impl<B: ByteSlice + Clone> Debug for Domain<B> {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let mut iter = self.fragments.iter();
        let data = iter.next().unwrap();
        Domain::fmt_byte_slice(f, data)?;
        loop {
            if let Some(next) = iter.next() {
                f.write_str(".")?;
                Domain::fmt_byte_slice(f, next)?;
            } else {
                return Ok(());
            }
        }
    }
}

/// An mDNS compliant domain builder. Does not support message compression.
#[derive(Clone, Debug, PartialEq)]
pub struct DomainBuilder {
    // TODO(awdavies): This can probably use the Buf struct instead.
    data: Vec<u8>,
}

impl DomainBuilder {
    /// Attempts to construct a domain from a string formatted according to DNS
    /// standards.
    ///
    /// Example usage:
    /// ```rust
    /// let domain = DomainBuilder::from_str("_fuchsia._udp.local")?;
    /// ```
    ///
    /// # Errors
    ///
    /// If the domain you supply is larger than `MAX_DOMAIN_SIZE` this will
    /// return an error. It is also an error if any individual label
    /// (the section of string between dots) is longer than 63 bytes.
    ///
    /// Currently, terminating a string with a dot is not supported.
    pub fn from_str(domain: &str) -> Result<Self, ParseError> {
        let mut data = Vec::<u8>::with_capacity(MAX_DOMAIN_SIZE);
        let mut domain_iter = domain.as_bytes().as_ref().iter();
        loop {
            data.push(0);
            // When copying is complete there will be one extra byte on the
            // beginning and end of the string, so the last_len_idx will be
            // equal to the total number of characters in the domain string plus
            // one.
            let last_len_idx = data.len() - 1;
            if last_len_idx == domain.len() + 1 {
                break;
            }
            let mut str_len = 0u8;
            loop {
                match domain_iter.next() {
                    // At end of string or a '.' symbol.
                    Some(46) | None => {
                        if str_len > MAX_LABEL_SIZE as u8 {
                            return Err(ParseError::Malformed);
                        }
                        data[last_len_idx] = str_len;
                        break;
                    }
                    Some(&c) => {
                        data.push(c);
                        str_len += 1;
                    }
                }
            }
        }
        if data.len() == 0 || data.len() > MAX_DOMAIN_SIZE {
            return Err(ParseError::Malformed);
        }
        Ok(Self { data })
    }
}

impl EmbeddedPacketBuilder for DomainBuilder {
    fn bytes_len(&self) -> usize {
        self.data.len()
    }

    fn serialize<B: ByteSliceMut, BV: BufferViewMut<B>>(&self, bv: &mut BV) {
        bv.take_front(self.data.len()).unwrap().copy_from_slice(self.data.as_slice());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use packet::{InnerPacketBuilder, ParseBuffer, Serializer};
    use std::fmt::Write;

    trait EmbeddedPacketBuilderTestExt: EmbeddedPacketBuilder {
        /// Convenience method for testing.
        fn serialize_to_buf(&self, mut buf: &mut [u8]) {
            let mut bv = &mut buf;
            self.serialize(&mut bv);
        }
    }
    impl<B: EmbeddedPacketBuilder> EmbeddedPacketBuilderTestExt for B {}

    struct DomainParseTest {
        packet: Vec<u8>,
        parsing_offset: usize,
        expected_result: &'static str,
    }

    // Some standard-looking domains gathered from the real world.
    const DOMAIN_STRING: &str = "_fuchsia._udp.local";
    const NODENAME_DOMAIN_STRING: &str = "thumb-set-human-shred._fuchsia._udp.local";
    const DOMAIN_BYTES: [u8; 21] = [
        0x08, 0x5f, 0x66, 0x75, 0x63, 0x68, 0x73, 0x69, 0x61, 0x04, 0x5f, 0x75, 0x64, 0x70, 0x05,
        0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x00,
    ];

    fn make_buf(size: usize) -> Vec<u8> {
        let mut buf = Vec::<u8>::with_capacity(size);
        for _ in 0..size {
            buf.push(0);
        }
        return buf;
    }

    #[test]
    fn test_parse_type() {
        const TYPES: [Type; 5] = [Type::A, Type::Aaaa, Type::Ptr, Type::Srv, Type::Txt];
        for t in TYPES.iter() {
            match Type::try_from(u16::from(*t)) {
                Ok(parsed_type) => assert_eq!(*t, parsed_type),
                Err(e) => panic!("parse error {:?}", e),
            }
        }
    }

    #[test]
    fn test_parse_class() {
        const CLASSES: [Class; 2] = [Class::In, Class::Any];
        for c in CLASSES.iter() {
            match Class::try_from(u16::from(*c)) {
                Ok(parsed_class) => assert_eq!(*c, parsed_class),
                Err(e) => panic!("parse error {:?}", e),
            }
        }
    }

    #[test]
    fn test_domain_parse() {
        let mut bv = BufferViewWrapper(&DOMAIN_BYTES[..]);
        let _ = Domain::parse(&mut bv, None).expect("Failed to parse");
    }

    #[test]
    fn test_domain_build_and_parse() {
        const BAD_DOMAIN_SHORT: &'static str = "_fuchsia._udp.loca";
        const BAD_DOMAIN_LONG: &'static str = "_fuchsia._udp.local.whatever.toooooolong";
        let domain = DomainBuilder::from_str(DOMAIN_STRING).unwrap();
        let mut buf = make_buf(domain.bytes_len());
        domain.serialize_to_buf(buf.as_mut_slice());

        let mut bv = BufferViewWrapper(buf.as_slice());
        let parsed = Domain::parse(&mut bv, None).unwrap();
        let mut s = String::new();
        write!(&mut s, "{}", parsed).unwrap();
        assert_eq!(s, DOMAIN_STRING);
        assert_eq!(parsed, DOMAIN_STRING);
        assert_ne!(parsed, BAD_DOMAIN_SHORT);
        assert_ne!(parsed, BAD_DOMAIN_LONG);
    }

    #[test]
    fn test_message_build_and_parse_one_question_one_record() {
        let domain = DomainBuilder::from_str(DOMAIN_STRING).unwrap();
        let nodename = DomainBuilder::from_str(NODENAME_DOMAIN_STRING).unwrap();
        let question = QuestionBuilder::new(domain, Type::Aaaa, Class::In, true);
        let record = RecordBuilder::new(
            nodename,
            Type::Ptr,
            Class::Any,
            true,
            4500,
            &[0x03, 'f' as u8, 'o' as u8, 'o' as u8, 0],
        );
        let mut message = MessageBuilder::new(0, true);
        message.add_question(question);
        message.add_additional(record);
        let mut msg_bytes = message
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("Failed to serialize"));
        let parsed = msg_bytes.parse::<Message<_>>().expect("Failed to parse!");
        // TODO(awdavies): These checks can probably be abstracted a bit.
        let q = &parsed.questions[0];
        assert_eq!(q.domain, DOMAIN_STRING);
        assert_eq!(q.qtype, Type::Aaaa);
        assert_eq!(q.class, Class::In);
        assert_eq!(q.unicast, true);
        assert_eq!(parsed.header.is_query(), true);
        assert_eq!(parsed.questions.len(), 1);
        assert_eq!(parsed.answers.len(), 0);
        assert_eq!(parsed.authority.len(), 0);
        assert_eq!(parsed.additional.len(), 1);
        let additional = &parsed.additional[0];
        assert_eq!(additional.domain, NODENAME_DOMAIN_STRING);
        assert_eq!(additional.ttl, 4500);
        assert_eq!(additional.flush, true);
        assert_eq!(additional.rtype, Type::Ptr);
        assert_eq!(additional.class, Class::Any);
        assert_eq!(additional.rdata.domain().unwrap(), &"foo");
    }

    #[test]
    fn test_question_build_and_parse() {
        let domain = DomainBuilder::from_str(DOMAIN_STRING).unwrap();
        let unicast = false;
        let qtype = Type::Aaaa;
        let class = Class::In;
        let question = QuestionBuilder::new(domain, qtype, class, unicast);
        let mut buf = make_buf(question.bytes_len());
        question.serialize_to_buf(buf.as_mut_slice());
        let mut bv = BufferViewWrapper(buf.as_ref());
        let parsed = Question::parse(&mut bv, None).unwrap();
        assert_eq!(parsed.unicast, unicast);
        assert_eq!(parsed.qtype, qtype);
        assert_eq!(parsed.class, class);
    }

    #[test]
    fn test_record_build_and_parse() {
        for r in [
            RecordBuilder {
                rdata: &[127, 0, 0, 1],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                ttl: 3500,
                rtype: Type::A,
                class: Class::In,
                flush: true,
            },
            RecordBuilder {
                rdata: &[
                    0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0x8e, 0xae, 0x4c, 0xff, 0xfe, 0xe9, 0xc9, 0xd3,
                ],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                ttl: 1,
                rtype: Type::Aaaa,
                class: Class::In,
                flush: false,
            },
            RecordBuilder {
                rdata: &[1, 2, 3, 4, 5, 6, 0x3, 'f' as u8, 'o' as u8, 'o' as u8, 0],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                ttl: 5000,
                rtype: Type::Srv,
                class: Class::In,
                flush: true,
            },
            RecordBuilder {
                rdata: &[0x04, 'q' as u8, 'u' as u8, 'u' as u8, 'x' as u8, 0x00],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                ttl: 10,
                rtype: Type::Ptr,
                class: Class::In,
                flush: false,
            },
        ]
        .iter()
        {
            let mut buf = make_buf(r.bytes_len());
            r.serialize_to_buf(buf.as_mut_slice());
            let mut bv = BufferViewWrapper(buf.as_ref());
            let parsed = Record::parse(&mut bv, None).unwrap();
            assert_eq!(parsed.domain, DOMAIN_STRING);
            assert_eq!(r.rtype, parsed.rtype);
            assert_eq!(r.ttl, parsed.ttl);
            assert_eq!(r.class, parsed.class);
            assert_eq!(r.flush, parsed.flush);
            match parsed.rtype {
                Type::Srv => {
                    assert_eq!(parsed.rdata.domain().unwrap(), &"foo");
                    let srv = parsed.rdata.srv().unwrap();
                    assert_eq!(srv.domain, "foo");
                    assert_eq!(srv.priority, 0x0102);
                    assert_eq!(srv.weight, 0x0304);
                    assert_eq!(srv.port, 0x0506);
                }
                Type::A | Type::Aaaa => assert_eq!(&r.rdata, parsed.rdata.bytes().unwrap()),
                Type::Ptr => assert_eq!(parsed.rdata.domain().unwrap(), &"quux"),
                _ => (),
            }
        }
    }

    #[test]
    fn test_srv_record_bad_sizing() {
        for r in [
            // RData with extra after null terminator.
            RecordBuilder {
                rdata: &[1, 2, 3, 4, 5, 6, 0x3, 'f' as u8, 'o' as u8, 'o' as u8, 0, 1, 2, 3, 4],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                class: Class::Any,
                flush: true,
                ttl: 2,
                rtype: Type::Srv,
            },
            // One byte too short.
            RecordBuilder {
                rdata: &[1, 2, 3, 4, 5],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                class: Class::Any,
                flush: true,
                ttl: 2,
                rtype: Type::Srv,
            },
            // Empty RData.
            RecordBuilder {
                rdata: &[],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                class: Class::Any,
                flush: true,
                ttl: 1,
                rtype: Type::Srv,
            },
            // Null domain.
            RecordBuilder {
                rdata: &[1, 2, 3, 4, 5, 6, 0],
                domain: DomainBuilder::from_str(DOMAIN_STRING).unwrap(),
                class: Class::Any,
                flush: true,
                ttl: 1,
                rtype: Type::Srv,
            },
        ]
        .iter()
        {
            let mut buf = make_buf(r.bytes_len());
            r.serialize_to_buf(buf.as_mut_slice());
            let mut bv = BufferViewWrapper(buf.as_ref());
            // Will panic if there is not an error.
            let _ = Record::parse(&mut bv, None).unwrap_err();
        }
    }

    #[test]
    fn test_domain_parse_no_trailing() {
        let mut bv = BufferViewWrapper(&DOMAIN_BYTES[..DOMAIN_BYTES.len() - 1]);
        assert_eq!(Domain::parse(&mut bv, None).unwrap_err(), ParseError::Malformed);
    }

    #[test]
    fn test_domain_parse_middle() {
        let packet = &mut DOMAIN_BYTES.to_vec();
        packet[3] = 0;
        let mut bv = BufferViewWrapper(&packet[..]);
        assert_eq!(Domain::parse(&mut bv, None).unwrap_err(), ParseError::UnexpectedZeroCharacter);
    }

    #[test]
    fn test_domain_parse_label_too_long() {
        let packet = &mut DOMAIN_BYTES.to_vec();
        let bad_len = 65u8;
        packet[0] = bad_len;
        let mut bv = BufferViewWrapper(&packet[..]);
        assert_eq!(
            Domain::parse(&mut bv, None).unwrap_err(),
            ParseError::LabelTooLong(bad_len.into())
        );
    }

    #[test]
    fn test_domain_parse_domain_too_long() {
        const LABELS: usize = 10;
        const SIZE: usize = MAX_LABEL_SIZE * LABELS;
        let mut packet = Vec::<u8>::with_capacity(SIZE);
        for _ in 0..LABELS {
            packet.push(MAX_LABEL_SIZE as u8);
            for _ in 0..MAX_LABEL_SIZE {
                packet.push('f' as u8);
            }
        }
        packet.push(0);
        let mut bv = BufferViewWrapper(packet.as_ref());
        assert_eq!(Domain::parse(&mut bv, None).unwrap_err(), ParseError::DomainTooLong(641));
    }

    #[test]
    fn test_domain_parse_empty_message() {
        const PACKET: [u8; 1] = [0];
        let mut bv = BufferViewWrapper(&PACKET[..]);
        assert_eq!(Domain::parse(&mut bv, None).unwrap_err(), ParseError::UnexpectedZeroCharacter);
    }

    #[test]
    fn test_domain_parse_short_malformed() {
        const PACKET: [u8; 2] = [1, 0];
        let mut bv = BufferViewWrapper(&PACKET[..]);
        assert_eq!(Domain::parse(&mut bv, None).unwrap_err(), ParseError::UnexpectedZeroCharacter);
    }

    #[test]
    fn test_domain_bad_pointer_index() {
        let packet: Vec<u8> = vec![0u8, 0x01, 'y' as u8, 0xc0, 0x09];
        let slice: &[u8] = packet.as_ref();
        let mut bv = BufferViewWrapper(slice.clone());
        bv.take_front(3).unwrap();
        assert_eq!(
            Domain::parse(&mut bv, Some(&slice)).unwrap_err(),
            ParseError::BadPointerIndex(0x09)
        );
    }

    #[test]
    fn test_domain_pointer_cycles() {
        for packet in [vec![0xc0, 0x00], vec![0x02, 0x02, 0x01, 0xc0, 0x05, 0xc0, 0x03]].iter() {
            let slice: &[u8] = packet.as_ref();
            let mut bv = BufferViewWrapper(slice.clone());
            assert_eq!(Domain::parse(&mut bv, Some(&slice)).unwrap_err(), ParseError::PointerCycle);
        }
    }

    #[test]
    fn test_domain_parse_fragmented_domains() {
        for test in [
            DomainParseTest {
                packet: vec![
                    0u8, 0x03, 'f' as u8, 'o' as u8, 'o' as u8, 0x03, 'b' as u8, 'a' as u8,
                    'r' as u8, 0x00, 0xc0, 0x01,
                ],
                expected_result: "foo.bar",
                parsing_offset: 10,
            },
            DomainParseTest {
                packet: vec![
                    0u8, 0x03, 'f' as u8, 'o' as u8, 'o' as u8, 0x03, 'b' as u8, 'a' as u8,
                    'r' as u8, 0x00, 0x03, 'b' as u8, 'a' as u8, 'z' as u8, 0x03, 'b' as u8,
                    'o' as u8, 'i' as u8, 0xc0, 0x01,
                ],
                expected_result: "baz.boi.foo.bar",
                parsing_offset: 10,
            },
            DomainParseTest {
                packet: vec![
                    2u8, 3u8, 0u8, 0x03, 'f' as u8, 'o' as u8, 'o' as u8, 0x03, 'b' as u8,
                    'a' as u8, 'r' as u8, 0x00, 0x03, 'b' as u8, 'a' as u8, 'z' as u8, 0x03,
                    'b' as u8, 'o' as u8, 'i' as u8, 0x07, '_' as u8, 'm' as u8, 'u' as u8,
                    'm' as u8, 'b' as u8, 'l' as u8, 'e' as u8, 0xc0, 0x03,
                ],
                expected_result: "baz.boi._mumble.foo.bar",
                parsing_offset: 12,
            },
            DomainParseTest {
                packet: vec![
                    2u8, 3u8, 0u8, 0x03, 'f' as u8, 'o' as u8, 'o' as u8, 0x03, 'b' as u8,
                    'a' as u8, 'r' as u8, 0xc0, 0x1f, 0x03, 'b' as u8, 'a' as u8, 'z' as u8, 0x03,
                    'b' as u8, 'o' as u8, 'i' as u8, 0x07, '_' as u8, 'm' as u8, 'u' as u8,
                    'm' as u8, 'b' as u8, 'l' as u8, 'e' as u8, 0xc0, 0x03, 0x04, 'q' as u8,
                    'u' as u8, 'u' as u8, 'x' as u8, 0x00,
                ],
                expected_result: "baz.boi._mumble.foo.bar.quux",
                parsing_offset: 13,
            },
        ]
        .iter()
        {
            let slice: &[u8] = test.packet.as_ref();
            let mut bv = BufferViewWrapper(slice.clone());
            bv.take_front(test.parsing_offset).unwrap();
            let parsed = Domain::parse(&mut bv, Some(&slice)).unwrap();
            let mut s = String::new();
            write!(&mut s, "{}", parsed).unwrap();
            assert_eq!(s, test.expected_result);
            assert_eq!(parsed, test.expected_result);
        }
    }

    #[test]
    fn test_real_world_mdns_packet_response() {
        // This is a real world mDNS packet from a Fuchsia device. These bytes
        // were copied from wireshark (and the fields were extracted from there
        // as well).
        //
        // The structure is as follows:
        // Header:
        //  -- Flags 0x8400
        //  -- Question count 0
        //  -- Answer count 1
        //  -- Authority count 0
        //  -- Additional count 4
        //
        //  Answer 1:
        //   -- Type: PTR
        //   -- Domain: '_fuchsia._udp.local'
        //   -- Class: IN
        //   -- Flush: False
        //   -- TTL: 4500
        //   -- Data Length: 24
        //   -- Data (uncompressed): thumb-set-human-shred._fuchsia._udp.local
        //
        //  Additional record 1:
        //   -- Type: SRV
        //   -- Domain: thumb-set-human-shred._fuchsia._udp.local
        //   -- Class: IN
        //   -- Flush: True
        //   -- TTL: 120
        //   -- Data Length: 30
        //   -- Priority: 0
        //   -- Weight: 0
        //   -- Port: 5353
        //   -- Target: thumb-set-human-shred.local
        //
        //  Additional record 2:
        //   -- Type: TXT
        //   -- Domain: thumb-set-human-shred._fuchsia._udp.local
        //   -- Class: IN
        //   -- Flush: True
        //   -- TTL: 4500
        //   -- Data Length: 0
        //   -- Data: '\0'
        //
        //  Additional record 3:
        //   -- Type: A
        //   -- Domain: thumb-set-human-shred.local
        //   -- Class: IN
        //   -- Flush: True
        //   -- TTL: 120
        //   -- Data Length: 4
        //   -- Data: '172.16.243.38'
        //
        //  Additional record 4:
        //   -- Type: AAAA
        //   -- Domain: thumb-set-human-shred.local
        //   -- Class: IN
        //   -- Flush: True
        //   -- TTL: 120
        //   -- Data length: 16
        //   -- Data: 'fe80::8eae:4cff:fee9:c9d3'
        let packet: Vec<u8> = vec![
            0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x08, 0x5f,
            0x66, 0x75, 0x63, 0x68, 0x73, 0x69, 0x61, 0x04, 0x5f, 0x75, 0x64, 0x70, 0x05, 0x6c,
            0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00,
            0x18, 0x15, 0x74, 0x68, 0x75, 0x6d, 0x62, 0x2d, 0x73, 0x65, 0x74, 0x2d, 0x68, 0x75,
            0x6d, 0x61, 0x6e, 0x2d, 0x73, 0x68, 0x72, 0x65, 0x64, 0xc0, 0x0c, 0xc0, 0x2b, 0x00,
            0x21, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x14,
            0xe9, 0x15, 0x74, 0x68, 0x75, 0x6d, 0x62, 0x2d, 0x73, 0x65, 0x74, 0x2d, 0x68, 0x75,
            0x6d, 0x61, 0x6e, 0x2d, 0x73, 0x68, 0x72, 0x65, 0x64, 0xc0, 0x1a, 0xc0, 0x2b, 0x00,
            0x10, 0x80, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x01, 0x00, 0xc0, 0x55, 0x00, 0x01,
            0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0xac, 0x10, 0xf3, 0x26, 0xc0, 0x55,
            0x00, 0x1c, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x10, 0xfe, 0x80, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x8e, 0xae, 0x4c, 0xff, 0xfe, 0xe9, 0xc9, 0xd3,
        ];
        let mut packet_slice = packet.as_slice();
        let parsed = packet_slice.parse::<Message<_>>().expect("Failed to parse!");
        assert!(parsed.header.is_response());
        assert_eq!(parsed.header.question_count(), 0);
        assert_eq!(parsed.header.answer_count(), 1);
        assert_eq!(parsed.header.authority_count(), 0);
        assert_eq!(parsed.header.additional_count(), 4);
        let answer = &parsed.answers[0];
        assert_eq!(answer.rtype, Type::Ptr);
        assert_eq!(answer.domain, "_fuchsia._udp.local");
        assert_eq!(answer.class, Class::In);
        assert_eq!(answer.flush, false);
        assert_eq!(answer.ttl, 4500);
        assert_eq!(answer.rdata.domain().unwrap(), &"thumb-set-human-shred._fuchsia._udp.local");
        let srv = &parsed.additional[0];
        assert_eq!(srv.rtype, Type::Srv);
        assert_eq!(srv.domain, "thumb-set-human-shred._fuchsia._udp.local");
        assert_eq!(srv.class, Class::In);
        assert_eq!(srv.flush, true);
        assert_eq!(srv.ttl, 120);
        let srv_rdata = srv.rdata.srv().unwrap();
        assert_eq!(srv_rdata.weight, 0);
        assert_eq!(srv_rdata.priority, 0);
        assert_eq!(srv_rdata.port, 5353);
        assert_eq!(srv_rdata.domain, "thumb-set-human-shred.local");
        let txt = &parsed.additional[1];
        assert_eq!(txt.rtype, Type::Txt);
        assert_eq!(txt.domain, "thumb-set-human-shred._fuchsia._udp.local");
        assert_eq!(txt.class, Class::In);
        assert_eq!(txt.flush, true);
        assert_eq!(txt.ttl, 4500);
        assert_eq!(txt.rdata.bytes().unwrap().len(), 1);
        let a = &parsed.additional[2];
        assert_eq!(a.rtype, Type::A);
        assert_eq!(a.domain, "thumb-set-human-shred.local");
        assert_eq!(a.class, Class::In);
        assert_eq!(a.ttl, 120);
        assert_eq!(a.rdata.bytes().unwrap().len(), IPV4_SIZE);
        assert_eq!(a.rdata.bytes().unwrap(), &[172, 16, 243, 38]);
        let aaaa = &parsed.additional[3];
        assert_eq!(aaaa.rtype, Type::Aaaa);
        assert_eq!(aaaa.domain, "thumb-set-human-shred.local");
        assert_eq!(aaaa.class, Class::In);
        assert_eq!(aaaa.ttl, 120);
        assert_eq!(aaaa.rdata.bytes().unwrap().len(), IPV6_SIZE);
        assert_eq!(
            aaaa.rdata.bytes().unwrap(),
            &[
                0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8e, 0xae, 0x4c, 0xff, 0xfe, 0xe9,
                0xc9, 0xd3
            ]
        );
    }

    #[test]
    fn test_real_world_mdns_packet_question() {
        let packet: Vec<u8> = vec![
            0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x5f,
            0x66, 0x75, 0x63, 0x68, 0x73, 0x69, 0x61, 0x04, 0x5f, 0x75, 0x64, 0x70, 0x05, 0x6c,
            0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01,
        ];
        let mut packet_slice = packet.as_slice();
        let parsed = packet_slice.parse::<Message<_>>().expect("Failed to parse!");
        assert!(parsed.header.is_query());
        assert_eq!(parsed.header.question_count(), 1);
        assert_eq!(parsed.header.answer_count(), 0);
        assert_eq!(parsed.header.authority_count(), 0);
        assert_eq!(parsed.header.additional_count(), 0);
        let q = &parsed.questions[0];
        assert_eq!(q.domain, "_fuchsia._udp.local");
        assert_eq!(q.qtype, Type::Ptr);
        assert_eq!(q.class, Class::In);
        assert_eq!(q.unicast, false);
    }
}
