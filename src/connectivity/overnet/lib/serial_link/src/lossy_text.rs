// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use byteorder::WriteBytesExt;
use crc::crc32;
use std::convert::TryInto;
use std::time::Duration;
use stream_framer::{Deframed, Format};

/// Framing format that assumes a lossy byte stream that doesn't support more than 7-bit text and is
/// hostile towards control characters.
pub struct LossyText {
    duration_per_byte: Duration,
}

impl LossyText {
    /// Create a new LossyBinary format instance with some timeout waiting for bytes (if this is
    /// exceeded a byte will be skipped in the input).
    pub fn new(duration_per_byte: Duration) -> Self {
        Self { duration_per_byte }
    }
}

impl Format for LossyText {
    fn frame(&self, bytes: &[u8], outgoing: &mut Vec<u8>) -> Result<(), Error> {
        outgoing.write_u8(b'*')?;
        let start_len = outgoing.len();
        // reserve space for length
        outgoing.extend_from_slice(&[0u8, 0u8]);
        let start_body = outgoing.len();
        let mut enc = Vec::new();
        enc.write_u32::<byteorder::LittleEndian>(crc32::checksum_ieee(bytes))?;
        enc.extend_from_slice(bytes);
        outgoing.extend_from_slice(base64::encode(&enc).as_bytes());
        let enc_len = outgoing.len() - start_body - 1;
        outgoing.write_u8(b'\n')?;
        let max_len = 95 * 95;
        if enc_len > max_len {
            return Err(format_err!(
                "Encoded length ({}) is longer than max ({})",
                enc_len,
                max_len
            ));
        }
        let len_prefix = &mut outgoing[start_len..start_body];
        len_prefix[0] = (enc_len / 95 + 32) as u8;
        len_prefix[1] = (enc_len % 95 + 32) as u8;
        Ok(())
    }

    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error> {
        let mut start = 0;
        loop {
            let buf = &bytes[start..];
            tracing::trace!("buf = {:?} [start={}]", buf, start);
            if buf.len() == 0 {
                return Ok(Deframed { frame: None, unframed_bytes: start, new_start_pos: start });
            }
            if buf[0] != b'*' {
                // Not a start marker: remove and continue
                tracing::trace!("skip non-start marker {:?}", buf[0]);
                start += 1;
                continue;
            }
            if buf.len() <= 1 {
                return Ok(Deframed { frame: None, unframed_bytes: start, new_start_pos: start });
            }
            if buf[1] < 32 || buf[1] >= 127 {
                // Not a start marker: remove and continue
                tracing::trace!("skip non-length byte {:?}", buf[2]);
                start += 2;
                continue;
            }
            if buf.len() <= 3 {
                return Ok(Deframed { frame: None, unframed_bytes: start, new_start_pos: start });
            }
            if buf[2] < 32 || buf[2] >= 127 {
                // Not a start marker: remove and continue
                tracing::trace!("skip non-length2 byte {:?}", buf[3]);
                start += 3;
                continue;
            }
            let len = 1usize + ((buf[1] - 32) as usize * 95) + (buf[2] - 32) as usize;
            tracing::trace!("len={:?} have:{:?}", len, buf.len());
            if buf.len() < 3 + len + 1 {
                tracing::trace!("insufficient bytes; start:{}", start);
                // Not enough bytes to deframe: done for now
                return Ok(Deframed { frame: None, unframed_bytes: start, new_start_pos: start });
            }
            tracing::trace!("Tail: {:?}", buf[3 + len]);
            let tail_bytes = match buf[3 + len] {
                b'\r' => {
                    // We allow our '\n' to be replaced with a '\r\n'.
                    if buf.len() < 3 + len + 2 {
                        tracing::trace!("insufficient bytes; start:{}", start);
                        // Not enough bytes to deframe: done for now
                        return Ok(Deframed {
                            frame: None,
                            unframed_bytes: start,
                            new_start_pos: start,
                        });
                    }
                    if buf[3 + len + 1] != b'\n' {
                        // Does not end with an end marker: remove start bytes and continue
                        tracing::trace!(
                            "skip no end marker after \\r {:?}; len={}",
                            buf[3 + len + 1],
                            len
                        );
                        start += 1;
                        continue;
                    }
                    2
                }
                b'\n' => 1,
                _ => {
                    // Does not end with an end marker: remove start bytes and continue
                    tracing::trace!(
                        "skip no end marker {:?}; len={}; buf={:?}",
                        buf[3 + len],
                        len,
                        buf
                    );
                    start += 1;
                    continue;
                }
            };
            let enc_frame = &buf[3..3 + len];
            tracing::trace!("enc_frame={:?}", enc_frame);
            let dec = match base64::decode(enc_frame) {
                Ok(dec) => dec,
                Err(_) => {
                    // base64 decoding failed: skip start marker and continue
                    start += 1;
                    continue;
                }
            };
            let crc = u32::from_le_bytes(dec[..4].try_into()?);
            let frame = &dec[4..];
            tracing::trace!("got: {:?}", frame);
            let crc_actual = crc32::checksum_ieee(&frame);
            tracing::trace!("crc: expect:{:?} got:{:?}", crc, crc_actual);
            if crc != crc_actual {
                tracing::trace!("skip crc mismatch {} vs {}", crc, crc_actual);
                // CRC mismatch: skip start marker and continue
                start += 1;
                continue;
            }
            return Ok(Deframed {
                frame: Some(frame.to_vec()),
                unframed_bytes: start,
                new_start_pos: start + 3 + len + tail_bytes,
            });
        }
    }

    fn deframe_timeout(&self, have_pending_bytes: bool) -> Option<Duration> {
        if have_pending_bytes {
            Some(self.duration_per_byte)
        } else {
            None
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures::prelude::*;
    use stream_framer::{new_deframer, new_framer, ReadBytes};

    #[fuchsia::test]
    async fn simple_frame_lossy_text() {
        // Try to encode and decode a packet of a variety of different packet sizes.
        // 280 is chosen arbitrarily - it's a short runtime, but hits all of the edge cases we'd
        // be worried about.
        futures::stream::iter(1..280)
            .for_each_concurrent(None, |n| async move {
                let test: Vec<_> = std::iter::repeat(b'a').take(n).collect();
                let (mut framer_writer, mut framer_reader) =
                    new_framer(LossyText::new(Duration::from_millis(100)), 1024);
                framer_writer.write(&test).await.unwrap();
                let (mut deframer_writer, mut deframer_reader) =
                    new_deframer(LossyText::new(Duration::from_millis(100)), 1024);
                let encoded = framer_reader.read().await.unwrap();
                println!("encoded = {:?}", std::str::from_utf8(&encoded).unwrap());
                deframer_writer.write(&encoded).await.unwrap();
                assert_eq!(deframer_reader.read().await.unwrap(), ReadBytes::Framed(test));
                let test: Vec<_> = std::iter::repeat(b'b').take(n).collect();
                framer_writer.write(&test).await.unwrap();
                let encoded = framer_reader.read().await.unwrap();
                println!("encoded = {:?}", std::str::from_utf8(&encoded).unwrap());
                deframer_writer.write(&encoded).await.unwrap();
                assert_eq!(deframer_reader.read().await.unwrap(), ReadBytes::Framed(test));
            })
            .await
    }
}
