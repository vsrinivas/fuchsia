// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles framing/deframing of stream links

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]

use anyhow::Error;

/// Describes a framing format.
pub trait Format: Send + Sync + 'static {
    /// Write a frame of `frame_type` with payload `bytes` into `outgoing`.
    fn frame(&self, bytes: &[u8], outgoing: &mut Vec<u8>) -> Result<(), Error>;

    /// Parse `bytes`.
    /// If the bytes could never lead to a successfully parsed frame ever again, return Err(_).
    /// Otherwise, return Ok(_).
    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error>;
}

// We occasionally want to make a dynamic selection of framing type, and in those cases it's
// often convenient to deal with a Box<dyn Format> instead of a custom thing. Provide this impl
// so that a Box<dyn Format> is a Format too.
impl Format for Box<dyn Format> {
    fn frame(&self, bytes: &[u8], outgoing: &mut Vec<u8>) -> Result<(), Error> {
        self.as_ref().frame(bytes, outgoing)
    }

    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error> {
        self.as_ref().deframe(bytes)
    }
}

/// Success result of [`Format::deframe`].
pub struct Deframed {
    /// The new beginning of the parsing buffer, as an offset from the beginning of the buffer.
    pub new_start_pos: usize,
    /// How many bytes (measured from the start of the buffer) should be reported as unframed
    /// data. It's required that `unframed_bytes` <= `new_start_pos`.
    pub unframed_bytes: usize,
    /// Optional parsed frame from the buffer.
    pub frame: Option<Vec<u8>>,
}

/// Manages framing of messages into a byte stream.
pub struct Framer<Fmt: Format> {
    fmt: Fmt,
}

impl<Fmt: Format> Framer<Fmt> {
    /// Construct a new framer for some format.
    pub fn new(fmt: Fmt) -> Self {
        Self { fmt }
    }

    /// Write a frame into the framer.
    pub fn write_frame(&self, bytes: &[u8]) -> Result<Vec<u8>, Error> {
        let mut buf = vec![];
        self.fmt.frame(bytes, &mut buf)?;
        Ok(buf)
    }
}

struct BVec(Vec<u8>);

impl std::fmt::Debug for BVec {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.0.len() <= 4 {
            self.0.fmt(f)
        } else {
            write!(f, "{:?}+{}b", &self.0[..4], self.0.len() - 4)
        }
    }
}

#[derive(Debug)]
enum DeframerState {
    Parse,
    Unframe { unframed: BVec },
    UnframeThenFrame { unframed: BVec, framed: BVec },
    Frame { framed: BVec },
    Done,
}

#[derive(Debug, PartialEq)]
/// Bytes ready by a Deframer.
pub enum ReadBytes {
    /// A frame to be processed.
    Framed(Vec<u8>),
    /// Garbage skipped between frames.
    Unframed(Vec<u8>),
}

/// Manages deframing of messages from a byte stream.
pub struct Deframer<Fmt: Format> {
    fmt: Fmt,
    eof: bool,
    unparsed: Vec<u8>,
    state: DeframerState,
}

impl<Fmt: Format> Deframer<Fmt> {
    /// Construct a new deframer.
    pub fn new(fmt: Fmt) -> Self {
        Self { fmt, eof: false, unparsed: vec![], state: DeframerState::Parse }
    }

    pub fn has_pending_bytes(&self) -> bool {
        !self.unparsed.is_empty()
    }

    /// Skip the first byte in the parse buffer if it's unlikely to form a correct parse.
    pub fn skip_byte_and_parse_frames<'a>(&'a mut self) -> DeframerIter<'a, Fmt> {
        if self.unparsed.is_empty() {
            DeframerIter { deframer: self, unframed: None }
        } else {
            let mut unframed = self.unparsed.split_off(1);
            std::mem::swap(&mut unframed, &mut self.unparsed);
            DeframerIter { deframer: self, unframed: Some(unframed) }
        }
    }

    /// Read frames from the deframer.
    pub fn parse_frames<'a>(&'a mut self, bytes: &[u8]) -> DeframerIter<'a, Fmt> {
        if bytes.is_empty() {
            self.eof = true;
        } else {
            self.unparsed.extend_from_slice(bytes);
        }

        DeframerIter { deframer: self, unframed: None }
    }

    fn parse_frame(&mut self) -> Option<Result<ReadBytes, Error>> {
        loop {
            match std::mem::replace(&mut self.state, DeframerState::Done) {
                DeframerState::Parse => {
                    // If we hit the end of the stream, stay in the `Done` state and exit. If we had
                    // any unparsed data, return it as unframed.
                    if self.eof {
                        if self.unparsed.is_empty() {
                            return None;
                        } else {
                            let unparsed = std::mem::replace(&mut self.unparsed, vec![]);
                            return Some(Ok(ReadBytes::Unframed(unparsed)));
                        }
                    }

                    // Otherwise, try to parse a frame out of our buffer.
                    match self.deframe_step() {
                        Ok(state) => {
                            self.state = state;

                            // Exit parsing if we didn't parse out a frame.
                            if matches!(self.state, DeframerState::Parse { .. }) {
                                return None;
                            }
                        }
                        Err(err) => {
                            self.state = DeframerState::Done;
                            return Some(Err(err));
                        }
                    }
                }
                DeframerState::Unframe { unframed } => {
                    self.state = DeframerState::Parse;

                    return Some(Ok(ReadBytes::Unframed(unframed.0)));
                }
                DeframerState::UnframeThenFrame { unframed, framed } => {
                    self.state = DeframerState::Frame { framed };

                    return Some(Ok(ReadBytes::Unframed(unframed.0)));
                }
                DeframerState::Frame { framed } => {
                    self.state = DeframerState::Parse;

                    return Some(Ok(ReadBytes::Framed(framed.0)));
                }
                DeframerState::Done => {
                    return None;
                }
            }
        }
    }

    /// A single attempt to read a frame.
    fn deframe_step(&mut self) -> Result<DeframerState, Error> {
        // Try to parse a frame.
        let Deframed { frame, unframed_bytes, new_start_pos } = self.fmt.deframe(&self.unparsed)?;
        assert!(unframed_bytes <= new_start_pos);

        // Consume any framed and unframed bytes from the reader.
        let mut unframed = None;
        if unframed_bytes != 0 {
            let mut unframed_vec = self.unparsed.split_off(unframed_bytes);
            std::mem::swap(&mut unframed_vec, &mut self.unparsed);
            unframed = Some(BVec(unframed_vec));
        }

        // TODO: Why do we need to do this?
        if new_start_pos != unframed_bytes {
            self.unparsed.drain(..(new_start_pos - unframed_bytes));
        }

        // Transition to the `Unframe` or `UnframeThenFrame` state if we parsed any unframed data.
        if let Some(unframed) = unframed {
            if let Some(frame) = frame {
                return Ok(DeframerState::UnframeThenFrame { unframed, framed: BVec(frame) });
            } else {
                return Ok(DeframerState::Unframe { unframed });
            }
        }

        // Transition to the `Frame` state if we parsed any framed data.
        if let Some(framed) = frame {
            return Ok(DeframerState::Frame { framed: BVec(framed) });
        }

        // Otherwise transition back to the `Read` state since we'll need more data to determine if the next
        // chunk is a frame or not.
        Ok(DeframerState::Parse)
    }
}

/// TODO
pub struct DeframerIter<'a, Fmt: Format> {
    deframer: &'a mut Deframer<Fmt>,
    unframed: Option<Vec<u8>>,
}

impl<'a, Fmt: Format> Iterator for DeframerIter<'a, Fmt> {
    type Item = Result<ReadBytes, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(skipped_bytes) = self.unframed.take() {
            Some(Ok(ReadBytes::Unframed(skipped_bytes)))
        } else {
            self.deframer.parse_frame()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use byteorder::WriteBytesExt;
    use crc::crc32;

    /// Framing format that assumes an underlying transport that *MAY* lose/duplicate/corrupt some bytes
    /// but usually transports the full 8 bits in a byte (e.g. many serial transports).
    pub struct LossyBinary;

    impl Format for LossyBinary {
        fn frame(&self, bytes: &[u8], outgoing: &mut Vec<u8>) -> Result<(), Error> {
            if bytes.len() > (std::u16::MAX as usize) + 1 {
                return Err(anyhow::format_err!(
                    "Packet length ({}) too long for stream framing",
                    bytes.len()
                ));
            }
            outgoing.reserve(2 + 4 + bytes.len() + 1);
            outgoing.write_u16::<byteorder::LittleEndian>((bytes.len() - 1) as u16)?;
            outgoing.write_u32::<byteorder::LittleEndian>(crc32::checksum_ieee(bytes))?;
            outgoing.extend_from_slice(bytes);
            outgoing.write_u8(10u8)?; // '\n'
            Ok(())
        }

        fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error> {
            let mut start = 0;
            loop {
                let buf = &bytes[start..];
                if buf.len() <= 7 {
                    return Ok(Deframed {
                        frame: None,
                        unframed_bytes: start,
                        new_start_pos: start,
                    });
                }
                let len = 1 + (u16::from_le_bytes([buf[0], buf[1]]) as usize);
                let crc = u32::from_le_bytes([buf[2], buf[3], buf[4], buf[5]]);
                if buf.len() < 7 + len {
                    // Not enough bytes to deframe: done for now
                    return Ok(Deframed {
                        frame: None,
                        unframed_bytes: start,
                        new_start_pos: start,
                    });
                }
                if buf[6 + len] != 10u8 {
                    // Does not end with an end marker: remove start byte and continue
                    start += 1;
                    continue;
                }
                let frame = &buf[6..6 + len];
                let crc_actual = crc32::checksum_ieee(frame);
                if crc != crc_actual {
                    // CRC mismatch: skip start marker and continue
                    start += 1;
                    continue;
                }
                // Successfully got a frame! Save it, and continue
                return Ok(Deframed {
                    frame: Some(frame.to_vec()),
                    unframed_bytes: start,
                    new_start_pos: start + 7 + len,
                });
            }
        }
    }

    fn join(mut a: Vec<u8>, mut b: Vec<u8>) -> Vec<u8> {
        a.append(&mut b);
        a
    }

    #[test]
    fn simple_frame_lossy_binary() {
        let framer = Framer::new(LossyBinary);
        let msg = framer.write_frame(&[1, 2, 3, 4]).unwrap();

        let mut deframer = Deframer::new(LossyBinary);
        let frames = deframer.parse_frames(&msg).map(|frame| frame.unwrap()).collect::<Vec<_>>();
        assert_eq!(frames, vec![ReadBytes::Framed(vec![1, 2, 3, 4])]);

        let msg = framer.write_frame(&[5, 6, 7, 8]).unwrap();
        let frames = deframer.parse_frames(&msg).map(|frame| frame.unwrap()).collect::<Vec<_>>();
        assert_eq!(frames, vec![ReadBytes::Framed(vec![5, 6, 7, 8])]);
    }

    #[test]
    fn lossy_binary_skip_junk_start_0() {
        let framer = Framer::new(LossyBinary);
        let body = vec![1, 2, 3, 4];
        let msg = framer.write_frame(&body).unwrap();

        let mut deframer = Deframer::new(LossyBinary);

        // Add junk at the front. This should return nothing, since the format thinks the frame
        // could be ~700 bytes long.
        assert_eq!(
            deframer
                .parse_frames(&join(vec![0], msg))
                .map(|frame| frame.unwrap())
                .collect::<Vec<_>>(),
            vec![]
        );

        // Skip the first byte, and try again. This should successfully parse.
        assert_eq!(
            deframer.skip_byte_and_parse_frames().map(|frame| frame.unwrap()).collect::<Vec<_>>(),
            vec![ReadBytes::Unframed(vec![0]), ReadBytes::Framed(body)]
        );
    }

    #[test]
    fn lossy_binary_skip_junk_start_1() {
        let framer = Framer::new(LossyBinary);
        let body = vec![1, 2, 3, 4];
        let msg = framer.write_frame(&body).unwrap();

        let mut deframer = Deframer::new(LossyBinary);

        // Add junk at the front. This should return nothing, since the format thinks the frame
        // could be ~700 bytes long.
        assert_eq!(
            deframer
                .parse_frames(&join(vec![1], msg))
                .map(|frame| frame.unwrap())
                .collect::<Vec<_>>(),
            vec![]
        );

        // Skip the first byte, and try again. This should successfully parse.
        assert_eq!(
            deframer.skip_byte_and_parse_frames().map(|frame| frame.unwrap()).collect::<Vec<_>>(),
            vec![ReadBytes::Unframed(vec![1]), ReadBytes::Framed(body)]
        );
    }
}
