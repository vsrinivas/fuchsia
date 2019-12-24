// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::{self, Read, Write};

use byteorder::{NativeEndian, ReadBytesExt, WriteBytesExt};
use thiserror::Error;

use fuchsia_trace as ftrace;
use fuchsia_zircon as zx;

use crate::{Fixed, NewId, ObjectId};

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum ArgKind {
    Int,
    Uint,
    Fixed,
    String,
    Object,
    NewId,
    Array,
    Handle,
}

#[derive(Debug)]
pub enum Arg {
    Int(i32),
    Uint(u32),
    Fixed(Fixed),
    String(String),
    Object(ObjectId),
    NewId(NewId),
    Array(Array),
    Handle(zx::Handle),
}

impl Arg {
    pub fn kind(&self) -> ArgKind {
        match self {
            Arg::Int(_) => ArgKind::Int,
            Arg::Uint(_) => ArgKind::Uint,
            Arg::Fixed(_) => ArgKind::Fixed,
            Arg::String(_) => ArgKind::String,
            Arg::Object(_) => ArgKind::Object,
            Arg::NewId(_) => ArgKind::NewId,
            Arg::Array(_) => ArgKind::Array,
            Arg::Handle(_) => ArgKind::Handle,
        }
    }
}

macro_rules! impl_unwrap_arg(
    ($name:ident, $type:ty, $enumtype:ident) => (
        pub fn $name(self) -> $type {
            if let Arg::$enumtype(x) = self {
                x
            } else {
                panic!("Argument is not of the required type: \
                        expected {:?}, found {:?}",
                        stringify!($enumtype), self);
            }
        }
    )
);

impl Arg {
    impl_unwrap_arg!(unwrap_int, i32, Int);
    impl_unwrap_arg!(unwrap_uint, u32, Uint);
    impl_unwrap_arg!(unwrap_fixed, Fixed, Fixed);
    impl_unwrap_arg!(unwrap_object, ObjectId, Object);
    impl_unwrap_arg!(unwrap_new_id, ObjectId, NewId);
    impl_unwrap_arg!(unwrap_string, String, String);
    impl_unwrap_arg!(unwrap_array, Array, Array);
    impl_unwrap_arg!(unwrap_handle, zx::Handle, Handle);
}

#[derive(Debug, Error)]
#[error("Argument is not of the required type: expected {:?}, found {:?}", expected, found)]
pub struct MismatchedArgKind {
    pub expected: ArgKind,
    pub found: ArgKind,
}

macro_rules! impl_as_arg(
    ($name:ident, $type:ty, $enumtype:ident) => (
        pub fn $name(self) -> Result<$type, MismatchedArgKind> {
            if let Arg::$enumtype(x) = self {
                Ok(x)
            } else {
                Err(MismatchedArgKind {
                    expected: ArgKind::$enumtype,
                    found: self.kind(),
                })
            }
        }
    )
);

impl Arg {
    impl_as_arg!(as_int, i32, Int);
    impl_as_arg!(as_uint, u32, Uint);
    impl_as_arg!(as_fixed, Fixed, Fixed);
    impl_as_arg!(as_object, ObjectId, Object);
    impl_as_arg!(as_new_id, ObjectId, NewId);
    impl_as_arg!(as_string, String, String);
    impl_as_arg!(as_array, Array, Array);
    impl_as_arg!(as_handle, zx::Handle, Handle);
}

/// Simple conversion from the inner arg types to the Arg wrapper.
macro_rules! impl_from_for_arg(
    ($enumtype:ident, $type:ty) => (
        impl From<$type> for Arg {
            fn from(v: $type) -> Self {
                Arg::$enumtype(v)
            }
        }
    )
);
impl_from_for_arg!(Int, i32);
impl_from_for_arg!(Uint, u32);
impl_from_for_arg!(Fixed, Fixed);
impl_from_for_arg!(String, String);
impl_from_for_arg!(Array, Array);
impl_from_for_arg!(Handle, zx::Handle);

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct MessageHeader {
    pub sender: u32,
    pub opcode: u16,
    pub length: u16,
}

#[derive(Debug)]
pub struct Message {
    byte_buf: io::Cursor<Vec<u8>>,
    handle_buf: Vec<zx::Handle>,
}

fn compute_padding(size: u64) -> usize {
    (-(size as i64) & 3) as usize
}

impl Message {
    /// Initialize an empty Message.
    pub fn new() -> Self {
        Message { byte_buf: io::Cursor::new(Vec::new()), handle_buf: Vec::new() }
    }

    pub fn from_parts(bytes: Vec<u8>, handles: Vec<zx::Handle>) -> Self {
        Message { byte_buf: io::Cursor::new(bytes), handle_buf: handles }
    }

    /// Returns |true| iff this buffer has no more data (bytes or handles).
    pub fn is_empty(&self) -> bool {
        self.byte_buf.get_ref().len() as u64 == self.byte_buf.position()
            && self.handle_buf.is_empty()
    }

    pub fn clear(&mut self) {
        self.byte_buf.set_position(0);
        self.byte_buf.get_mut().truncate(0);
        self.handle_buf.truncate(0);
    }

    pub fn rewind(&mut self) {
        self.byte_buf.set_position(0);
    }

    pub fn bytes(&self) -> &[u8] {
        self.byte_buf.get_ref().as_slice()
    }

    pub fn take(self) -> (Vec<u8>, Vec<zx::Handle>) {
        (self.byte_buf.into_inner(), self.handle_buf)
    }

    /// Converts the value in |arg| into the appropriate wire format
    /// serialization.
    pub fn write_arg(&mut self, arg: Arg) -> io::Result<()> {
        ftrace::duration!("wayland", "Message::write_arg");
        match arg {
            Arg::Int(i) => self.byte_buf.write_i32::<NativeEndian>(i),
            Arg::Uint(i) => self.byte_buf.write_u32::<NativeEndian>(i),
            Arg::Fixed(i) => self.byte_buf.write_i32::<NativeEndian>(i.bits()),
            Arg::Object(i) => self.byte_buf.write_u32::<NativeEndian>(i),
            Arg::NewId(i) => self.byte_buf.write_u32::<NativeEndian>(i),
            Arg::String(s) => self.write_slice(s.as_bytes(), true),
            Arg::Array(a) => self.write_slice(a.as_slice(), false),
            Arg::Handle(h) => {
                self.handle_buf.push(h);
                Ok(())
            }
        }
    }

    /// Reads an Arg out of this Message and return the value.
    pub fn read_arg(&mut self, arg: ArgKind) -> io::Result<Arg> {
        ftrace::duration!("wayland", "Message::read_arg");
        match arg {
            ArgKind::Int => self.byte_buf.read_i32::<NativeEndian>().map(Arg::Int),
            ArgKind::Uint => self.byte_buf.read_u32::<NativeEndian>().map(Arg::Uint),
            ArgKind::Fixed => {
                self.byte_buf.read_i32::<NativeEndian>().map(|i| Arg::Fixed(i.into()))
            }
            ArgKind::Object => self.byte_buf.read_u32::<NativeEndian>().map(Arg::Object),
            ArgKind::NewId => self.byte_buf.read_u32::<NativeEndian>().map(Arg::NewId),
            ArgKind::String => self
                .read_slice(true)
                .map(|vec| String::from_utf8_lossy(vec.as_slice()).to_string())
                .map(Arg::String),
            ArgKind::Array => self.read_slice(false).map(|v| Arg::Array(v.into())),
            ArgKind::Handle => {
                if !self.handle_buf.is_empty() {
                    Ok(Arg::Handle(self.handle_buf.remove(0)))
                } else {
                    Err(io::Error::new(
                        io::ErrorKind::UnexpectedEof,
                        "Unable to read handle from Message",
                    ))
                }
            }
        }
    }

    /// Reads the set of arguments specified by the given &[ArgKind].
    pub fn read_args(&mut self, args: &[ArgKind]) -> io::Result<Vec<Arg>> {
        ftrace::duration!("wayland", "Message::read_args", "len" => args.len() as u64);
        args.iter().map(|arg| self.read_arg(*arg)).collect()
    }

    /// Reads the set of arguments specified by the given &[ArgKind].
    pub fn write_args(&mut self, args: Vec<Arg>) -> io::Result<()> {
        ftrace::duration!("wayland", "Message::write_args", "len" => args.len() as u64);
        args.into_iter().try_for_each(|arg| self.write_arg(arg))?;
        Ok(())
    }

    /// Reads a |MessageHeader| without advancing the read pointer in the
    /// underlying buffer.
    pub fn peek_header(&mut self) -> io::Result<MessageHeader> {
        let pos = self.byte_buf.position();
        let header = self.read_header();
        self.byte_buf.set_position(pos);
        header
    }

    pub fn read_header(&mut self) -> io::Result<MessageHeader> {
        let sender = self.byte_buf.read_u32::<NativeEndian>()?;
        let word = self.byte_buf.read_u32::<NativeEndian>()?;
        Ok(MessageHeader { sender, length: (word >> 16) as u16, opcode: word as u16 })
    }

    pub fn write_header(&mut self, header: &MessageHeader) -> io::Result<()> {
        self.byte_buf.write_u32::<NativeEndian>(header.sender)?;
        self.byte_buf
            .write_u32::<NativeEndian>((header.length as u32) << 16 | header.opcode as u32)?;
        Ok(())
    }

    fn read_slice(&mut self, null_term: bool) -> io::Result<Vec<u8>> {
        let pos = self.byte_buf.position();
        let len = self.byte_buf.read_u32::<NativeEndian>()?;
        let mut vec: Vec<u8> = Vec::with_capacity(len as usize);
        if len == 0 {
            return Ok(vec);
        }

        vec.resize(len as usize, 0);
        self.byte_buf.read_exact(vec.as_mut_slice())?;

        if null_term {
            match vec.pop() {
                Some(term) => {
                    if term != b'\0' {
                        return Err(io::Error::new(
                            io::ErrorKind::InvalidData,
                            format!("Expected null terminator; found {}", term),
                        ));
                    }
                }
                None => {
                    return Err(io::Error::new(
                        io::ErrorKind::UnexpectedEof,
                        "Missing null terminator on string argument",
                    ));
                }
            }
        }

        let pad = compute_padding(self.byte_buf.position() - pos);
        if pad > 0 {
            self.byte_buf.read_uint::<NativeEndian>(pad)?;
        }
        assert!(self.byte_buf.position() % 4 == 0);
        Ok(vec)
    }

    fn write_slice(&mut self, s: &[u8], null_term: bool) -> io::Result<()> {
        let pos = self.byte_buf.position();
        let mut len: u32 = s.len() as u32;
        if null_term {
            len += 1;
        }

        self.byte_buf.write_u32::<NativeEndian>(len)?;
        self.byte_buf.write_all(s)?;
        if null_term {
            self.byte_buf.write_u8(0)?;
        }

        // Pad to 32-bit boundary.
        let pad = compute_padding(self.byte_buf.position() - pos);
        if pad > 0 {
            self.byte_buf.write_uint::<NativeEndian>(0, pad)?;
        }
        assert!(self.byte_buf.position() % 4 == 0);
        Ok(())
    }
}

/// Convert from the zx::MessageBuf we get out of the channel.
impl From<zx::MessageBuf> for Message {
    fn from(buf: zx::MessageBuf) -> Self {
        let (bytes, handles) = buf.split();
        Message::from_parts(bytes, handles)
    }
}

/// Wrapper around the array data provided by the wayland wire format.
///
/// On the wire, wl_array arguments are simple byte arrays. Protocol
/// documentation may specify a specific interpretation of the bytes, however
/// this is not directly modeled in the protocol XML.
///
/// For example, the wl_keyboard::enter event has an argument for the set of
/// keys pressed at the time of the event:
///
///   <arg name="keys" type="array" summary="the currently pressed keys"/>
///
/// While not explitly stated, |keys| is an array of |uint| arguments. These are
/// packed into the array using the same rules used to write consecutive |uint|
/// arguments to a Message.
// We use a |Message| internally here to reuse the same argument serialization
// logic used for full messages. The semantics of Array is similar to a message
// that cannot contain any handles.
#[derive(Debug)]
pub struct Array(Message);

impl Array {
    /// Creates a new, empty Array.
    pub fn new() -> Self {
        Self(Message::new())
    }

    /// Creates a new Array that contains the bytes stored in the provided Vec.
    pub fn from_vec(v: Vec<u8>) -> Self {
        Self(Message::from_parts(v, vec![]))
    }

    /// Access the array data as a slice.
    pub fn as_slice(&self) -> &[u8] {
        self.0.bytes()
    }

    /// Extracts the Array data as a Vec<u8>.
    pub fn into_vec(self) -> Vec<u8> {
        let (bytes, _) = self.0.take();
        bytes
    }

    /// Returns the length (in bytes) of the array.
    pub fn len(&self) -> usize {
        self.0.bytes().len()
    }

    /// Writes an Arg to the end of this Array, increasing its size.
    ///
    /// Note that arrays cannot contain handles and it is an error to attempt
    /// to write an `Arg::Handle` to an array.
    pub fn push<T: Into<Arg>>(&mut self, arg: T) -> io::Result<()> {
        let arg = arg.into();
        if let Arg::Handle(_) = &arg {
            Err(io::Error::new(io::ErrorKind::InvalidInput, "Arrays cannot contain handles"))
        } else {
            self.0.write_arg(arg)
        }
    }

    /// Reads an Arg out of this Array and return the value.
    ///
    /// Note that arrays cannot contain handles and it is an error to attempt
    /// to read an `ArgKind::Handle` from an array.
    pub fn read_arg(&mut self, kind: ArgKind) -> io::Result<Arg> {
        if kind == ArgKind::Handle {
            Err(io::Error::new(io::ErrorKind::InvalidInput, "Arrays cannot contain handles"))
        } else {
            self.0.read_arg(kind)
        }
    }
}

impl From<Vec<u8>> for Array {
    fn from(v: Vec<u8>) -> Self {
        Self::from_vec(v)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use anyhow::Error;

    /// Helper to assist asserting a single match branch.
    ///
    /// Ex:
    ///
    /// let arg = Arg::Uint(8);
    /// assert_matches(arg, Arg::Uint(x) => assert_eq!(x, 8));
    macro_rules! assert_matches(
        ($e:expr, $p:pat => $a:expr) => (
            match $e {
                $p => $a,
                _ => panic!("Failed to match!"),
            }
        )
    );

    const UINT_VALUE: u32 = 0x1234567;
    const INT_VALUE: i32 = -12345678;
    const FIXED_VALUE: i32 = 0x11223344;
    const OBJECT_VALUE: u32 = 0x88775566;
    const NEW_ID_VALUE: u32 = 0x55443322;
    const STRING_VALUE: &str = "Hello from a test";
    const ARRAY_VALUE: &[u8] = &[0, 1, 2, 3, 4, 5, 6];

    /// Emit one argument of each type and read it back out.
    #[test]
    fn sanity() {
        let (h1, _h2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

        let mut message = Message::new();
        assert!(message.write_arg(Arg::Uint(UINT_VALUE)).is_ok());
        assert!(message.write_arg(Arg::Int(INT_VALUE)).is_ok());
        assert!(message.write_arg(Arg::Fixed(Fixed::from_bits(FIXED_VALUE))).is_ok());
        assert!(message.write_arg(Arg::Object(OBJECT_VALUE)).is_ok());
        assert!(message.write_arg(Arg::NewId(NEW_ID_VALUE)).is_ok());
        assert!(message.write_arg(Arg::String(STRING_VALUE.to_owned())).is_ok());
        assert!(message.write_arg(Arg::Array(ARRAY_VALUE.to_owned().into())).is_ok());
        assert!(message.write_arg(Arg::Handle(h1.into())).is_ok());

        let (bytes, handles) = message.take();
        assert_eq!(1, handles.len());
        const INT_SIZE: u32 = 4;
        const UINT_SIZE: u32 = 4;
        const FIXED_SIZE: u32 = 4;
        const OBJECT_SIZE: u32 = 4;
        const NEW_ID_SIZE: u32 = 4;
        const STRING_SIZE: u32 = 24;
        const VEC_SIZE: u32 = 12;
        let expected_size =
            INT_SIZE + UINT_SIZE + FIXED_SIZE + OBJECT_SIZE + NEW_ID_SIZE + STRING_SIZE + VEC_SIZE;
        assert_eq!(expected_size as usize, bytes.len());

        let mut message = Message::from_parts(bytes, handles);
        let arg = message.read_arg(ArgKind::Uint).unwrap();
        assert_matches!(arg, Arg::Uint(x) => assert_eq!(x, UINT_VALUE));
        let arg = message.read_arg(ArgKind::Int).unwrap();
        assert_matches!(arg, Arg::Int(x) => assert_eq!(x, INT_VALUE));
        let arg = message.read_arg(ArgKind::Fixed).unwrap();
        assert_matches!(arg, Arg::Fixed(x) => assert_eq!(x, Fixed::from_bits(FIXED_VALUE)));
        let arg = message.read_arg(ArgKind::Object).unwrap();
        assert_matches!(arg, Arg::Object(x) => assert_eq!(x, OBJECT_VALUE));
        let arg = message.read_arg(ArgKind::NewId).unwrap();
        assert_matches!(arg, Arg::NewId(x) => assert_eq!(x, NEW_ID_VALUE));
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref x) => assert_eq!(x, STRING_VALUE));
        let arg = message.read_arg(ArgKind::Array).unwrap();
        assert_matches!(arg, Arg::Array(ref x) => assert_eq!(x.as_slice(), ARRAY_VALUE));
    }

    /// Test some of the padding edge-cases for string values.
    #[test]
    fn string_test() {
        let mut message = Message::new();

        // 4 bytes length + 1 null terminating byte + 3 pad bytes
        let s0 = "";
        message.clear();
        assert!(message.write_arg(Arg::String(s0.to_owned())).is_ok());
        assert_eq!(8, message.bytes().len());
        message.rewind();
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref s) => assert_eq!(s, s0));

        // 4 bytes length + 1 char + 1 null terminating byte + 2 pad bytes
        let s1 = "1";
        message.clear();
        assert!(message.write_arg(Arg::String(s1.to_owned())).is_ok());
        assert_eq!(8, message.bytes().len());
        message.rewind();
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref s) => assert_eq!(s, s1));

        // 4 bytes length + 2 char + 1 null terminating byte + 1 pad bytes
        let s2 = "22";
        message.clear();
        assert!(message.write_arg(Arg::String(s2.to_owned())).is_ok());
        assert_eq!(8, message.bytes().len());
        message.rewind();
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref s) => assert_eq!(s, s2));

        // 4 bytes length + 3 char + 1 null terminating byte + 0 pad bytes
        let s3 = "333";
        message.clear();
        assert!(message.write_arg(Arg::String(s3.to_owned())).is_ok());
        assert_eq!(8, message.bytes().len());
        message.rewind();
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref s) => assert_eq!(s, s3));

        // 4 bytes length + 4 char + 1 null terminating byte + 3 pad bytes
        let s4 = "4444";
        message.clear();
        assert!(message.write_arg(Arg::String(s4.to_owned())).is_ok());
        assert_eq!(12, message.bytes().len());
        message.rewind();
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref s) => assert_eq!(s, s4));

        // 4 bytes length + 5 char + 1 null terminating byte + 2 pad bytes
        let s5 = "55555";
        message.clear();
        assert!(message.write_arg(Arg::String(s5.to_owned())).is_ok());
        assert_eq!(12, message.bytes().len());
        message.rewind();
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref s) => assert_eq!(s, s5));

        // Null string (no \0 termiator). This is distinct from the empty
        // string (which is encoded as a single null terminator character).
        // Note that once decoded, a null and an empty string will have the
        // same representation, which is a Rust 'String' with length 0, but
        // this test ensures that we decode the null string correctly.
        //
        // 4 bytes length
        message.clear();
        assert!(message.write_arg(Arg::Uint(0)).is_ok());
        assert_eq!(4, message.bytes().len());
        message.rewind();
        let arg = message.read_arg(ArgKind::String).unwrap();
        assert_matches!(arg, Arg::String(ref s) => assert_eq!(s, ""));
    }

    #[test]
    fn peek_header() -> Result<(), Error> {
        // Write just a message header.
        let header = MessageHeader { sender: 3, opcode: 2, length: 8 };
        let mut message = Message::new();
        message.write_header(&header)?;
        message.rewind();

        // Verify peek doesn't advance the read pointer.
        assert_eq!(header, message.peek_header()?);
        assert_eq!(header, message.peek_header()?);
        assert_eq!(header, message.peek_header()?);
        assert_eq!(header, message.peek_header()?);
        Ok(())
    }

    #[test]
    fn empty_message() {
        let (h1, h2) = zx::Channel::create().unwrap();

        let message = Message::new();
        assert!(message.is_empty());
        let message = Message::from_parts(vec![], vec![]);
        assert!(message.is_empty());
        let message = Message::from_parts(vec![1], vec![]);
        assert!(!message.is_empty());
        let message = Message::from_parts(vec![], vec![h1.into()]);
        assert!(!message.is_empty());

        let mut message = Message::from_parts(vec![0, 0, 0, 0], vec![h2.into()]);
        assert!(!message.is_empty());
        let _ = message.read_arg(ArgKind::Uint).unwrap();
        assert!(!message.is_empty());
        let _ = message.read_arg(ArgKind::Handle).unwrap();
        assert!(message.is_empty());
    }

    #[test]
    fn array_read_write() -> Result<(), Error> {
        let mut message = Message::new();
        let mut array = Array::new();
        array.push(3)?;
        array.push(-2)?;
        array.push(Fixed::from_float(-2.0))?;
        message.write_arg(array.into())?;
        message.rewind();

        let mut array = message.read_arg(ArgKind::Array)?.as_array()?;
        assert_eq!(3, array.read_arg(ArgKind::Uint)?.as_uint()?);
        assert_eq!(-2, array.read_arg(ArgKind::Int)?.as_int()?);
        assert_eq!(Fixed::from_float(-2.), array.read_arg(ArgKind::Fixed)?.as_fixed()?);

        Ok(())
    }
}
