// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles framing/deframing of stream links

use crate::runtime::wait_until;
use anyhow::{format_err, Error};
use byteorder::WriteBytesExt;
use crc::crc32;
use futures::future::poll_fn;
use futures::prelude::*;
use std::cell::Cell;
use std::future::Future;
use std::pin::Pin;
use std::rc::Rc;
use std::task::{Context, Poll, Waker};
use std::time::{Duration, Instant};

use crate::router::generate_node_id;

/// Describes a framing format.
pub trait Format {
    /// Write a frame of `frame_type` with payload `bytes` into `outgoing`.
    fn frame(
        &self,
        frame_type: FrameType,
        bytes: &[u8],
        outgoing: &mut Vec<u8>,
    ) -> Result<(), Error>;

    /// Parse `bytes`.
    /// If the bytes could never lead to a successfully parsed frame ever again, return Err(_).
    /// Otherwise, return Ok(_).
    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error>;

    /// Return a time at which the first byte of the buffered bytes should be dropped (as it's
    /// unlikely they'll form a correct parse).
    fn deframe_timeout(&self, have_pending_bytes: bool) -> Option<Instant>;
}

// We occasionally want to make a dynamic selection of framing type, and in those cases it's
// often convenient to deal with a Box<dyn Format> instead of a custom thing. Provide this impl
// so that a Box<dyn Format> is a Format too.
impl Format for Box<dyn Format> {
    fn frame(
        &self,
        frame_type: FrameType,
        bytes: &[u8],
        outgoing: &mut Vec<u8>,
    ) -> Result<(), Error> {
        self.as_ref().frame(frame_type, bytes, outgoing)
    }

    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error> {
        self.as_ref().deframe(bytes)
    }

    fn deframe_timeout(&self, have_pending_bytes: bool) -> Option<Instant> {
        self.as_ref().deframe_timeout(have_pending_bytes)
    }
}

/// Success result of [`Format::deframe`].
pub struct Deframed {
    /// The new beginning of the parsing buffer, as an offset from the beginning of the buffer.
    pub new_start_pos: usize,
    /// Optional parsed frame from the buffer.
    pub frame: Option<(FrameType, Vec<u8>)>,
}

/// The type of frame.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum FrameType {
    /// A frame intended for Overnet.
    Overnet,
}

/// Manages framing of messages into a byte stream.
struct Framer<Fmt: Format> {
    fmt: Fmt,
    max_queued: usize,
    outgoing: Cell<Outgoing>,
    id: u64,
}

struct BVec(Vec<u8>);

impl std::fmt::Debug for BVec {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "[{}b]", self.0.len())
    }
}

#[derive(Debug)]
enum Outgoing {
    Closed,
    Open { buffer: BVec, waiting_read: Option<Waker>, waiting_write: Option<Waker> },
}

impl Outgoing {
    fn wake(self) {
        match self {
            Outgoing::Closed => (),
            Outgoing::Open { waiting_read, waiting_write, .. } => {
                waiting_read.map(|w| w.wake());
                waiting_write.map(|w| w.wake());
            }
        }
    }
}

impl Default for Outgoing {
    fn default() -> Self {
        Outgoing::Closed
    }
}

/// Writes frames into the framer.
pub struct FramerWriter<Fmt: Format> {
    framer: Rc<Framer<Fmt>>,
}

/// Reads framed bytes out of the framer.
pub struct FramerReader<Fmt: Format> {
    framer: Rc<Framer<Fmt>>,
}

/// Construct a new framer for some format.
pub fn new_framer<Fmt: Format>(
    fmt: Fmt,
    max_queued: usize,
) -> (FramerWriter<Fmt>, FramerReader<Fmt>) {
    let framer = Rc::new(Framer {
        fmt,
        max_queued,
        outgoing: Cell::new(Outgoing::Open {
            buffer: BVec(Vec::new()),
            waiting_read: None,
            waiting_write: None,
        }),
        id: generate_node_id().0,
    });
    (FramerWriter { framer: framer.clone() }, FramerReader { framer })
}

impl<Fmt: Format> FramerWriter<Fmt> {
    fn poll_write(
        &mut self,
        ctx: &mut Context<'_>,
        frame_type: FrameType,
        bytes: &[u8],
    ) -> Poll<Result<(), Error>> {
        let outgoing = self.framer.outgoing.take();
        log::trace!("{} poll_write: {:?}", self.framer.id, outgoing);
        match outgoing {
            Outgoing::Closed => Poll::Ready(Err(format_err!("Closed Framer"))),
            Outgoing::Open { buffer: BVec(mut buffer), mut waiting_read, waiting_write: _ } => {
                if buffer.len() >= self.framer.max_queued {
                    self.framer.outgoing.set(Outgoing::Open {
                        buffer: BVec(buffer),
                        waiting_read,
                        waiting_write: Some(ctx.waker().clone()),
                    });
                    Poll::Pending
                } else {
                    self.framer.fmt.frame(frame_type, bytes, &mut buffer)?;
                    waiting_read.take().map(|w| w.wake());
                    self.framer.outgoing.set(Outgoing::Open {
                        buffer: BVec(buffer),
                        waiting_read,
                        waiting_write: None,
                    });
                    Poll::Ready(Ok(()))
                }
            }
        }
    }

    /// Write a frame into the framer.
    pub async fn write(&mut self, frame_type: FrameType, bytes: &[u8]) -> Result<(), Error> {
        poll_fn(|ctx| self.poll_write(ctx, frame_type, bytes)).await
    }
}

impl<Fmt: Format> Drop for FramerWriter<Fmt> {
    fn drop(&mut self) {
        self.framer.outgoing.take().wake();
    }
}

impl<Fmt: Format> FramerReader<Fmt> {
    fn poll_read(&mut self, ctx: &mut Context<'_>) -> Poll<Result<Vec<u8>, Error>> {
        let outgoing = self.framer.outgoing.take();
        log::trace!("{} poll_read: {:?}", self.framer.id, outgoing);
        match outgoing {
            Outgoing::Closed => Poll::Ready(Err(format_err!("Closed Framer"))),
            Outgoing::Open { buffer: BVec(buffer), mut waiting_write, waiting_read: _ } => {
                if buffer.is_empty() {
                    self.framer.outgoing.set(Outgoing::Open {
                        buffer: BVec(buffer),
                        waiting_write,
                        waiting_read: Some(ctx.waker().clone()),
                    });
                    Poll::Pending
                } else {
                    waiting_write.take().map(|w| w.wake());
                    self.framer.outgoing.set(Outgoing::Open {
                        buffer: BVec(Vec::new()),
                        waiting_write,
                        waiting_read: None,
                    });
                    Poll::Ready(Ok(buffer))
                }
            }
        }
    }

    /// Read framed bytes out of the framer.
    pub async fn read(&mut self) -> Result<Vec<u8>, Error> {
        poll_fn(|ctx| self.poll_read(ctx)).await
    }
}

impl<Fmt: Format> Drop for FramerReader<Fmt> {
    fn drop(&mut self) {
        self.framer.outgoing.take().wake();
    }
}

struct Deframer<Fmt: Format> {
    fmt: Fmt,
    incoming: Cell<Incoming>,
    id: u64,
}

struct TimeoutFut(Option<Pin<Box<dyn Future<Output = ()>>>>);

impl std::fmt::Debug for TimeoutFut {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.as_ref().map(|_| "timeout").fmt(f)
    }
}

#[derive(Debug)]
enum Incoming {
    Parsing { unparsed: BVec, waiting_read: Option<Waker>, timeout: TimeoutFut },
    Queuing { frame_type: FrameType, bytes: BVec, unparsed: BVec, waiting_write: Option<Waker> },
    Closed,
}

impl Default for Incoming {
    fn default() -> Self {
        Incoming::Closed
    }
}

impl Incoming {
    fn wake(self) {
        match self {
            Incoming::Closed => (),
            Incoming::Parsing { waiting_read, .. } => {
                waiting_read.map(|w| w.wake());
            }
            Incoming::Queuing { waiting_write, .. } => {
                waiting_write.map(|w| w.wake());
            }
        }
    }
}

/// Writes framed bytes into the deframer.
pub struct DeframerWriter<Fmt: Format> {
    deframer: Rc<Deframer<Fmt>>,
}

/// Reads deframed packets from a deframer.
pub struct DeframerReader<Fmt: Format> {
    deframer: Rc<Deframer<Fmt>>,
}

/// Construct a new deframer, with an optional timeout for reads (such that bytes can be skipped).
pub fn new_deframer<Fmt: Format>(fmt: Fmt) -> (DeframerWriter<Fmt>, DeframerReader<Fmt>) {
    let deframer = Rc::new(Deframer {
        incoming: Cell::new(Incoming::Parsing {
            unparsed: BVec(Vec::new()),
            waiting_read: None,
            timeout: TimeoutFut(fmt.deframe_timeout(false).map(|i| wait_until(i).boxed_local())),
        }),
        fmt,
        id: generate_node_id().0,
    });
    (DeframerWriter { deframer: deframer.clone() }, DeframerReader { deframer })
}

fn deframe_step<Fmt: Format>(
    mut unparsed: Vec<u8>,
    mut waiting_read: Option<Waker>,
    fmt: &Fmt,
) -> Result<Incoming, Error> {
    let Deframed { frame, new_start_pos } = fmt.deframe(&unparsed)?;
    if new_start_pos != 0 {
        unparsed.drain(..new_start_pos);
    }
    if let Some((frame_type, bytes)) = frame {
        waiting_read.take().map(|w| w.wake());
        Ok(Incoming::Queuing {
            frame_type,
            bytes: BVec(bytes),
            unparsed: BVec(unparsed),
            waiting_write: None,
        })
    } else {
        Ok(Incoming::Parsing {
            timeout: TimeoutFut(
                fmt.deframe_timeout(unparsed.len() > 0).map(|when| wait_until(when).boxed_local()),
            ),
            unparsed: BVec(unparsed),
            waiting_read,
        })
    }
}

impl<Fmt: Format> DeframerWriter<Fmt> {
    fn poll_write(&mut self, ctx: &mut Context<'_>, bytes: &[u8]) -> Poll<Result<(), Error>> {
        let incoming = self.deframer.incoming.take();
        log::trace!("{} poll_write: {:?}", self.deframer.id, incoming);
        match incoming {
            Incoming::Closed => Poll::Ready(Err(format_err!("Deframer closed"))),
            Incoming::Parsing { unparsed: BVec(mut unparsed), waiting_read, timeout: _ } => {
                unparsed.extend_from_slice(bytes);
                self.deframer.incoming.set(deframe_step(
                    unparsed,
                    waiting_read,
                    &self.deframer.fmt,
                )?);
                Poll::Ready(Ok(()))
            }
            Incoming::Queuing { unparsed, frame_type, bytes, waiting_write: _ } => {
                self.deframer.incoming.set(Incoming::Queuing {
                    unparsed,
                    frame_type,
                    bytes,
                    waiting_write: Some(ctx.waker().clone()),
                });
                Poll::Pending
            }
        }
    }

    /// Write some data into the deframer, to be deframed and read later.
    pub async fn write(&mut self, bytes: &[u8]) -> Result<(), Error> {
        poll_fn(|ctx| self.poll_write(ctx, bytes)).await
    }
}

impl<Fmt: Format> Drop for DeframerWriter<Fmt> {
    fn drop(&mut self) {
        self.deframer.incoming.take().wake();
    }
}

impl<Fmt: Format> DeframerReader<Fmt> {
    fn poll_read(&mut self, ctx: &mut Context<'_>) -> Poll<Result<(FrameType, Vec<u8>), Error>> {
        let incoming = self.deframer.incoming.take();
        log::trace!("{} poll_read: {:?}", self.deframer.id, incoming);
        match incoming {
            Incoming::Closed => Poll::Ready(Err(format_err!("Deframer closed"))),
            Incoming::Queuing {
                unparsed: BVec(unparsed),
                frame_type,
                bytes: BVec(bytes),
                waiting_write,
            } => {
                self.deframer.incoming.set(deframe_step(unparsed, None, &self.deframer.fmt)?);
                waiting_write.map(|w| w.wake());
                Poll::Ready(Ok((frame_type, bytes)))
            }
            Incoming::Parsing { unparsed, timeout: TimeoutFut(None), waiting_read: _ } => {
                self.deframer.incoming.set(Incoming::Parsing {
                    unparsed,
                    waiting_read: Some(ctx.waker().clone()),
                    timeout: TimeoutFut(None),
                });
                Poll::Pending
            }
            Incoming::Parsing {
                unparsed: BVec(mut unparsed),
                timeout: TimeoutFut(Some(mut timeout)),
                waiting_read: _,
            } => match timeout.as_mut().poll(ctx) {
                Poll::Pending => {
                    self.deframer.incoming.set(Incoming::Parsing {
                        unparsed: BVec(unparsed),
                        waiting_read: Some(ctx.waker().clone()),
                        timeout: TimeoutFut(Some(timeout)),
                    });
                    Poll::Pending
                }
                Poll::Ready(()) => {
                    unparsed.drain(..1);
                    self.deframer.incoming.set(deframe_step(unparsed, None, &self.deframer.fmt)?);
                    self.poll_read(ctx)
                }
            },
        }
    }

    /// Read one frame from the deframer.
    pub async fn read(&mut self) -> Result<(FrameType, Vec<u8>), Error> {
        poll_fn(|ctx| self.poll_read(ctx)).await
    }
}

impl<Fmt: Format> Drop for DeframerReader<Fmt> {
    fn drop(&mut self) {
        self.deframer.incoming.take().wake();
    }
}

/// Framing format that assumes an underlying transport that *MAY* lose/duplicate/corrupt some bytes
/// but usually transports the full 8 bits in a byte (e.g. many serial transports).
pub struct LossyBinary {
    duration_per_byte: Duration,
}

impl LossyBinary {
    /// Create a new LossyBinary format instance with some timeout waiting for bytes (if this is
    /// exceeded a byte will be skipped in the input).
    pub fn new(duration_per_byte: Duration) -> Self {
        Self { duration_per_byte }
    }
}

impl Format for LossyBinary {
    fn frame(
        &self,
        frame_type: FrameType,
        bytes: &[u8],
        outgoing: &mut Vec<u8>,
    ) -> Result<(), Error> {
        if bytes.len() > (std::u16::MAX as usize) + 1 {
            return Err(anyhow::format_err!(
                "Packet length ({}) too long for stream framing",
                bytes.len()
            ));
        }
        match frame_type {
            FrameType::Overnet => outgoing.write_u8(0u8)?, // '\0'
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
            if buf.len() <= 8 {
                return Ok(Deframed { frame: None, new_start_pos: start });
            }
            let frame_type = match buf[0] {
                0u8 => FrameType::Overnet,
                _ => {
                    // Not a start marker: remove and continue
                    start += 1;
                    continue;
                }
            };
            let len = 1 + (u16::from_le_bytes([buf[1], buf[2]]) as usize);
            let crc = u32::from_le_bytes([buf[3], buf[4], buf[5], buf[6]]);
            if buf.len() < 8 + len {
                // Not enough bytes to deframe: done for now
                return Ok(Deframed { frame: None, new_start_pos: start });
            }
            if buf[7 + len] != 10u8 {
                // Does not end with an end marker: remove start byte and continue
                start += 1;
                continue;
            }
            let frame = &buf[7..7 + len];
            let crc_actual = crc32::checksum_ieee(frame);
            if crc != crc_actual {
                // CRC mismatch: skip start marker and continue
                start += 1;
                continue;
            }
            // Successfully got a frame! Save it, and continue
            start += 8 + len;
            return Ok(Deframed {
                frame: Some((frame_type, frame.to_vec())),
                new_start_pos: start,
            });
        }
    }

    fn deframe_timeout(&self, have_pending_bytes: bool) -> Option<Instant> {
        if have_pending_bytes {
            Some(Instant::now() + self.duration_per_byte)
        } else {
            None
        }
    }
}

/// Framing format that assumes a lossless underlying byte stream that can transport all 8 bits of a
/// byte.
pub struct LosslessBinary;

impl Format for LosslessBinary {
    fn frame(
        &self,
        frame_type: FrameType,
        bytes: &[u8],
        outgoing: &mut Vec<u8>,
    ) -> Result<(), Error> {
        if bytes.len() > (std::u16::MAX as usize) + 1 {
            return Err(anyhow::format_err!(
                "Packet length ({}) too long for stream framing",
                bytes.len()
            ));
        }
        match frame_type {
            FrameType::Overnet => outgoing.write_u8(0u8)?, // '\0'
        }
        outgoing.write_u16::<byteorder::LittleEndian>((bytes.len() - 1) as u16)?;
        outgoing.extend_from_slice(bytes);
        Ok(())
    }

    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error> {
        log::trace!("DEFRAME: buffer_len={}", bytes.len());
        if bytes.len() <= 4 {
            return Ok(Deframed { frame: None, new_start_pos: 0 });
        }
        let frame_type = match bytes[0] {
            0u8 => FrameType::Overnet,
            _ => return Err(format_err!("Bad frame type {:?}", bytes[0])),
        };
        log::trace!("DEFRAME: frame_type={:?}", frame_type);
        let len = 1 + (u16::from_le_bytes([bytes[1], bytes[2]]) as usize);
        log::trace!("DEFRAME: frame_len={}", len);
        if bytes.len() < 3 + len {
            // Not enough bytes to deframe: done for now.
            return Ok(Deframed { frame: None, new_start_pos: 0 });
        }
        let frame = &bytes[3..3 + len];
        return Ok(Deframed { frame: Some((frame_type, frame.to_vec())), new_start_pos: 3 + len });
    }

    fn deframe_timeout(&self, _have_pending_bytes: bool) -> Option<Instant> {
        None
    }
}

#[cfg(test)]
mod test {

    use super::*;
    use crate::router::test_util::run;

    fn join(mut a: Vec<u8>, mut b: Vec<u8>) -> Vec<u8> {
        a.append(&mut b);
        a
    }

    #[test]
    fn simple_frame() {
        run(|| async move {
            let (mut framer_writer, mut framer_reader) = new_framer(LosslessBinary, 1024);
            framer_writer.write(FrameType::Overnet, &[1, 2, 3, 4]).await.unwrap();
            let (mut deframer_writer, mut deframer_reader) = new_deframer(LosslessBinary);
            deframer_writer.write(framer_reader.read().await.unwrap().as_slice()).await.unwrap();
            assert_eq!(
                deframer_reader.read().await.unwrap(),
                (FrameType::Overnet, vec![1, 2, 3, 4])
            );
            framer_writer.write(FrameType::Overnet, &[5, 6, 7, 8]).await.unwrap();
            deframer_writer.write(framer_reader.read().await.unwrap().as_slice()).await.unwrap();
            assert_eq!(
                deframer_reader.read().await.unwrap(),
                (FrameType::Overnet, vec![5, 6, 7, 8])
            );
        })
    }

    #[test]
    fn large_frame() {
        run(|| async move {
            let big_slice = [0u8; 100000];
            let (mut framer_writer, _framer_reader) = new_framer(LosslessBinary, 1024);
            assert!(framer_writer.write(FrameType::Overnet, &big_slice).await.is_err());
        })
    }

    #[test]
    fn skip_junk_start_0() {
        run(|| async move {
            let (mut framer_writer, mut framer_reader) =
                new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
            framer_writer.write(FrameType::Overnet, &[1, 2, 3, 4]).await.unwrap();
            let (mut deframer_writer, mut deframer_reader) =
                new_deframer(LossyBinary::new(Duration::from_millis(100)));
            deframer_writer
                .write(join(vec![0], framer_reader.read().await.unwrap()).as_slice())
                .await
                .unwrap();
            assert_eq!(
                deframer_reader.read().await.unwrap(),
                (FrameType::Overnet, vec![1, 2, 3, 4])
            );
        })
    }

    #[test]
    fn skip_junk_start_1() {
        run(|| async move {
            let (mut framer_writer, mut framer_reader) =
                new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
            framer_writer.write(FrameType::Overnet, &[1, 2, 3, 4]).await.unwrap();
            let (mut deframer_writer, mut deframer_reader) =
                new_deframer(LossyBinary::new(Duration::from_millis(100)));
            deframer_writer
                .write(join(vec![1], framer_reader.read().await.unwrap()).as_slice())
                .await
                .unwrap();
            assert_eq!(
                deframer_reader.read().await.unwrap(),
                (FrameType::Overnet, vec![1, 2, 3, 4])
            );
        })
    }
}
