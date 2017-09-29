// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Support for serializing and deserializing to fidl wire format.

use std::collections::HashMap;
use std::hash::Hash;
use std::mem;
use std::ptr;

use DecodeBuf;
use EncodeBuf;
use Result;
use Error;

use byteorder::{ByteOrder, LittleEndian};

use zircon::Handle;

/// A "type" of encodable message.
#[derive(PartialEq)]
pub enum EncodableType {
    /// A boolean value.
    Bool,
    /// A number.
    Num,
    /// A pointer.
    Pointer,
    /// A union of different types.
    Union,
    /// A Fuchsia handle.
    Handle,  // also includes InterfaceRequest
    /// A pointer to a type which implements some interface.
    InterfacePtr,
}

/// A type which can be encoded into a FIDL buffer.
pub trait Encodable {
    /// Write the value into the buf buffer at the given offset.
    /// The `base` index is always in units of bytes. The `offset` argument is usually
    /// bytes, but has a special interpretation for booleans, where it's bits.
    fn encode(self, buf: &mut EncodeBuf, base: usize, offset: usize);

    /// This method may be removed, it seems not to be needed
    fn encodable_type() -> EncodableType;

    /// Size of inline encoding, in the same units as `offset` param to `encode`.
    fn size() -> usize;

    /// Size of a vector of this type, in bytes.
    /// This is a trait method because vector representation is specialized for bool.
    fn vec_size(len: usize) -> usize {
        Self::size() * len
    }
}

// We'd love to have something along the lines of "impl<T: EncodableImpl> Encodable for T",
// but that would run smack into coherence / orphan rules. So instead we have a macro
// that just delegates to the implementation trait.

macro_rules! impl_encodable {
    ( ( $( $ty_params:tt )* ) $encodable_ty:ty ) => {
        impl< $( $ty_params )* > $crate::Encodable for $encodable_ty {
            fn encode(self, buf: &mut $crate::EncodeBuf, base: usize, offset: usize) {
                self.encode_impl(buf, base, offset);
            }
            fn encodable_type() -> $crate::EncodableType {
                Self::type_impl()
            }
            fn size() -> usize {
                Self::size_impl()
            }
        }
    };
    ( $( $encodable_ty:ty ),* ) => {
        $(
        impl $crate::Encodable for $encodable_ty {
            fn encode(self, buf: &mut $crate::EncodeBuf, base: usize, offset: usize) {
                self.encode_impl(buf, base, offset);
            }
            fn encodable_type() -> $crate::EncodableType {
                Self::type_impl()
            }
            fn size() -> usize {
                Self::size_impl()
            }
        }
        )*
    };
}

/// Types which may be decoded from a FIDL buffer.
///
/// Decodable is a subtrait of Encodable because there are strictly more types that
/// can implement the latter; Encodable supports encoding from both owned and borrowed
/// types, while Decodable requires ownership of the result.
pub trait Decodable: Encodable + Sized {
    /// Read the value from the buf buffer at the given offset.
    /// The `base` index is always in units of bytes. The `offset` argument is usually
    /// bytes, but has a special interpretation for booleans, where it's bits.
    ///
    /// For efficiency reasons, checking that base and offset are in range is the
    /// responsibility of the caller. The method may panic if not.
    fn decode(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<Self>;
}

macro_rules! impl_decodable {
    ( ( $( $ty_params:tt )* ) $decodable_ty:ty ) => {
        impl< $( $ty_params )* > $crate::Decodable for $decodable_ty {
            fn decode(buf: &mut $crate::DecodeBuf, base: usize, offset: usize) -> $crate::Result<Self> {
                Self::decode_impl(buf, base, offset)
            }
        }
    };
    ( $( $decodable_ty:ty ),* ) => {
        $(
        impl $crate::Decodable for $decodable_ty {
            fn decode(buf: &mut $crate::DecodeBuf, base: usize, offset: usize) -> $crate::Result<Self> {
                Self::decode_impl(buf, base, offset)
            }
        }
        )*
    };
}

impl Encodable for bool {
    fn encode(self, buf: &mut EncodeBuf, base: usize, offset: usize) {
        let self_num = if self { 1 } else { 0 };
        let byte_offset = base + (offset >> 3);
        buf.get_mut_slice(byte_offset, 1)[0] |= self_num << (offset & 7);
    }

    fn encodable_type() -> EncodableType {
        EncodableType::Bool
    }

    fn size() -> usize {
        1
    }

    fn vec_size(len: usize) -> usize {
        (len + 7) >> 3
    }
}

impl Decodable for bool {
    fn decode(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<bool> {
        let byte_offset = base + (offset >> 3);
        Ok(buf.get_bytes()[byte_offset] & (1 << (offset & 7)) != 0)
    }
}

/// Trait for FIDL-encodable numeric types.
trait CodableNum: Sized {
    /// Write the numeric value into a buffer.
    fn write(self, buf: &mut [u8]);

    /// Read the numeric value from a buffer.
    fn read(&[u8]) -> Self;

    /// Encode the numeric value into a buffer.
    fn encode_impl(self, buf: &mut EncodeBuf, base: usize, offset: usize) {
        self.write(buf.get_mut_slice(base + offset, Self::size_impl()));
    }

    /// Decode the numeric value from a buffer.
    fn decode_impl(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<Self> {
        let start = base + offset;
        Ok(Self::read(&buf.get_bytes()[start .. start + Self::size_impl()]))
    }

    /// The kind of encodable type.
    fn type_impl() -> EncodableType {
        EncodableType::Num
    }

    /// The size of the encoded type.
    fn size_impl() -> usize {
        mem::size_of::<Self>()
    }
}

#[macro_export]
macro_rules! impl_codable_for_num {
    ($($prim_type:ty => $reader:ident + $writer:ident),*) => {
        $(
        impl CodableNum for $prim_type {
            fn write(self, buf: &mut [u8]) {
                LittleEndian::$writer(buf, self);
            }

            fn read(buf: &[u8]) -> Self {
                LittleEndian::$reader(buf)
            }
        }

        impl_encodable!($prim_type);
        impl_decodable!($prim_type);
        )*
    }
}

impl_codable_for_num!(
    u16 => read_u16 + write_u16,
    u32 => read_u32 + write_u32,
    u64 => read_u64 + write_u64,
    i16 => read_i16 + write_i16,
    i32 => read_i32 + write_i32,
    i64 => read_i64 + write_i64,
    f32 => read_f32 + write_f32,
    f64 => read_f64 + write_f64
);

impl CodableNum for u8 {
    fn write(self, buf: &mut [u8]) {
        buf[0] = self;
    }

    fn read(buf: &[u8]) -> Self {
        buf[0]
    }
}

impl CodableNum for i8 {
    fn write(self, buf: &mut [u8]) {
        buf[0] = self as u8;
    }

    fn read(buf: &[u8]) -> Self {
        buf[0] as i8
    }
}

impl_encodable!(u8, i8);
impl_decodable!(u8, i8);

/// Option types, which are considered "nullable" in FIDL.
pub trait EncodableNullable : Encodable {
    /// The type of the null value;
    type NullType;

    /// Get a null value of the given nullable-encodable value.
    fn null_value() -> Self::NullType;
}

/// Option types which can be decoded.
pub trait DecodableNullable : EncodableNullable + Decodable {}

impl<T: EncodableNullable> Encodable for Option<T>
    where <T as EncodableNullable>::NullType: Encodable
{
    fn encode(self, buf: &mut EncodeBuf, base: usize, offset: usize) {
        match self {
            None => {
                Encodable::encode(T::null_value(), buf, base, offset);
                if <T as EncodableNullable>::NullType::size() < T::size() {
                    // Generate second null for union types (16 bytes)
                    Encodable::encode(T::null_value(), buf, base, offset);
                }
            }
            Some(val) => val.encode(buf, base, offset)
        }
    }

    fn encodable_type() -> EncodableType {
        EncodableType::Pointer
    }

    fn size() -> usize {
        T::size()
    }
}

impl<T: DecodableNullable> Decodable for Option<T>
    where <T as EncodableNullable>::NullType: Decodable + PartialEq
{
    fn decode(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<Self> {
        let val : T::NullType = Decodable::decode(buf, base, offset).unwrap();
        if val == T::null_value() {
            Ok(None)
        } else {
            Decodable::decode(buf, base, offset).map(Some)
        }
    }
}

/// FIDL Pointer types (structs, arrays, strings)
pub trait EncodablePtr: Sized {
    /// Size of body including header, does not need to be aligned
    fn body_size(&self) -> usize;

    /// Value for second word of header
    fn header_data(&self) -> u32;

    /// Encode the body of the pointer-based value.
    fn encode_body(self, buf: &mut EncodeBuf, base: usize);

    /// Encode the body of the pointer-based value at the provided offset.
    fn encode_impl(self, buf: &mut EncodeBuf, base: usize, offset: usize) {
        buf.encode_pointer(base + offset);
        self.encode_obj(buf);
    }

    /// Encode the object into an n `EncodeBuf`.
    fn encode_obj(self, buf: &mut EncodeBuf) {
        let total_size = self.body_size();
        let body_offset = buf.claim(total_size);
        (total_size as u32).encode(buf, body_offset, 0);
        self.header_data().encode(buf, body_offset, 4);
        self.encode_body(buf, body_offset + 8);
    }
}

// Decode the pointer and then the object header.
fn decode_ptr_header(buf: &mut DecodeBuf, base: usize, offset: usize)
    -> Result<(u32, u32, usize)>
{
    let start = base + offset;
    let rel_ptr = u64::decode(buf, start, 0).unwrap() as usize;
    if rel_ptr < 8 {
        return Err(Error::NotNullable);
    }
    // Ensure at least the size of the data header (8 bytes)
    // Note: len() - start >= 8 is invariant here, else the u64 decode would
    // have panicked.
    if rel_ptr > buf.get_bytes().len() - start - 8 {
        return Err(Error::OutOfRange);
    }
    // overflow is impossible, result must be <= buf.len() - 8
    decode_obj_header(buf, start + rel_ptr)
}

// With base pointing at the start of an object header, decode the header.
fn decode_obj_header(buf: &mut DecodeBuf, base: usize) -> Result<(u32, u32, usize)>
{
    // Precondition: buf len >= 8. Also note this condition can never be
    // triggered when decode_ptr_header is calling us; a super-smart compiler
    // might elide it.
    if base > buf.get_bytes().len() - 8 {
        return Err(Error::OutOfRange);
    }
    let size = u32::decode(buf, base, 0).unwrap();
    let val = u32::decode(buf, base, 4).unwrap();
    if size as usize > buf.get_bytes().len() - base {
        return Err(Error::OutOfRange);
    }
    // overflow is impossible, result must be <= buf.len()
    let body_start = base + 8;
    Ok((size, val, body_start))
}

/// A decodable pointer-based type.
pub trait DecodablePtr: EncodablePtr + Sized {
    /// Decode this pointer-based type from the provided buffer.
    ///
    /// size and val are the data header, base points past header. The callee can
    /// assume that at least `size` bytes are available in the buffer. That said,
    /// callee should check that the size is valid.
    fn decode_body(buf: &mut DecodeBuf, size: u32, val: u32, base: usize) -> Result<Self>;

    /// Decode this pointer-based type from the provided buffer.
    fn decode_obj(buf: &mut DecodeBuf, base: usize) -> Result<Self> {
        let (size, val, body_start) = try!(decode_obj_header(buf, base));
        Self::decode_body(buf, size, val, body_start)
    }

    /// Decode this pointer-based type from the provided buffer at the apppropriate `base` and `offset`.
    fn decode_impl(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<Self> {
        let (size, val, body_start) = try!(decode_ptr_header(buf, base, offset));
        // TODO: maybe delegate to decode_obj to reduce code duplication?
        Self::decode_body(buf, size, val, body_start)
    }
}

#[macro_export]
macro_rules! impl_encodable_ptr {
    ( ( $( $ty_params:tt )* ) $codable_ty:ty ) => {
        impl< $( $ty_params )* > $crate::Encodable for $codable_ty {
            fn encode(self, buf: &mut $crate::EncodeBuf, base: usize, offset: usize) {
                $crate::EncodablePtr::encode_impl(self, buf, base, offset);
            }
            fn encodable_type() -> $crate::EncodableType {
                $crate::EncodableType::Pointer
            }
            fn size() -> usize {
                8
            }
        }
        impl< $( $ty_params )* > $crate::EncodableNullable for $codable_ty {
            type NullType = u64;
            fn null_value() -> u64 { 0 }
        }
    };
    ( $( $codable_ty:ty ),* ) => {
        $(
        impl $crate::Encodable for $codable_ty {
            fn encode(self, buf: &mut $crate::EncodeBuf, base: usize, offset: usize) {
                $crate::EncodablePtr::encode_impl(self, buf, base, offset);
            }
            fn encodable_type() -> $crate::EncodableType {
                $crate::EncodableType::Pointer
            }
            fn size() -> usize {
                8
            }
        }
        impl $crate::EncodableNullable for $codable_ty {
            type NullType = u64;
            fn null_value() -> u64 { 0 }
        }
        )*
    };
}

#[macro_export]
macro_rules! impl_decodable_ptr {
    ( ( $( $ty_params:tt )* ) $codable_ty:ty ) => {
        impl< $( $ty_params )* > $crate::Decodable for $codable_ty {
            fn decode(buf: &mut $crate::DecodeBuf, base: usize, offset: usize) -> $crate::Result<Self> {
                $crate::DecodablePtr::decode_impl(buf, base, offset)
            }
        }
        impl< $( $ty_params )* > $crate::DecodableNullable for $codable_ty {
        }
    };
    ( $( $codable_ty:ty ),* ) => {
        $(
        impl $crate::Decodable for $codable_ty {
            fn decode(buf: &mut $crate::DecodeBuf, base: usize, offset: usize) -> $crate::Result<Self> {
                $crate::DecodablePtr::decode_impl(buf, base, offset)
            }
        }
        impl $crate::DecodableNullable for $codable_ty {
        }
        )*
    };
}

#[macro_export]
macro_rules! impl_codable_ptr {
    ( $( $params:tt )* ) => {
        impl_encodable_ptr!( $( $params )* );
        impl_decodable_ptr!( $( $params )* );
    };
}


impl<'a> EncodablePtr for &'a str {
    fn body_size(&self) -> usize {
        8 + self.len()
    }

    fn header_data(&self) -> u32 {
        self.len() as u32
    }

    fn encode_body(self, buf: &mut EncodeBuf, base: usize) {
        buf.get_mut_slice(base, self.len())
            .clone_from_slice(self.as_bytes());
    }
}

impl EncodablePtr for String {
    fn body_size(&self) -> usize {
        8 + self.len()
    }

    fn header_data(&self) -> u32 {
        self.len() as u32
    }

    fn encode_body(self, buf: &mut EncodeBuf, base: usize) {
        buf.get_mut_slice(base, self.len())
            .clone_from_slice(self.as_bytes());
    }
}

impl_encodable_ptr!(('a) &'a str);

impl<'a, T: Encodable + Clone> EncodablePtr for &'a [T] {
    fn body_size(&self) -> usize {
        8 + T::vec_size(self.len())
    }

    fn header_data(&self) -> u32 {
        self.len() as u32
    }

    // TODO: specializing for u8 at least would be nice
    fn encode_body(self, buf: &mut EncodeBuf, base: usize) {
        let mut offset = 0;
        for item in self {
            item.clone().encode(buf, base, offset);
            offset += T::size();
        }
    }
}

impl_encodable_ptr!(('a, T: 'a + Encodable + Clone) &'a [T]);

impl<T: Encodable> EncodablePtr for Vec<T> {
    fn body_size(&self) -> usize {
        8 + T::vec_size(self.len())
    }

    fn header_data(&self) -> u32 {
        self.len() as u32
    }

    // TODO: specializing for u8 at least would be nice
    fn encode_body(self, buf: &mut EncodeBuf, base: usize) {
        let mut offset = 0;
        for item in self {
            item.encode(buf, base, offset);
            offset += T::size();
        }
    }
}

impl<T: Decodable> DecodablePtr for Vec<T> {
    fn decode_body(buf: &mut DecodeBuf, size: u32, val: u32, base: usize) -> Result<Self> {
        let len = val as usize;
        if size as usize != 8 + T::vec_size(len) {
            return Err(Error::Invalid);
        }
        let mut result = Vec::with_capacity(len);
        for i in 0..len {
            result.push(try!(T::decode(buf, base, i * T::size())));
        }
        Ok(result)
    }
}

impl_encodable_ptr!((T: Encodable) Vec<T>);
impl_decodable_ptr!((T: Decodable) Vec<T>);

macro_rules! impl_codable_for_fixed_array {
    ($($len:expr),*) => {
        $(
        impl<T: Encodable> EncodablePtr for [T; $len] {
            fn body_size(&self) -> usize {
                8 + T::vec_size($len)
            }

            fn header_data(&self) -> u32 {
                $len as u32
            }

            // TODO: specializing for u8 at least would be nice
            fn encode_body(self, buf: &mut EncodeBuf, base: usize) {
                // Copy into a Vec, because moving out of a fixed size array is hard
                // TODO: figure out a more efficient way, or, alternatively, choose
                // a different Rust type to map fixed-size arrays in fidl
                let mut v: Vec<T> = Vec::with_capacity($len);
                unsafe {
                    ptr::copy_nonoverlapping(self.as_ptr(), v.as_mut_ptr(), $len);
                    mem::forget(self);
                    v.set_len($len);
                }
                v.encode_body(buf, base);
            }
        }
        impl<T: Decodable> DecodablePtr for [T; $len] {
            fn decode_body(buf: &mut DecodeBuf, size: u32, val: u32, base: usize) -> Result<Self> {
                let len = val as usize;
                if len != $len || size as usize != 8 + T::vec_size($len) {
                    return Err(Error::Invalid);
                }
                // Decode into a Vec, then copy out. Similar reason and concerns as
                // encode case.
                let mut v = try!(Vec::<T>::decode_body(buf, size, val, base));
                unsafe {
                    let mut result: [T; $len] = mem::uninitialized();
                    ptr::copy_nonoverlapping(v.as_ptr(), result.as_mut_ptr(), $len);
                    v.set_len(0);
                    Ok(result)
                }
            }
        }
        impl_encodable_ptr!((T: Encodable) [T; $len]);
        impl_decodable_ptr!((T: Decodable) [T; $len]);
        )*
    };
}

// Unfortunately, we cannot be generic over the length of a fixed array
// even though its part of the type (this will hopefully be added in the
// future) so for now we implement encodable for only the first 33 fixed
// size array types.
impl_codable_for_fixed_array!( 0,  1,  2,  3,  4,  5,  6,  7,
                               8,  9, 10, 11, 12, 13, 14, 15,
                              16, 17, 18, 19, 20, 21, 22, 23,
                              24, 25, 26, 27, 28, 29, 30, 31,
                              32);

impl DecodablePtr for String {
    fn decode_body(buf: &mut DecodeBuf, size: u32, val: u32, base: usize) -> Result<Self> {
        DecodablePtr::decode_body(buf, size, val, base).and_then(|vec|
            String::from_utf8(vec).map_err(|_err| Error::Utf8Error))
    }
}

impl_codable_ptr!(String);

impl<K: Encodable + Eq + Hash, V: Encodable> EncodablePtr for HashMap<K, V> {
    fn body_size(&self) -> usize {
        24
    }

    fn header_data(&self) -> u32 {
        0
    }

    fn encode_body(self, buf: &mut EncodeBuf, base: usize) {
        buf.encode_pointer(base);
        let key_vec_size = 8 + K::vec_size(self.len());
        let key_base = buf.claim(key_vec_size);
        (key_vec_size as u32).encode(buf, key_base, 0);
        (self.len() as u32).encode(buf, key_base, 4);
        // Allocate a vector to hold the values, to satisfy pointer ordering
        // constraints; if these are relaxed, we could just interleave the keys
        // and values as we iterate the map entries.
        let mut offset = 0;
        let vals_vec = self.into_iter().map(|(key, val)| {
            key.encode(buf, key_base + 8, offset);
            offset += V::size();
            val
        }).collect::<Vec<_>>();
        vals_vec.encode(buf, base, 8);
    }
}

impl_encodable_ptr!((K: Encodable + Eq + Hash, V: Encodable) HashMap<K, V>);

impl<K: Decodable + Eq + Hash, V: Decodable> DecodablePtr for HashMap<K, V> {
    fn decode_body(buf: &mut DecodeBuf, size: u32, arg: u32, base: usize) -> Result<Self> {
        if size != 16 || arg != 0 {
            return Err(Error::Invalid);
        }
        let (ksize, kval, kstart) = try!(decode_ptr_header(buf, base, 0));
        let (vsize, vval, vstart) = try!(decode_ptr_header(buf, base, 4));
        let klen = kval as usize;
        let vlen = vval as usize;
        if klen != vlen ||
            ksize as usize != 8 + K::vec_size(klen) ||
            vsize as usize != 8 + V::vec_size(vlen)
        {
            return Err(Error::Invalid);
        }
        let mut result = HashMap::with_capacity(klen);
        for i in 0..klen {
            let key = try!(K::decode(buf, kstart, i * K::size()));
            let val = try!(V::decode(buf, vstart, i * V::size()));
            result.insert(key, val);
        }
        Ok(result)
    }
}

impl_decodable_ptr!((K: Decodable + Eq + Hash, V: Decodable) HashMap<K, V>);

impl<T: Encodable> Encodable for Box<T> {
    fn encode(self, buf: &mut EncodeBuf, base: usize, offset: usize) {
        T::encode(*self, buf, base, offset);
    }

    fn encodable_type() -> EncodableType {
        T::encodable_type()
    }

    fn size() -> usize {
        T::size()
    }
}

impl<T: Decodable> Decodable for Box<T> {
    fn decode(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<Self> {
        T::decode(buf, base, offset).map(Box::new)
    }
}

// Support for generic implementation of Option<Box<T>>

impl<T: EncodableNullable> EncodableNullable for Box<T> {
    type NullType = T::NullType;
    fn null_value() -> Self::NullType {
        T::null_value()
    }
}

impl<T: DecodableNullable> DecodableNullable for Box<T> {
}

impl<T: CodableUnion> CodableUnion for Box<T> {
}

/// Encode a handle into a buffer at the provided base and offset.
pub fn encode_handle(handle: Handle, buf: &mut EncodeBuf, base: usize, offset: usize) {
    let index = buf.encode_handle(handle);
    index.encode(buf, base, offset)
}

/// Decode a handle from a buffer at the provided base and offset.
pub fn decode_handle(buf: &mut DecodeBuf, base: usize, offset: usize) -> Result<Handle> {
    let index = try!(u32::decode(buf, base, offset));
    buf.get_mut_buf().take_handle(index as usize).ok_or(Error::InvalidHandle)
}

#[macro_export]
macro_rules! impl_codable_handle {
    ( $( $codable_ty:ty ),* ) => {
        $(
        impl $crate::Encodable for $codable_ty {
            fn encode(self, buf: &mut $crate::EncodeBuf, base: usize, offset: usize) {
                $crate::encode_handle(self.into(), buf, base, offset);
            }
            fn encodable_type() -> $crate::EncodableType {
                $crate::EncodableType::Handle
            }
            fn size() -> usize {
                4
            }
        }
        impl $crate::EncodableNullable for $codable_ty {
            type NullType = u32;
            fn null_value() -> u32 { !0u32 }
        }
        impl $crate::Decodable for $codable_ty {
            fn decode(buf: &mut $crate::DecodeBuf, base: usize, offset: usize) -> $crate::Result<Self> {
                $crate::decode_handle(buf, base, offset).map(From::from)
            }
        }
        impl $crate::DecodableNullable for $codable_ty {
        }
        )*
    };
}

// Note: Add other handle types here as needed.
impl_codable_handle!(Handle);
impl_codable_handle!(::zircon::Channel);
impl_codable_handle!(::zircon::Event);
impl_codable_handle!(::zircon::EventPair);
impl_codable_handle!(::zircon::Job);
impl_codable_handle!(::zircon::Port);
impl_codable_handle!(::zircon::Process);
impl_codable_handle!(::zircon::Socket);
impl_codable_handle!(::zircon::Thread);
impl_codable_handle!(::zircon::Vmo);

/// A FIDL-encodeable and decodable union type.
pub trait CodableUnion : Encodable + Decodable {
    /// Encode the object as a pointer reference. Primarily used for nested unions.
    fn encode_as_ptr(self, buf: &mut EncodeBuf, base: usize) {
        buf.encode_pointer(base);
        let obj_offset = buf.claim(Self::size());
        self.encode(buf, obj_offset, 0);
    }

    /// Decode the object as pointer reference. Primarily used for nested unions.
    fn decode_as_ptr(buf: &mut DecodeBuf, start: usize) -> Result<Self> {
        let rel_ptr = u64::decode(buf, start, 0).unwrap() as usize;
        if rel_ptr < 8 {
            return Err(Error::NotNullable);
        }
        // Ensure at least the size of the union (16 bytes)
        // Note: we depend on len() - start >= 16 as a precondition.
        if rel_ptr > buf.get_bytes().len() - start - 16 {
            return Err(Error::OutOfRange);
        }
        // overflow is impossible, result must be <= buf.len() - 8
        Self::decode(buf, start + rel_ptr, 0)
    }

    /// Encode the optional object as a pointer.
    fn encode_opt_as_ptr(opt: Option<Self>, buf: &mut EncodeBuf, base: usize) {
        match opt {
            None => 0u64.encode(buf, base, 0),
            Some(u) => u.encode_as_ptr(buf, base),
        }
    }

    /// Dencode the optional object as a pointer.
    fn decode_opt_as_ptr(buf: &mut DecodeBuf, start: usize) -> Result<Option<Self>> {
        let rel_ptr = u64::decode(buf, start, 0).unwrap();
        if rel_ptr == 0 {
            Ok(None)
        } else {
            Self::decode_as_ptr(buf, start).map(Some)
        }
    }
}

#[macro_export]
macro_rules! impl_codable_union {
    ( $( $codable_ty:ty ),* ) => {
        $(
        impl $crate::CodableUnion for $codable_ty {
        }
        impl $crate::EncodableNullable for $codable_ty {
            type NullType = u64;
            fn null_value() -> u64 { 0 }
        }
        impl $crate::DecodableNullable for $codable_ty {
        }
        )*
    }
}
