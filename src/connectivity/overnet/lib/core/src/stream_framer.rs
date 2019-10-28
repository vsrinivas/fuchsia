// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles framing/deframing of stream links

use {byteorder::WriteBytesExt, crc::crc32, failure::Error, std::collections::VecDeque};

/// Manages framing of messages into a byte stream.
#[derive(Debug)]
pub struct StreamFramer {
    outgoing: Vec<u8>,
}

impl StreamFramer {
    /// Create a new (empty) framer
    pub fn new() -> Self {
        Self { outgoing: Vec::new() }
    }

    /// Queue a send to the remote end.
    pub fn queue_send(&mut self, bytes: &[u8]) -> Result<(), Error> {
        if bytes.len() == 0 {
            return Ok(());
        }
        if bytes.len() > (std::u16::MAX as usize) + 1 {
            failure::bail!("Packet length ({}) too long for stream framing", bytes.len());
        }
        self.outgoing.write_u8(0u8)?; // '\0'
        self.outgoing.write_u16::<byteorder::LittleEndian>((bytes.len() - 1) as u16)?;
        self.outgoing.write_u32::<byteorder::LittleEndian>(crc32::checksum_ieee(bytes))?;
        self.outgoing.extend_from_slice(bytes);
        self.outgoing.write_u8(10u8)?; // '\n'
        Ok(())
    }

    /// Take (framed) sends as a byte stream
    pub fn take_sends<'a>(&'a mut self) -> Vec<u8> {
        std::mem::replace(&mut self.outgoing, vec![])
    }
}

/// Deframe a set of bytes
pub struct StreamDeframer {
    incoming: Vec<u8>,
    deframed: VecDeque<Vec<u8>>,
}

impl StreamDeframer {
    /// Create a new deframer
    pub fn new() -> Self {
        Self { incoming: Vec::new(), deframed: VecDeque::new() }
    }

    /// Queue some framed bytes from a byte stream to be deframed
    pub fn queue_recv(&mut self, bytes: &[u8]) {
        self.incoming.extend_from_slice(bytes);
        self.deframe_some();
    }

    fn deframe_some(&mut self) {
        let mut buf = self.incoming.as_slice();
        while buf.len() > 8 {
            if buf[0] != 0u8 {
                // Not a start marker: remove and continue
                buf = &buf[1..];
                continue;
            }
            let len = 1 + (u16::from_le_bytes([buf[1], buf[2]]) as usize);
            let crc = u32::from_le_bytes([buf[3], buf[4], buf[5], buf[6]]);
            if buf.len() < 8 + len {
                // Not enough bytes to deframe: done for now
                break;
            }
            if buf[7 + len] != 10u8 {
                // Does not end with an end marker: remove start byte and continue
                buf = &buf[1..];
                continue;
            }
            let frame = &buf[7..7 + len];
            let crc_actual = crc32::checksum_ieee(frame);
            if crc != crc_actual {
                // CRC mismatch: skip start marker and continue
                buf = &buf[1..];
                continue;
            }
            // Successfully got a frame! Save it, and continue
            self.deframed.push_back(frame.to_vec());
            buf = &buf[8 + len..];
        }
        if buf.len() != self.incoming.len() {
            self.incoming = buf.to_vec();
        }
    }

    /// Return the next deframed frame, or None if no frame was ready
    pub fn next_incoming_frame(&mut self) -> Option<Vec<u8>> {
        self.deframed.pop_front()
    }

    /// Returns true if there are incoming bytes that have not yet been deframed
    pub fn is_stuck(&mut self) -> bool {
        self.incoming.len() > 0
    }

    /// Skip one byte and continue trying to deframe
    pub fn skip_byte(&mut self) {
        self.incoming = self.incoming[1..].to_vec();
        self.deframe_some();
    }
}

#[cfg(test)]
mod test {

    use super::*;

    fn join(mut a: Vec<u8>, mut b: Vec<u8>) -> Vec<u8> {
        a.append(&mut b);
        a
    }

    #[test]
    fn simple_frame() {
        let mut framer = StreamFramer::new();
        framer.queue_send(&[1, 2, 3, 4]).unwrap();
        let mut deframer = StreamDeframer::new();
        deframer.queue_recv(framer.take_sends().as_slice());
        assert_eq!(deframer.next_incoming_frame(), Some(vec![1, 2, 3, 4]));
        framer.queue_send(&[5, 6, 7, 8]).unwrap();
        deframer.queue_recv(framer.take_sends().as_slice());
        assert_eq!(deframer.next_incoming_frame(), Some(vec![5, 6, 7, 8]));
    }

    #[test]
    fn large_frame() {
        let big_slice = [0u8; 100000];
        assert!(StreamFramer::new().queue_send(&big_slice).is_err());
    }

    #[test]
    fn skip_junk_start_0() {
        let mut framer = StreamFramer::new();
        framer.queue_send(&[1, 2, 3, 4]).unwrap();
        let mut deframer = StreamDeframer::new();
        deframer.queue_recv(join(vec![0], framer.take_sends()).as_slice());
        assert_eq!(deframer.next_incoming_frame(), None);
        assert!(deframer.is_stuck());
        deframer.skip_byte();
        assert_eq!(deframer.next_incoming_frame(), Some(vec![1, 2, 3, 4]));
        assert!(!deframer.is_stuck());
    }

    #[test]
    fn skip_junk_start_1() {
        let mut framer = StreamFramer::new();
        framer.queue_send(&[1, 2, 3, 4]).unwrap();
        let mut deframer = StreamDeframer::new();
        deframer.queue_recv(join(vec![1], framer.take_sends()).as_slice());
        assert_eq!(deframer.next_incoming_frame(), Some(vec![1, 2, 3, 4]));
    }
}
