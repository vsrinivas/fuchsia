// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::{Error, ExtendBufferTooShortError, Result};
use std::io::Write;

/// A serialization trait for objects which we convert to and from a wire format.
pub trait ProtocolMessage: Sized {
    /// The minimum size an object of this type can have when serialized via `write_bytes`.
    const MIN_SIZE: usize;
    /// Encode this value into the given buffer as a stream of bytes.
    fn write_bytes<W: Write>(&self, out: &mut W) -> Result<usize>;
    /// Returns the size of the data that `write_bytes` will write for this value. Useful for buffer
    /// allocation.
    fn byte_size(&self) -> usize;
    /// Try to read a serialized form of this value from the given buffer. On success returns both
    /// the value and how many bytes were consumed. If we return `Error::BufferTooShort`, we may
    /// have only part of the value and can try again with an extension of the same data.
    fn try_from_bytes(bytes: &[u8]) -> Result<(Self, usize)>;
}

/// We often encode strings on the wire as a 1-byte (u8) length followed by a stream of UTF-8
/// characters. This has the restriction, of course, that the length of the string must fit in one
/// byte.
///
/// EncodableString is a wrapper for strings that is only constructible if the string is short
/// enough to be encoded in this way. It's otherwise mostly transparent, but handling strings
/// through this type saves us a lot of error handling (or worse, unwraps!)
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct EncodableString(String);

impl ProtocolMessage for EncodableString {
    const MIN_SIZE: usize = 1;
    fn write_bytes<W: Write>(&self, out: &mut W) -> Result<usize> {
        let len: u8 =
            self.0.as_bytes().len().try_into().expect("EncodableString wasn't encodable!");
        out.write_all(&[len])?;
        out.write_all(self.0.as_bytes())?;
        Ok(usize::from(len) + 1)
    }

    fn byte_size(&self) -> usize {
        self.0.as_bytes().len() + 1
    }

    fn try_from_bytes(bytes: &[u8]) -> Result<(Self, usize)> {
        if bytes.is_empty() {
            Err(Error::BufferTooShort(1))
        } else if bytes.len() - 1 < bytes[0] as usize {
            Err(Error::BufferTooShort(bytes[0] as usize + 1))
        } else {
            let len = bytes[0] as usize;
            let bytes = &bytes[1..][..len];
            Ok((
                std::str::from_utf8(bytes)
                    .map_err(|_| Error::BadUTF8(String::from_utf8_lossy(bytes).to_string()))?
                    .to_owned()
                    .try_into()
                    .expect("String wasn't decodable right after encoding!"),
                len + 1,
            ))
        }
    }
}

impl TryFrom<String> for EncodableString {
    type Error = Error;
    fn try_from(src: String) -> Result<EncodableString> {
        let _: u8 =
            src.as_bytes().len().try_into().map_err(|_| Error::StringTooBig(src.clone()))?;
        Ok(EncodableString(src))
    }
}

impl std::ops::Deref for EncodableString {
    type Target = String;
    fn deref(&self) -> &String {
        &self.0
    }
}

impl<T> PartialEq<T> for EncodableString
where
    String: PartialEq<T>,
{
    fn eq(&self, other: &T) -> bool {
        PartialEq::eq(&self.0, other)
    }
}

impl std::fmt::Display for EncodableString {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Display::fmt(&self.0, f)
    }
}

/// The initial packet that goes out on the main control stream when a circuit node first connects
/// to another. Contains basic version info, an implementation-usable string indicating what
/// protocol will be run atop the circuit network.
#[derive(Debug)]
pub struct Identify {
    pub circuit_version: u8,
    pub protocol: EncodableString,
}

impl Identify {
    /// Construct a new Identify header.
    pub fn new(protocol: EncodableString) -> Self {
        Identify { circuit_version: crate::CIRCUIT_VERSION, protocol }
    }
}

impl ProtocolMessage for Identify {
    const MIN_SIZE: usize = 1 + EncodableString::MIN_SIZE;
    fn byte_size(&self) -> usize {
        self.protocol.byte_size() + 1
    }

    fn write_bytes<W: Write>(&self, out: &mut W) -> Result<usize> {
        let mut bytes = 0;
        out.write_all(&[self.circuit_version])?;
        bytes += 1;
        bytes += self.protocol.write_bytes(out)?;
        Ok(bytes)
    }

    fn try_from_bytes(bytes: &[u8]) -> Result<(Self, usize)> {
        if bytes.len() < 2 {
            return Err(Error::BufferTooShort(2));
        }

        let circuit_version = bytes[0];
        let (protocol, proto_len) =
            EncodableString::try_from_bytes(&bytes[1..]).extend_buffer_too_short(1)?;

        Ok((Identify { circuit_version, protocol }, 1 + proto_len))
    }
}

/// Information about the quality of a link. A lower value for the contained u8 is better, with 0
/// usually meaning a node linked to itself with no intermediate connection. The u8 value should
/// never be 255 as this has a reserved meaning when we encode.
#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct Quality(u8);

impl Quality {
    /// Quality of connecting from a node to itself in a loop.
    pub const SELF: Quality = Quality(0);
    /// Quality of connecting two nodes in the same process directly.
    pub const IN_PROCESS: Quality = Quality(1);
    /// Quality of connecting two nodes over a local IPC mechanism.
    pub const LOCAL_SOCKET: Quality = Quality(2);
    /// Quality of connecting two nodes via a USB link.
    pub const USB: Quality = Quality(5);
    /// Quality of connecting two nodes over the network.
    pub const NETWORK: Quality = Quality(20);
    /// Worst quality value.
    pub const WORST: Quality = Quality(u8::MAX - 1);

    /// Add two quality values together. If we are routing a stream across two links, we can add the
    /// quality of those links to get the quality of the combined link the stream is on.
    pub fn combine(self, other: Self) -> Self {
        Quality(std::cmp::min(self.0.saturating_add(other.0), u8::MAX - 1))
    }
}

impl TryFrom<u8> for Quality {
    type Error = ();
    fn try_from(value: u8) -> Result<Self, Self::Error> {
        if value != u8::MAX {
            Ok(Quality(value))
        } else {
            Err(())
        }
    }
}

/// Information about the state of a node. We transmit this information from node to node so that
/// each node knows what peers are available to establish circuits with.
#[derive(Debug)]
pub enum NodeState {
    /// Node is online.
    Online(EncodableString, Quality),
    /// Node is offline.
    Offline(EncodableString),
}

impl NodeState {
    /// Same as `write_bytes` but specifically takes a vector, and thus cannot return an error.
    pub fn write_bytes_vec(&self, out: &mut Vec<u8>) -> usize {
        self.write_bytes(out).expect("Write to vector should't fail but did!")
    }
}

impl ProtocolMessage for NodeState {
    const MIN_SIZE: usize = 1 + EncodableString::MIN_SIZE;
    fn byte_size(&self) -> usize {
        let s = match self {
            NodeState::Online(s, _) => s,
            NodeState::Offline(s) => s,
        };

        s.byte_size() + 1
    }

    fn write_bytes<W: Write>(&self, out: &mut W) -> Result<usize> {
        let (st, speed) = match self {
            NodeState::Online(s, quality) => {
                debug_assert!(quality.0 != u8::MAX);
                (s, quality.0)
            }
            NodeState::Offline(s) => (s, u8::MAX),
        };
        let mut bytes = 0;
        out.write_all(&[speed])?;
        bytes += 1;
        bytes += st.write_bytes(out)?;
        Ok(bytes)
    }

    fn try_from_bytes(bytes: &[u8]) -> Result<(Self, usize)> {
        if bytes.len() < 2 {
            return Err(Error::BufferTooShort(2));
        }

        let quality = bytes[0];
        let (node, node_len) =
            EncodableString::try_from_bytes(&bytes[1..]).extend_buffer_too_short(1)?;

        let state = if let Ok(quality) = quality.try_into() {
            NodeState::Online(node, quality)
        } else {
            NodeState::Offline(node)
        };

        Ok((state, 1 + node_len))
    }
}
