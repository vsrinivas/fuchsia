// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encoding2 contains functions and traits for FIDL2 encoding and decoding.

use {Error, Result};
use std::{mem, ptr, str, u32, u64};
use zircon as zx;
use self::zx::HandleBased;

use byteorder::{ByteOrder, LittleEndian};

/// Rounds `x` up if necessary so that it is a multiple of `align`.
pub fn round_up_to_align(x: usize, align: usize) -> usize {
    if align == 0 {
        0
    } else {
        ((x + align - 1) / align) * align
    }
}

/// Split off the first element from a slice.
fn split_off_first<'a, T>(slice: &mut &'a [T]) -> Result<&'a T> {
    split_off_front(slice, 1).map(|res| &res[0])
}

/// Split off the first element from a mutable slice.
fn split_off_first_mut<'a, T>(slice: &mut &'a mut [T]) -> Result<&'a mut T> {
    split_off_front_mut(slice, 1).map(|res| &mut res[0])
}

/// Split of the first `n` bytes from `slice`.
fn split_off_front<'a, T>(slice: &mut &'a [T], n: usize) -> Result<&'a [T]> {
    if n > slice.len() {
        return Err(Error::OutOfRange);
    }
    let original = take_slice(slice);
    let (head, tail) = original.split_at(n);
    *slice = tail;
    Ok(head)
}

/// Split of the first `n` mutable bytes from `slice`.
fn split_off_front_mut<'a, T>(slice: &mut &'a mut [T], n: usize) -> Result<&'a mut [T]> {
    if n > slice.len() {
        return Err(Error::OutOfRange);
    }
    let original = take_slice_mut(slice);
    let (head, tail) = original.split_at_mut(n);
    *slice = tail;
    Ok(head)
}

/// Empty out a slice.
fn take_slice<'a, T>(x: &mut &'a [T]) -> &'a [T] {
    mem::replace(x, &mut [])
}

/// Empty out a mutable slice.
fn take_slice_mut<'a, T>(x: &mut &'a mut [T]) -> &'a mut [T] {
    mem::replace(x, &mut [])
}

#[doc(hidden)] // only exported for macro use
pub fn take_handle<T: HandleBased>(handle: &mut T) -> zx::Handle {
    let invalid = T::from_handle(zx::Handle::invalid());
    mem::replace(handle, invalid).into_handle()
}

/// The maximum recursion depth of encoding and decoding.
/// Each nested aggregate type (structs, unions, arrays, or vectors) counts as one step in the
/// recursion depth.
pub const MAX_RECURSION: usize = 32;

/// Indicates that an optional value is present.
pub const ALLOC_PRESENT_U64: u64 = u64::MAX;
/// Indicates that an optional value is present.
pub const ALLOC_PRESENT_U32: u32 = u32::MAX;
/// Indicates that an optional value is absent.
pub const ALLOC_ABSENT_U64: u64 = 0;
/// Indicates that an optional value is absent.
pub const ALLOC_ABSENT_U32: u32 = 0;

/// Encoding state
#[derive(Debug)]
pub struct Encoder<'a> {
    /// Offset at which to write new objects.
    offset: usize,

    /// The maximum remaining number of recursive steps.
    remaining_depth: usize,

    /// Buffer to write output data into.
    ///
    /// New chunks of out-of-line data should be appended to the end of the `Vec`.
    /// `buf` should be resized to be large enough for any new data *prior* to encoding the inline
    /// portion of that data.
    buf: &'a mut Vec<u8>,

    /// Buffer to write output handles into.
    handles: &'a mut Vec<zx::Handle>,
}

/// Decoding state
#[derive(Debug)]
pub struct Decoder<'a> {
    /// The maximum remaining number of recursive steps.
    remaining_depth: usize,

    /// Buffer from which to read data.
    buf: &'a [u8],

    /// Buffer from which to read out-of-line data.
    out_of_line_buf: &'a [u8],

    /// Buffer from which to read handles.
    handles: &'a mut [zx::Handle],
}

impl<'a> Encoder<'a> {
    /// FIDL2-encodes `x` into the provided data and handle buffers.
    pub fn encode<T: Encodable + ?Sized>(
        buf: &'a mut Vec<u8>,
        handles: &'a mut Vec<zx::Handle>,
        x: &mut T
    ) -> Result<()>
    {
        let inline_size = round_up_to_align(x.inline_size(), 8);
        buf.truncate(0);
        buf.resize(inline_size, 0);
        handles.truncate(0);

        let mut encoder = Encoder {
            offset: 0,
            remaining_depth: MAX_RECURSION,
            buf,
            handles,
        };

        x.encode(&mut encoder)
    }

    /// Runs the provided closure at at the next recursion depth level,
    /// erroring if the maximum recursion limit has been reached.
    pub fn recurse<F, R>(&mut self, f: F) -> Result<R>
        where F: FnOnce(&mut Encoder) -> Result<R>
    {
        if self.remaining_depth == 0 {
            return Err(Error::MaxRecursionDepth);
        }

        self.remaining_depth -= 1;
        let res = f(self)?;
        self.remaining_depth += 1;
        Ok(res)
    }

    /// Returns a slice of the next `len` bytes after `offset` and increases `offset` by `len`.
    pub fn next_slice(&mut self, len: usize) -> Result<&mut [u8]> {
        let ret = self.buf.get_mut(self.offset..(self.offset + len)).ok_or(Error::OutOfRange)?;
        self.offset += len;
        Ok(ret)
    }

    /// Runs the provided closure inside an encoder modified
    /// to write the data out-of-line.
    ///
    /// Returns the a result indicating the offset at which the data was written.
    ///
    /// Once the closure has completed, this function resets the offset
    /// to where it was at the beginning of the call.
    pub fn write_out_of_line<F>(&mut self, len: usize, f: F) -> Result<()>
        where F: FnOnce(&mut Encoder) -> Result<()>
    {
        let old_offset = self.offset;
        self.offset = self.buf.len();
        // Create space for the new data
        self.buf.resize(self.offset + round_up_to_align(len, 8), 0);
        f(self)?;
        self.offset = old_offset;
        Ok(())
    }
}

impl<'a> Decoder<'a> {
    /// FIDL2-decodes a value of type `T` from the provided data and handle buffers.
    pub fn decode_into<T: Decodable>(
        buf: &'a [u8],
        handles: &'a mut [zx::Handle],
        value: &mut T,
    ) -> Result<()>
    {
        let out_of_line_offset = round_up_to_align(T::inline_size(), 8);
        if buf.len() < out_of_line_offset {
            return Err(Error::OutOfRange);
        }

        let (buf, out_of_line_buf) = buf.split_at(out_of_line_offset);

        let mut decoder = Decoder {
            remaining_depth: MAX_RECURSION,
            buf,
            out_of_line_buf,
            handles,
        };

        value.decode(&mut decoder)
    }

    /// Runs the provided closure at at the next recursion depth level,
    /// erroring if the maximum recursion limit has been reached.
    pub fn recurse<F, R>(&mut self, f: F) -> Result<R>
        where F: FnOnce(&mut Decoder) -> Result<R>
    {
        if self.remaining_depth == 0 {
            return Err(Error::MaxRecursionDepth);
        }

        self.remaining_depth -= 1;
        let res = f(self)?;
        self.remaining_depth += 1;
        Ok(res)
    }

    /// Runs the provided closure inside an decoder modified
    /// to read out-of-line data.
    ///
    /// `absolute_offset` indicates the offset of the start of the out-of-line data to read,
    /// relative to the original start of the buffer.
    pub fn read_out_of_line<F, R>(&mut self, len: usize, f: F) -> Result<R>
        where F: FnOnce(&mut Decoder) -> Result<R>
    {
        // Currently, out-of-line points here:
        // [---------------------------------]
        //     ^---buf--^    ^-out-of-line--^ (slices)
        //
        // We want to shift so that `buf` points to the first `len` bytes in `out-of-line` that
        // are aligned to `align`, and `old-buf` points to the previous value of `buf`:
        //
        // [---------------------------------]
        //     ^old--buf^      ^--buf--^^ool^

        // We split off the first `len` bytes from `out_of_line`.
        let new_buf = split_off_front(&mut self.out_of_line_buf, len)?;
        // Split off any trailing bytes up to the alignment and discard them.
        if len % 8 != 0 {
            let trailer = 8 - (len % 8);
            let _ = split_off_front(&mut self.out_of_line_buf, trailer)?;
        }

        // Store the current `buf` slice and shift the `buf` slice to point at the out-of-line data.
        let old_buf = take_slice(&mut self.buf);
        self.buf = new_buf;
        let res = f(self);

        // Set the current `buf` back to its original position.
        //
        // After this transformation, the final `Decoder` looks like this:
        // [---------------------------------]
        //     ^---buf--^               ^ool^ (slices)
        //                              ^out-of-line-advanced (index)
        self.buf = old_buf;
        res
    }

    /// Returns a slice of the next `len` bytes to be decoded into and shifts the decoding buffer.
    pub fn next_slice(&mut self, len: usize) -> Result<&[u8]> {
        split_off_front(&mut self.buf, len)
    }

    /// Take the next handle from the `handles` list and shift the list down by one element.
    pub fn take_handle(&mut self) -> Result<zx::Handle> {
        split_off_first_mut(&mut self.handles).map(take_handle)
    }
}

/// A type which can be FIDL2-encoded into a buffer.
pub trait Encodable {
    /// Returns the minimum required alignment of the inline portion of the encoded object.
    fn inline_align(&self) -> usize;

    /// Returns the size of the inline portion of the encoded object.
    fn inline_size(&self) -> usize;

    /// Encode the object into the buffer.
    /// Any handles stored in the object are swapped for `zx::Handle::INVALID`.
    /// Calls to this function should ensure that `encoder.offset` is a multiple of `inline_size`.
    /// Successful calls to this function should increase `encoder.offset` by `inline_size`.
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()>;
}

/// A type which can be FIDL2-decoded from a buffer.
pub trait Decodable {
    /// Returns the minimum required alignment of the inline portion of the encoded object.
    fn inline_align() -> usize where Self: Sized;

    /// Returns the size of the inline portion of the encoded object.
    fn inline_size() -> usize where Self: Sized;

    /// Creates a new value of this type with an "empty" representation.
    fn new_empty() -> Self where Self: Sized;

    /// Decodes an object of this type from the provided buffer and list of handles.
    /// On success, returns `Self`, as well as the yet-unused tails of the data and handle buffers.
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()>;
}

macro_rules! impl_codable_num { ($($prim_ty:ty => $reader:ident + $writer:ident,)*) => { $(
    impl Encodable for $prim_ty {
        fn inline_align(&self) -> usize { mem::size_of::<$prim_ty>() }
        fn inline_size(&self) -> usize { mem::size_of::<$prim_ty>() }
        fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
            let slot = encoder.next_slice(mem::size_of::<Self>())?;
            LittleEndian::$writer(slot, *self);
            Ok(())
        }
    }

    impl Decodable for $prim_ty {
        fn new_empty() -> Self { 0 as $prim_ty }
        fn inline_size() -> usize { mem::size_of::<$prim_ty>() }
        fn inline_align() -> usize { mem::size_of::<$prim_ty>() }
        fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
            let end = mem::size_of::<Self>();
            let range = split_off_front(&mut decoder.buf, end)?;
            *self = LittleEndian::$reader(range);
            Ok(())
        }
    }
)* } }

impl_codable_num!(
    u16 => read_u16 + write_u16,
    u32 => read_u32 + write_u32,
    u64 => read_u64 + write_u64,
    i16 => read_i16 + write_i16,
    i32 => read_i32 + write_i32,
    i64 => read_i64 + write_i64,
    f32 => read_f32 + write_f32,
    f64 => read_f64 + write_f64,
);

impl Encodable for bool {
    fn inline_align(&self) -> usize { 1 }
    fn inline_size(&self) -> usize { 1 }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        let slot = encoder.next_slice(1)?;
        slot[0] = if *self { 1 } else { 0 };
        Ok(())
    }
}

impl Decodable for bool {
    fn new_empty() -> Self { false }
    fn inline_align() -> usize { 1 }
    fn inline_size() -> usize { 1 }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        let num = *split_off_first(&mut decoder.buf)?;
        *self = match num {
            0 => false,
            1 => true,
            _ => return Err(Error::Invalid),
        };
        Ok(())
    }
}

impl Encodable for u8 {
    fn inline_align(&self) -> usize { 1 }
    fn inline_size(&self) -> usize { 1 }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        let slot = encoder.next_slice(1)?;
        slot[0] = *self;
        Ok(())
    }
}

impl Decodable for u8 {
    fn new_empty() -> Self { 0 }
    fn inline_align() -> usize { 1 }
    fn inline_size() -> usize { 1 }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        *self = *split_off_first(&mut decoder.buf)?;
        Ok(())
    }
}

impl Encodable for i8 {
    fn inline_align(&self) -> usize { 1 }
    fn inline_size(&self) -> usize { 1 }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        let slot = encoder.next_slice(1)?;
        slot[0] = *self as u8;
        Ok(())
    }
}

impl Decodable for i8 {
    fn new_empty() -> Self { 0 }
    fn inline_align() -> usize { 1 }
    fn inline_size() -> usize { 1 }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        *self = *split_off_first(&mut decoder.buf)? as i8;
        Ok(())
    }
}

fn encode_array<T: Encodable>(encoder: &mut Encoder, slice: &mut [T]) -> Result<()> {
    encoder.recurse(|encoder| {
        for item in slice {
            item.encode(encoder)?;
        }
        Ok(())
    })
}

fn decode_array<T: Decodable>(decoder: &mut Decoder, slice: &mut [T]) -> Result<()> {
    decoder.recurse(|decoder| {
        for item in slice {
            item.decode(decoder)?;
        }
        Ok(())
    })
}

macro_rules! impl_codable_for_fixed_array { ($($len:expr,)*) => { $(
    impl<T: Encodable> Encodable for [T; $len] {
        fn inline_align(&self) -> usize {
            self.get(0).map(Encodable::inline_align).unwrap_or(0)
        }

        fn inline_size(&self) -> usize {
            self.get(0).map(Encodable::inline_size).unwrap_or(0)
        }

        fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
            encode_array(encoder, self)
        }
    }

    impl<T: Decodable> Decodable for [T; $len] {
        fn new_empty() -> Self {
            unsafe {
                // We wrap the `arr` in a `ManuallyDrop` to prevent it from
                // being dropped during a failure partway through initialization.
                let mut arr: mem::ManuallyDrop<[T; $len]> = mem::uninitialized();
                let arr_ptr: *mut T = arr.as_mut_ptr();
                for i in 0..$len {
                    ptr::write(arr_ptr.offset(i as isize), T::new_empty());
                }
                mem::ManuallyDrop::into_inner(arr)
            }
        }

        fn inline_align() -> usize { T::inline_align() }
        fn inline_size() -> usize { T::inline_size() * $len }

        fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
            decode_array(decoder, self)
        }
    }
)* } }

// Unfortunately, we cannot be generic over the length of a fixed array
// even though its part of the type (this will hopefully be added in the
// future) so for now we implement encodable for only the first 33 fixed
// size array types.
impl_codable_for_fixed_array!( 0,  1,  2,  3,  4,  5,  6,  7,
                               8,  9, 10, 11, 12, 13, 14, 15,
                              16, 17, 18, 19, 20, 21, 22, 23,
                              24, 25, 26, 27, 28, 29, 30, 31,
                              32,);
// Hack for FIDL library fuchsia.net
impl_codable_for_fixed_array!(256,);

fn encode_byte_slice(encoder: &mut Encoder, slice_opt: Option<&[u8]>) -> Result<()> {
    match slice_opt {
        None => {
            0u64.encode(encoder)?;
            0u64.encode(encoder)
        }
        Some(slice) => {
            // Two u64: (len, present)
            (slice.len() as u64).encode(encoder)?;
            ALLOC_PRESENT_U64.encode(encoder)?;
            encoder.write_out_of_line(slice.len(), |encoder| {
                let slot = encoder.next_slice(slice.len())?;
                slot.copy_from_slice(slice);
                Ok(())
            })
        }
    }
}

fn encode_encodable_iter<Iter, T>(
    encoder: &mut Encoder,
    iter_opt: Option<Iter>,
) -> Result<()>
where
    Iter: ExactSizeIterator<Item = T>,
    T: Encodable,
{
    match iter_opt {
        None => {
            0u64.encode(encoder)?;
            ALLOC_ABSENT_U64.encode(encoder)
        }
        Some(mut iter) => {
            // Two u64: (len, present)
            (iter.len() as u64).encode(encoder)?;
            ALLOC_PRESENT_U64.encode(encoder)?;
            let mut first = if let Some(first) = iter.next() {
                first
            } else {
                return Ok(())
            };
            let bytes_len = (iter.len() + 1) * first.inline_size();
            encoder.write_out_of_line(bytes_len, |encoder| {
                encoder.recurse(|encoder| {
                    first.encode(encoder)?;
                    for mut item in iter {
                        item.encode(encoder)?;
                    }
                    Ok(())
                })
            })
        }
    }
}

/// Attempts to decode a string into `string`, returning a `bool`
/// indicating whether or not a string was present.
fn decode_string(decoder: &mut Decoder, string: &mut String) -> Result<bool> {
    let mut len: u64 = 0;
    len.decode(decoder)?;

    let mut present: u64 = 0;
    present.decode(decoder)?;

    match present {
        ALLOC_ABSENT_U64 => return Ok(false),
        ALLOC_PRESENT_U64 => {},
        _ => return Err(Error::Invalid),
    };
    let len = len as usize;
    decoder.read_out_of_line(len, |decoder| {
        string.truncate(0);
        string.push_str(
            str::from_utf8(decoder.buf)
                .map_err(|_| Error::Utf8Error)?
        );
        Ok(true)
    })
}

/// Attempts to decode a vec into `vec`, returning a `bool`
/// indicating whether or not a vec was present.
fn decode_vec<T: Decodable>(decoder: &mut Decoder, vec: &mut Vec<T>) -> Result<bool> {
    let mut len: u64 = 0;
    len.decode(decoder)?;

    let mut present: u64 = 0;
    present.decode(decoder)?;

    match present {
        ALLOC_ABSENT_U64 => return Ok(false),
        ALLOC_PRESENT_U64 => {},
        _ => return Err(Error::Invalid),
    }

    let len = len as usize;
    let bytes_len = len * T::inline_size();
    decoder.read_out_of_line(bytes_len, |decoder| {
        decoder.recurse(|decoder| {
            vec.truncate(0);
            for _ in 0..len {
                vec.push(T::new_empty());
                // We just pushed an element on, so the `unwrap` will succeed.
                vec.last_mut().unwrap().decode(decoder)?;
            }
            Ok(true)
        })
    })
}

impl<'a> Encodable for &'a str {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_byte_slice(encoder, Some(self.as_bytes()))
    }
}

impl Encodable for String {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_byte_slice(encoder, Some(self.as_bytes()))
    }
}

impl Decodable for String {
    fn new_empty() -> Self {
        String::new()
    }

    fn inline_align() -> usize { 8 }

    fn inline_size() -> usize { 16 }

    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        if decode_string(decoder, self)? {
            Ok(())
        } else {
            Err(Error::NotNullable)
        }
    }
}

impl<'a> Encodable for Option<&'a str> {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_byte_slice(encoder, self.as_ref().map(|x| x.as_bytes()))
    }
}

impl Encodable for Option<String> {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_byte_slice(encoder, self.as_ref().map(|x| x.as_bytes()))
    }
}

impl Decodable for Option<String> {
    fn new_empty() -> Self {
        None
    }

    fn inline_align() -> usize { 8 }

    fn inline_size() -> usize { 16 }

    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        let was_some;
        {
            let string = self.get_or_insert(String::new());
            was_some = decode_string(decoder, string)?;
        }
        if !was_some { *self = None }
        Ok(())
    }
}

impl<'a, 'b: 'a, T: Encodable> Encodable for
    &'a mut ExactSizeIterator<Item = T>
{
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_encodable_iter(encoder, Some(self))
    }
}

impl<'a, T: Encodable> Encodable for &'a mut [T] {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_encodable_iter(encoder, Some(self.iter_mut()))
    }
}

impl<T: Encodable> Encodable for Vec<T> {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_encodable_iter(encoder, Some(self.iter_mut()))
    }
}

impl<T: Decodable> Decodable for Vec<T> {
    fn new_empty() -> Self {
        Vec::new()
    }

    fn inline_align() -> usize { 8 }

    fn inline_size() -> usize { 16 }

    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        if decode_vec(decoder, self)? {
            Ok(())
        } else {
            Err(Error::NotNullable)
        }
    }
}

impl<'a, 'b: 'a, T: Encodable> Encodable for
    Option<&'a mut ExactSizeIterator<Item = T>>
{
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_encodable_iter(encoder, self.as_mut().map(|x| &mut **x))
    }
}

impl<'a, T: Encodable> Encodable for Option<&'a mut [T]> {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_encodable_iter(encoder, self.as_mut().map(|x| x.iter_mut()))
    }
}

impl<T: Encodable> Encodable for Option<Vec<T>> {
    fn inline_align(&self) -> usize { 8 }

    fn inline_size(&self) -> usize { 16 }

    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        encode_encodable_iter(encoder, self.as_mut().map(|x| x.iter_mut()))
    }
}

impl<T: Decodable> Decodable for Option<Vec<T>> {
    fn new_empty() -> Self {
        None
    }

    fn inline_align() -> usize { 8 }

    fn inline_size() -> usize { 16 }

    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        let was_some;
        {
            let vec = self.get_or_insert(Vec::new());
            was_some = decode_vec(decoder, vec)?;
        }
        if !was_some { *self = None }
        Ok(())
    }
}

#[macro_export]
macro_rules! fidl2_inline_size {
    ($type:ty) => {
        <$type as $crate::encoding2::Decodable>::inline_size()
    }
}

#[macro_export]
macro_rules! fidl2_inline_align {
    ($type:ty) => {
        <$type as $crate::encoding2::Decodable>::inline_align()
    }
}

#[macro_export]
macro_rules! fidl2_encode {
    ($val:expr, $encoder:expr) => {
        $crate::encoding2::Encodable::encode($val, $encoder)
    }
}

#[macro_export]
macro_rules! fidl2_decode {
    ($val:expr, $decoder:expr) => {
        $crate::encoding2::Decodable::decode($val, $decoder)
    }
}

#[macro_export]
macro_rules! fidl2_new_empty {
    ($type:ty) => {
        <$type as $crate::encoding2::Decodable>::new_empty()
    }
}

#[macro_export]
macro_rules! fidl2_enum {
    ($name:ident ($prim_ty:ident) { $($key:ident = $value:expr,)* }) => {
        #[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
        #[repr($prim_ty)]
        pub enum $name {
            $(
                $key = $value,
            )*
        }

        impl $name {
            pub fn from_primitive(prim: $prim_ty) -> Option<Self> {
                match prim {
                    $(
                        $value => Some($name::$key),
                    )*
                    _ => None,
                }
            }

            pub fn into_primitive(self) -> $prim_ty {
                self as $prim_ty
            }
        }

        impl $crate::encoding2::Encodable for $name {
            fn inline_align(&self) -> usize {
                fidl2_inline_align!($prim_ty)
            }

            fn inline_size(&self) -> usize {
                fidl2_inline_size!($prim_ty)
            }

            fn encode(&mut self, encoder: &mut $crate::encoding2::Encoder)
                -> ::std::result::Result<(), $crate::Error>
            {
                fidl2_encode!(&mut (*self as $prim_ty), encoder)
            }
        }

        impl $crate::encoding2::Decodable for $name {
            fn new_empty() -> Self {
                // Returns the first declared variant
                #![allow(unreachable_code)]
                $(
                    return $name::$key;
                )*
                panic!("new_empty called on enum with no variants")
            }

            fn inline_align() -> usize {
                fidl2_inline_align!($prim_ty)
            }

            fn inline_size() -> usize {
                fidl2_inline_size!($prim_ty)
            }

            fn decode(&mut self, decoder: &mut $crate::encoding2::Decoder)
                -> ::std::result::Result<(), $crate::Error>
            {
                let mut prim = fidl2_new_empty!($prim_ty);
                fidl2_decode!(&mut prim, decoder)?;
                *self = Self::from_primitive(prim).ok_or($crate::Error::Invalid)?;
                Ok(())
            }
        }
    }
}

impl Encodable for zx::Status {
    fn inline_align(&self) -> usize { mem::size_of::<zx::sys::zx_status_t>() }
    fn inline_size(&self) -> usize { mem::size_of::<zx::sys::zx_status_t>() }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        let slot = encoder.next_slice(mem::size_of::<zx::sys::zx_status_t>())?;
        LittleEndian::write_i32(slot, self.into_raw());
        Ok(())
    }
}


impl Decodable for zx::Status {
    fn new_empty() -> Self { Self::from_raw(0) }
    fn inline_size() -> usize { mem::size_of::<zx::sys::zx_status_t>() }
    fn inline_align() -> usize { mem::size_of::<zx::sys::zx_status_t>() }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        let end = mem::size_of::<zx::sys::zx_status_t>();
        let range = split_off_front(&mut decoder.buf, end)?;
        *self = Self::from_raw(LittleEndian::read_i32(range));
        Ok(())
    }
}

impl Encodable for zx::Handle {
    fn inline_align(&self) -> usize { 4 }
    fn inline_size(&self) -> usize { 4 }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()>
    {
        ALLOC_PRESENT_U32.encode(encoder)?;
        let handle = take_handle(self);
        encoder.handles.push(handle);
        Ok(())
    }
}

impl Decodable for zx::Handle {
    fn new_empty() -> Self {
        zx::Handle::invalid()
    }
    fn inline_align() -> usize { 4 }
    fn inline_size() -> usize { 4 }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        let mut present: u32 = 0;
        present.decode(decoder)?;
        match present {
            ALLOC_ABSENT_U32 => return Err(Error::NotNullable),
            ALLOC_PRESENT_U32 => {},
            _ => return Err(Error::Invalid),
        }
        *self = decoder.take_handle()?;
        Ok(())
    }
}

impl Encodable for Option<zx::Handle> {
    fn inline_align(&self) -> usize { 4 }
    fn inline_size(&self) -> usize { 4 }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        match self {
            Some(handle) => handle.encode(encoder),
            None => ALLOC_ABSENT_U32.encode(encoder),
        }
    }
}

impl Decodable for Option<zx::Handle> {
    fn new_empty() -> Self { None }
    fn inline_align() -> usize { 4 }
    fn inline_size() -> usize { 4 }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        let mut present: u32 = 0;
        present.decode(decoder)?;
        match present {
            ALLOC_ABSENT_U32 => {
                *self = None;
                Ok(())
            },
            ALLOC_PRESENT_U32 => {
                *self = Some(decoder.take_handle()?);
                Ok(())
            },
            _ => Err(Error::Invalid),
        }
    }
}

// TODO(cramertj) replace when specialization is stable
#[macro_export]
macro_rules! handle_based_codable {
    ($($ty:ident$(:- <$($generic:ident,)*>)*, )*) => { $(
        impl<$($($generic,)*)*> $crate::encoding2::Encodable for $ty<$($($generic,)*)*> {
            fn inline_align(&self) -> usize { 4 }
            fn inline_size(&self) -> usize { 4 }
            fn encode(&mut self, encoder: &mut $crate::encoding2::Encoder)
                -> $crate::Result<()>
            {
                let mut handle = $crate::encoding2::take_handle(self);
                fidl2_encode!(&mut handle, encoder)
            }
        }

        impl<$($($generic,)*)*> $crate::encoding2::Decodable for $ty<$($($generic,)*)*> {
            fn new_empty() -> Self {
                <$ty<$($($generic,)*)*> as zx::HandleBased>::from_handle(zx::Handle::invalid())
            }
            fn inline_align() -> usize { 4 }
            fn inline_size() -> usize { 4 }
            fn decode(&mut self, decoder: &mut $crate::encoding2::Decoder)
                -> $crate::Result<()>
            {
                let mut handle = zx::Handle::invalid();
                fidl2_decode!(&mut handle, decoder)?;
                *self = <$ty<$($($generic,)*)*> as zx::HandleBased>::from_handle(handle);
                Ok(())
            }
        }

        impl<$($($generic,)*)*> $crate::encoding2::Encodable for Option<$ty<$($($generic,)*)*>> {
            fn inline_align(&self) -> usize { 4 }
            fn inline_size(&self) -> usize { 4 }
            fn encode(&mut self, encoder: &mut $crate::encoding2::Encoder)
                -> $crate::Result<()>
            {
                match self {
                    Some(handle) => fidl2_encode!(handle, encoder),
                    None => fidl2_encode!(&mut $crate::encoding2::ALLOC_ABSENT_U32, encoder),
                }
            }
        }

        impl<$($($generic,)*)*> $crate::encoding2::Decodable for Option<$ty<$($($generic,)*)*>> {
            fn new_empty() -> Self { None }
            fn inline_align() -> usize { 4 }
            fn inline_size() -> usize { 4 }
            fn decode(&mut self, decoder: &mut $crate::encoding2::Decoder) -> $crate::Result<()> {
                let mut handle: Option<zx::Handle> = None;
                fidl2_decode!(&mut handle, decoder)?;
                *self = handle.map(Into::into);
                Ok(())
            }
        }
    )* }
}

type ZxChannel = zx::Channel;
type ZxEvent = zx::Event;
type ZxEventPair = zx::EventPair;
type ZxFifo = zx::Fifo;
type ZxJob = zx::Job;
type ZxProcess = zx::Process;
type ZxSocket = zx::Socket;
type ZxThread = zx::Thread;
type ZxTimer = zx::Timer;
type ZxPort = zx::Port;
type ZxVmo = zx::Vmo;
handle_based_codable![
    ZxChannel,
    ZxEvent,
    ZxEventPair,
    ZxFifo,
    ZxJob,
    ZxProcess,
    ZxSocket,
    ZxThread,
    ZxTimer,
    ZxPort,
    ZxVmo,
];

/// A trait that provides automatic `Encodable` and `Decodable`
/// implementations for `Option<Box<Self>>`.
pub trait Autonull: Encodable + Decodable {}

/// A wrapper for FIDL types that will cause them to be encoded
/// or decoded from the out-of-line buffer rather than inline.
pub struct OutOfLine<'a, T: 'a>(pub &'a mut T);

impl<'a, T: Autonull> Encodable for Option<OutOfLine<'a, T>> {
    fn inline_align(&self) -> usize {
        fidl2_inline_align!(u64)
    }
    fn inline_size(&self) -> usize {
        fidl2_inline_size!(u64)
    }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        match self {
            Some(OutOfLine(inner)) => {
                ALLOC_PRESENT_U64.encode(encoder)?;
                encoder.write_out_of_line(
                    inner.inline_size(),
                    |encoder| inner.encode(encoder))
            }
            None => {
                ALLOC_ABSENT_U64.encode(encoder)
            }
        }
    }
}

impl<T: Autonull> Encodable for Option<Box<T>> {
    fn inline_align(&self) -> usize {
        fidl2_inline_align!(u64)
    }
    fn inline_size(&self) -> usize {
        fidl2_inline_size!(u64)
    }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        match self {
            Some(inner) => {
                ALLOC_PRESENT_U64.encode(encoder)?;
                encoder.write_out_of_line(
                    (*inner).inline_size(),
                    |encoder| (*inner).encode(encoder))
            }
            None => {
                ALLOC_ABSENT_U64.encode(encoder)
            }
        }
    }
}

impl<T: Autonull> Decodable for Option<Box<T>> {
    fn inline_align() -> usize {
        fidl2_inline_align!(u64)
    }
    fn inline_size() -> usize {
        fidl2_inline_size!(u64)
    }
    fn new_empty() -> Self {
        None
    }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        let mut present: u64 = 0;
        fidl2_decode!(&mut present, decoder)?;
        match present {
            ALLOC_ABSENT_U64 => {
                *self = None;
                Ok(())
            }
            ALLOC_PRESENT_U64 => {
                // Loop will only run once to set `self` to `Some` before decoding innards
                loop {
                    if let Some(inner) = self {
                        return decoder.read_out_of_line(
                            <T as Decodable>::inline_size(),
                            |decoder| (*inner).decode(decoder));
                    } else {
                        *self = Some(Box::new(T::new_empty()));
                    }
                }
            }
            _ => Err(Error::Invalid),
        }
    }
}

#[macro_export]
macro_rules! fidl2_struct {
    (
        name: $name:ty,
        members: [$(
            $member_name:ident {
                ty: $member_ty:ty,
                offset: $member_offset:expr,
            },
        )*],
        size: $size:expr,
        align: $align:expr,
    ) => {
        impl $crate::encoding2::Encodable for $name {
            fn inline_align(&self) -> usize {
                $align
            }

            fn inline_size(&self) -> usize {
                $size
            }

            fn encode(&mut self, encoder: &mut $crate::encoding2::Encoder) -> $crate::Result<()> {
                encoder.recurse(|encoder| {
                    let mut cur_offset = 0;
                    $(
                        // Skip to the start of the next field
                        encoder.next_slice($member_offset - cur_offset)?;
                        cur_offset = $member_offset;
                        fidl2_encode!(&mut self.$member_name, encoder)?;
                        cur_offset += fidl2_inline_size!($member_ty);
                    )*
                    // Skip to the end of the struct's size
                    encoder.next_slice($size - cur_offset)?;
                    Ok(())
                })
            }
        }

        impl $crate::encoding2::Decodable for $name {
            fn inline_align() -> usize {
                $align
            }

            fn inline_size() -> usize {
                $size
            }

            fn new_empty() -> Self {
                Self {
                    $(
                        $member_name: fidl2_new_empty!($member_ty),
                    )*
                }
            }

            fn decode(&mut self, decoder: &mut $crate::encoding2::Decoder) -> $crate::Result<()> {
                decoder.recurse(|decoder| {
                    let mut cur_offset = 0;
                    $(
                        // Skip to the start of the next field
                        decoder.next_slice($member_offset - cur_offset)?;
                        cur_offset = $member_offset;
                        fidl2_decode!(&mut self.$member_name, decoder)?;
                        cur_offset += fidl2_inline_size!($member_ty);
                    )*
                    // Skip to the end of the struct's size
                    decoder.next_slice($size - cur_offset)?;
                    Ok(())
                })
            }
        }

        impl $crate::encoding2::Autonull for $name {}
    }
}

#[macro_export]
macro_rules! fidl2_union {
    (
        name: $name:ident,
        members: [$(
            $member_name:ident {
                ty: $member_ty:ty,
                offset: $member_offset:expr,
            },
        )*],
        size: $size:expr,
        align: $align:expr,
    ) => {
        #[derive(Debug, PartialEq)]
        pub enum $name {
            $(
                $member_name ( $member_ty ),
            )*
        }

        impl $name {
            fn member_index(&self) -> u32 {
                #![allow(unused)]
                let mut index = 0;
                // TODO(cramertj): switch to `if let` when irrefutable `if let` patterns
                // stabilize
                $(
                    match *self {
                        $name::$member_name(_) => return index,
                        _ => index += 1,
                    }
                )*
                panic!("unreachable union member")
            }
        }

        impl $crate::encoding2::Encodable for $name {
            fn inline_align(&self) -> usize {
                $align
            }

            fn inline_size(&self) -> usize {
                $size
            }

            fn encode(&mut self, encoder: &mut $crate::encoding2::Encoder) -> $crate::Result<()> {
                let mut member_index = self.member_index();
                // Encode tag
                fidl2_encode!(&mut member_index, encoder)?;

                encoder.recurse(|encoder| {
                    match self { $(
                        $name::$member_name ( val ) => {
                            // Jump to offset minus 4-byte tag
                            encoder.next_slice($member_offset - 4)?;
                            // Encode value
                            fidl2_encode!(val, encoder)?;
                            // Skip to the end of the union's size
                            encoder.next_slice($size - (fidl2_inline_size!($member_ty) + $member_offset))?;
                            Ok(())
                        }
                    )* }
                })
            }
        }

        impl $crate::encoding2::Decodable for $name {
            fn inline_align() -> usize {
                $align
            }

            fn inline_size() -> usize {
                $size
            }

            fn new_empty() -> Self {
                #![allow(unreachable_code)]
                $(
                    return $name::$member_name(fidl2_new_empty!($member_ty));
                )*
                panic!("called new_empty on empty fidl union")
            }

            fn decode(&mut self, decoder: &mut $crate::encoding2::Decoder) -> $crate::Result<()> {
                #![allow(unused)]
                let mut tag: u32 = 0;
                fidl2_decode!(&mut tag, decoder)?;
                decoder.recurse(|decoder| {
                    let mut index = 0;
                    $(
                        if index == tag {
                            // Jump to offset minus 4-byte tag
                            decoder.next_slice($member_offset - 4)?;
                            // Loop will only ever run once-- if the variant is not correct,
                            // it is fixed up.
                            loop {
                                match self {
                                    $name::$member_name(val) => {
                                        fidl2_decode!(val, decoder)?;
                                        break;
                                    }
                                    _ => {}
                                }
                                *self = $name::$member_name(fidl2_new_empty!($member_ty));
                            }
                            // Skip to the end of the union's size
                            decoder.next_slice($size - (fidl2_inline_size!($member_ty) + $member_offset))?;
                            return Ok(());
                        }
                        index += 1;
                    )*
                    Err($crate::Error::UnknownUnionTag)
                })
            }
        }

        impl $crate::encoding2::Autonull for $name {}
    }
}

/// Header for transactional FIDL messages
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct TransactionHeader {
    /// Transaction ID which identifies a request-response pair
    pub tx_id: u32,
    /// Flags (always zero for now)
    pub flags: u32,
    /// Ordinal which identifies the FIDL method
    pub ordinal: u32,
}

fidl2_struct! {
    name: TransactionHeader,
    members: [
        tx_id {
            ty: u32,
            offset: 0,
        },
        flags {
            ty: u32,
            offset: 8, // Save 64 bits for id, even though it's only 32 bits
        },
        ordinal {
            ty: u32,
            offset: 12,
        },
    ],
    size: 16,
    align: 0,
}

/// Transactional FIDL message
pub struct TransactionMessage<'a, T: 'a> {
    /// Header of the message
    pub header: TransactionHeader,
    /// Body of the message
    pub body: &'a mut T,
}

impl<'a, T: Encodable> Encodable for TransactionMessage<'a, T> {
    fn inline_align(&self) -> usize { 0 }
    fn inline_size(&self) -> usize {
        self.header.inline_size() + self.body.inline_size()
    }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        self.header.encode(encoder)?;
        (*self.body).encode(encoder)?;
        Ok(())
    }
}

impl<'a, T: Decodable> Decodable for TransactionMessage<'a, T> {
    fn new_empty() -> Self {
        panic!("cannot create an empty transaction message")
    }
    fn inline_align() -> usize { 0 }
    fn inline_size() -> usize {
        <TransactionHeader as Decodable>::inline_size() + T::inline_size()
    }
    fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
        self.header.decode(decoder)?;
        (*self.body).decode(decoder)?;
        Ok(())
    }
}

/// Decode the transaction header from a message.
/// Returns the header and a reference to the tail of the message.
pub fn decode_transaction_header(bytes: &[u8]) -> Result<(TransactionHeader, &[u8])> {
    let mut header = TransactionHeader::new_empty();
    let header_len = <TransactionHeader as Decodable>::inline_size();
    if bytes.len() < header_len { return Err(Error::OutOfRange); }
    let (header_bytes, body_bytes) = bytes.split_at(header_len);
    let handles = &mut [];
    Decoder::decode_into(header_bytes, handles, &mut header)?;
    Ok((header, body_bytes))
}

// Implementations of Encodable for (&mut Head, ...Tail) and Decodable for (Head, ...Tail)
macro_rules! tuple_impls {
    () => {};

    (($idx:tt => $typ:ident), $( ($nidx:tt => $ntyp:ident), )*) => {
        /*
         * Invoke recursive reversal of list that ends in the macro expansion implementation
         * of the reversed list
        */
        tuple_impls!([($idx, $typ);] $( ($nidx => $ntyp), )*);
        tuple_impls!($( ($nidx => $ntyp), )*); // invoke macro on tail
    };

    /*
     * ([accumulatedList], listToReverse); recursively calls tuple_impls until the list to reverse
     + is empty (see next pattern)
    */
    ([$(($accIdx:tt, $accTyp:ident);)+]
        ($idx:tt => $typ:ident), $( ($nidx:tt => $ntyp:ident), )*) => {
      tuple_impls!([($idx, $typ); $(($accIdx, $accTyp); )*] $( ($nidx => $ntyp), ) *);
    };

    // Finally expand into the implementation
    ([($idx:tt, $typ:ident); $( ($nidx:tt, $ntyp:ident); )*]) => {
        impl<$typ, $( $ntyp ,)*>
            Encodable for ($typ, $( $ntyp ,)*)
            where $typ: Encodable,
                  $( $ntyp: Encodable,)*
        {
            fn inline_align(&self) -> usize {
                let mut max = 0;
                if max < self.$idx.inline_align() {
                    max = self.$idx.inline_align();
                }
                $(
                    if max < self.$nidx.inline_align() {
                        max = self.$nidx.inline_align();
                    }
                )*
                max
            }

            fn inline_size(&self) -> usize {
                let mut offset = 0;
                offset += self.$idx.inline_size();
                $(
                    offset = round_up_to_align(offset, self.$nidx.inline_align());
                    offset += self.$nidx.inline_size();
                )*
                offset
            }

            fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
                encoder.recurse(|encoder| {
                    let mut cur_offset = 0;
                    self.$idx.encode(encoder)?;
                    cur_offset += self.$idx.inline_size();
                    $(
                        // Skip to the start of the next field
                        let member_offset = round_up_to_align(cur_offset, self.$nidx.inline_align());
                        encoder.next_slice(member_offset - cur_offset)?;
                        cur_offset = member_offset;
                        self.$nidx.encode(encoder)?;
                        cur_offset += self.$nidx.inline_size();
                    )*
                    // Skip to the end of the struct's size
                    encoder.next_slice(self.inline_size() - cur_offset)?;
                    Ok(())
                })
            }
        }

        impl<$typ, $( $ntyp ),*> Decodable for ($typ, $( $ntyp, )*)
            where $typ: Decodable,
                  $( $ntyp: Decodable, )*
        {
            fn inline_align() -> usize {
                let mut max = 0;
                if max < $typ::inline_align() {
                    max = $typ::inline_align();
                }
                $(
                    if max < $ntyp::inline_align() {
                        max = $ntyp::inline_align();
                    }
                )*
                max
            }

            fn inline_size() -> usize {
                let mut offset = 0;
                offset += $typ::inline_size();
                $(
                    offset = round_up_to_align(offset, $ntyp::inline_align());
                    offset += $ntyp::inline_size();
                )*
                offset
            }

            fn new_empty() -> Self {
                (
                    $typ::new_empty(),
                    $(
                        $ntyp::new_empty(),
                    )*
                )
            }

            fn decode(&mut self, decoder: &mut Decoder) -> Result<()> {
                decoder.recurse(|decoder| {
                    let mut cur_offset = 0;
                    self.$idx.decode(decoder)?;
                    cur_offset += $typ::inline_size();
                    $(
                        // Skip to the start of the next field
                        let member_offset = round_up_to_align(cur_offset, $ntyp::inline_align());
                        decoder.next_slice(member_offset - cur_offset)?;
                        cur_offset = member_offset;
                        self.$nidx.decode(decoder)?;
                        cur_offset += $ntyp::inline_size();
                    )*
                    // Skip to the end of the struct's size
                    decoder.next_slice(Self::inline_size() - cur_offset)?;
                    Ok(())
                })
            }
        }
    }
}

tuple_impls!(
    (10 => K),
    (9 => J),
    (8 => I),
    (7 => H),
    (6 => G),
    (5 => F),
    (4 => E),
    (3 => D),
    (2 => C),
    (1 => B),
    (0 => A),
);

impl Encodable for () {
    fn inline_align(&self) -> usize { 0 }
    fn inline_size(&self) -> usize { 0 }
    fn encode(&mut self, _: &mut Encoder) -> Result<()> {
        Ok(())
    }
}

impl Decodable for () {
    fn new_empty() -> Self { () }
    fn inline_size() -> usize { 0 }
    fn inline_align() -> usize { 0 }
    fn decode(&mut self, _: &mut Decoder) -> Result<()> {
        Ok(())
    }
}

impl<'a, T> Encodable for &'a mut T where T: Encodable {
    fn inline_align(&self) -> usize { (**self).inline_align() }
    fn inline_size(&self) -> usize { (**self).inline_size() }
    fn encode(&mut self, encoder: &mut Encoder) -> Result<()> {
        (&mut **self).encode(encoder)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::{fmt, u64, i64, f32, f64};
    use self::zx::AsHandleRef;

    fn encode_decode<T: Encodable + Decodable>(start: &mut T) -> T {
        let buf = &mut Vec::new();
        let handle_buf = &mut Vec::new();
        Encoder::encode(buf, handle_buf, start)
            .expect("Encoding failed");
        let mut out = T::new_empty();
        Decoder::decode_into(buf, handle_buf, &mut out)
            .expect("Decoding failed");
        out
    }

    fn assert_identity<T>(mut x: T)
        where T: Encodable + Decodable + Clone + PartialEq + fmt::Debug
    {
        let cloned = x.clone();
        assert_eq!(cloned, encode_decode(&mut x));
    }

    macro_rules! identities { ($($x:expr,)*) => { $(
        assert_identity($x);
    )* } }

    #[test]
    fn encode_decode_byte() {
        identities![
            0u8, 57u8, 255u8, 0i8, -57i8, 12i8,
        ];
    }

    #[test]
    fn encode_decode_multibyte() {
        identities![
            0u64, 1u64, u64::MAX, u64::MIN,
            0i64, 1i64, i64::MAX, i64::MIN,
            0f32, 1f32, f32::MAX, f32::MIN,
            0f64, 1f64, f64::MAX, f64::MIN,
        ];
    }

    #[test]
    fn encode_decode_nan() {
        let nan32 = encode_decode(&mut f32::NAN);
        assert!(nan32.is_nan());

        let nan64 = encode_decode(&mut f64::NAN);
        assert!(nan64.is_nan());
    }

    #[test]
    fn encode_decode_out_of_line() {
        identities![
            Vec::<i32>::new(),
            vec![1, 2, 3],
            None::<Vec<i32>>,
            Some(Vec::<i32>::new()),
            Some(vec![1, 2, 3]),
            Some(vec![vec![1, 2, 3]]),
            Some(vec![Some(vec![1, 2, 3])]),
            "".to_string(),
            "foo".to_string(),
            None::<String>,
            Some("".to_string()),
            Some("foo".to_string()),
            Some(vec![None, Some("foo".to_string())]),
            vec!["foo".to_string(), "bar".to_string()],
        ];
    }

    #[test]
    fn encode_handle() {
        let mut handle = zx::Handle::from(zx::Port::create().expect("Port creation failed"));
        let raw_handle = handle.raw_handle();

        let buf = &mut Vec::new();
        let handle_buf = &mut Vec::new();
        Encoder::encode(buf, handle_buf, &mut handle)
            .expect("Encoding failed");

        assert!(handle.is_invalid());

        let mut handle_out = zx::Handle::new_empty();
        Decoder::decode_into(buf, handle_buf, &mut handle_out).expect("Decoding failed");

        assert_eq!(raw_handle, handle_out.raw_handle());
    }

    #[test]
    fn encode_decode_enum() {
        fidl2_enum!(Animal(i32) {
            Dog = 0,
            Cat = 1,
            Frog = 2,
        });

        assert_eq!(Animal::from_primitive(0), Some(Animal::Dog));
        assert_eq!(Animal::from_primitive(3), None);
        assert_eq!(Animal::Cat.into_primitive(), 1);

        identities![
            Animal::Dog,
            Animal::Cat,
            Animal::Frog,
            Animal::from_primitive(0).expect("should be dog"),
            Animal::from_primitive(Animal::Cat.into_primitive()).expect("should be cat"),
        ];
    }

    #[test]
    fn encode_decode_union() {
        fidl2_union! {
            name: NumOrStr,
            members: [
                Num {
                    ty: u64,
                    offset: 8,
                },
                Str {
                    ty: String,
                    offset: 8,
                },
            ],
            size: 24,
            align: 8,
        };

        // These need to be manually compared because of missing `PartialEq` impls.
        for num in vec![0, 255, 256] {
            match encode_decode(&mut NumOrStr::Num(num)) {
                NumOrStr::Num(out_num) if num == out_num => {},
                x => panic!("unexpected decoded value {:?}", x),
            }
        }

        for string in vec![String::new(), "hello world!".to_string()] {
            match &encode_decode(&mut NumOrStr::Str(string.clone())) {
                NumOrStr::Str(out_str) if out_str == &string => {},
                x => panic!("unexpected decoded value {:?}", x),
            }
        }
    }

    struct Foo {
        byte: u8,
        bignum: u64,
        string: String,
    }

    fidl2_struct! {
        name: Foo,
        members: [
            byte {
                ty: u8,
                offset: 0,
            },
            bignum {
                ty: u64,
                offset: 8,
            },
            string {
                ty: String,
                offset: 16,
            },
        ],
        size: 32,
        align: 8,
    }

    #[test]
    fn encode_decode_struct() {
        let out_foo = encode_decode(&mut Some(Box::new(Foo {
            byte: 5,
            bignum: 22,
            string: "hello world".to_string()
        }))).expect("should be some");

        assert_eq!(out_foo.byte, 5);
        assert_eq!(out_foo.bignum, 22);
        assert_eq!(out_foo.string, "hello world");

        let out_foo: Option<Box<Foo>> = encode_decode(&mut Box::new(None));
        assert!(out_foo.is_none());
    }

    #[test]
    fn encode_decode_tuple() {
        let mut start: (&mut u8, &mut u64, &mut String) = (
            &mut 5,
            &mut 10,
            &mut "foo".to_string(),
        );
        let mut out: (u8, u64, String) = Decodable::new_empty();

        let buf = &mut Vec::new();
        let handle_buf = &mut Vec::new();
        Encoder::encode(buf, handle_buf, &mut start)
            .expect("Encoding failed");
        Decoder::decode_into(buf, handle_buf, &mut out)
            .expect("Decoding failed");

        assert_eq!(*start.0, out.0);
        assert_eq!(*start.1, out.1);
        assert_eq!(*start.2, out.2);
    }

    #[test]
    fn encode_decode_struct_as_tuple() {
        let mut start = Foo { byte: 5, bignum: 10, string: "foo".to_string() };
        let mut out: (u8, u64, String) = Decodable::new_empty();

        let buf = &mut Vec::new();
        let handle_buf = &mut Vec::new();
        Encoder::encode(buf, handle_buf, &mut start)
            .expect("Encoding failed");
        Decoder::decode_into(buf, handle_buf, &mut out)
            .expect("Decoding failed");

        assert_eq!(start.byte, out.0);
        assert_eq!(start.bignum, out.1);
        assert_eq!(start.string, out.2);
    }

    #[test]
    fn encode_decode_tuple_as_struct() {
        let mut start = (&mut 5u8, &mut 10u64, &mut "foo".to_string());
        let mut out: Foo = Decodable::new_empty();

        let buf = &mut Vec::new();
        let handle_buf = &mut Vec::new();
        Encoder::encode(buf, handle_buf, &mut start)
            .expect("Encoding failed");
        Decoder::decode_into(buf, handle_buf, &mut out)
            .expect("Decoding failed");

        assert_eq!(*start.0, out.byte);
        assert_eq!(*start.1, out.bignum);
        assert_eq!(*start.2, out.string);
    }

    #[test]
    fn encode_decode_tuple_msg() {
        let mut body_start = (&mut "foo".to_string(), &mut 5);
        let mut body_out: (String, u8) = Decodable::new_empty();

        let buf = &mut Vec::new();
        let handle_buf = &mut Vec::new();
        Encoder::encode(buf, handle_buf, &mut body_start).unwrap();
        println!("buffer: {:?}", buf);
        Decoder::decode_into(buf, handle_buf, &mut body_out).unwrap();

        assert_eq!(body_start.0, &mut body_out.0);
        assert_eq!(body_start.1, &mut body_out.1);
    }

    #[test]
    fn encode_decode_transaction_msg() {
        let header = TransactionHeader {
            tx_id: 4,
            flags: 5,
            ordinal: 6,
        };
        let body = "hello".to_string();

        let start = &mut TransactionMessage { header, body: &mut body.clone() };

        let (buf, handles) = (&mut vec![], &mut vec![]);
        Encoder::encode(buf, handles, start).expect("Encoding failed");

        let (out_header, out_buf) = decode_transaction_header(&**buf).expect("Decoding header failed");
        assert_eq!(header, out_header);

        let mut body_out = String::new();
        Decoder::decode_into(out_buf, handles, &mut body_out).expect("Decoding body failed");
        assert_eq!(body, body_out);
    }
}
