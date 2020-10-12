// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles framing/deframing of stream links

use crate::future_help::PollMutex;
use anyhow::{format_err, Error};
use byteorder::WriteBytesExt;
use crc::crc32;
use fuchsia_async::{Task, Timer};
use futures::future::poll_fn;
use futures::lock::Mutex;
use futures::prelude::*;
use futures::ready;
use std::sync::Arc;
use std::task::{Context, Poll, Waker};
use std::time::Duration;

use crate::router::generate_node_id;

/// Describes a framing format.
pub trait Format: Send + Sync + 'static {
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
    fn deframe_timeout(&self, have_pending_bytes: bool) -> Option<Duration>;
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

    fn deframe_timeout(&self, have_pending_bytes: bool) -> Option<Duration> {
        self.as_ref().deframe_timeout(have_pending_bytes)
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
    pub frame: Option<(FrameType, Vec<u8>)>,
}

/// The type of frame.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum FrameType {
    /// The first frame of an Overnet conversation.
    OvernetHello,
    /// A frame intended for Overnet.
    Overnet,
}

/// Manages framing of messages into a byte stream.
struct Framer<Fmt: Format> {
    fmt: Fmt,
    max_queued: usize,
    outgoing: Mutex<Outgoing>,
    id: u64,
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
    framer: Arc<Framer<Fmt>>,
}

/// Reads framed bytes out of the framer.
pub struct FramerReader<Fmt: Format> {
    framer: Arc<Framer<Fmt>>,
}

/// Construct a new framer for some format.
pub fn new_framer<Fmt: Format>(
    fmt: Fmt,
    max_queued: usize,
) -> (FramerWriter<Fmt>, FramerReader<Fmt>) {
    let framer = Arc::new(Framer {
        fmt,
        max_queued,
        outgoing: Mutex::new(Outgoing::Open {
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
        &self,
        ctx: &mut Context<'_>,
        frame_type: FrameType,
        bytes: &[u8],
        lock: &mut PollMutex<'_, Outgoing>,
    ) -> Poll<Result<(), Error>> {
        let mut outgoing = ready!(lock.poll(ctx));
        log::trace!("{} poll_write: {:?}", self.framer.id, outgoing);
        match std::mem::replace(&mut *outgoing, Outgoing::Closed) {
            Outgoing::Closed => Poll::Ready(Err(format_err!("Closed Framer"))),
            Outgoing::Open { buffer: BVec(mut buffer), mut waiting_read, waiting_write: _ } => {
                if buffer.len() >= self.framer.max_queued {
                    *outgoing = Outgoing::Open {
                        buffer: BVec(buffer),
                        waiting_read,
                        waiting_write: Some(ctx.waker().clone()),
                    };
                    Poll::Pending
                } else {
                    self.framer.fmt.frame(frame_type, bytes, &mut buffer)?;
                    waiting_read.take().map(|w| w.wake());
                    *outgoing =
                        Outgoing::Open { buffer: BVec(buffer), waiting_read, waiting_write: None };
                    Poll::Ready(Ok(()))
                }
            }
        }
    }

    /// Write a frame into the framer.
    pub async fn write(&mut self, frame_type: FrameType, bytes: &[u8]) -> Result<(), Error> {
        let mut lock = PollMutex::new(&self.framer.outgoing);
        poll_fn(|ctx| self.poll_write(ctx, frame_type, bytes, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for FramerWriter<Fmt> {
    fn drop(&mut self) {
        let framer = self.framer.clone();
        // TODO: don't detach
        Task::spawn(async move {
            std::mem::replace(&mut *framer.outgoing.lock().await, Outgoing::Closed).wake();
        })
        .detach()
    }
}

impl<Fmt: Format> FramerReader<Fmt> {
    fn poll_read(
        &self,
        ctx: &mut Context<'_>,
        lock: &mut PollMutex<'_, Outgoing>,
    ) -> Poll<Result<Vec<u8>, Error>> {
        let mut outgoing = ready!(lock.poll(ctx));
        log::trace!("{} poll_read: {:?}", self.framer.id, outgoing);
        match std::mem::replace(&mut *outgoing, Outgoing::Closed) {
            Outgoing::Closed => Poll::Ready(Err(format_err!("Closed Framer"))),
            Outgoing::Open { buffer: BVec(buffer), mut waiting_write, waiting_read: _ } => {
                if buffer.is_empty() {
                    *outgoing = Outgoing::Open {
                        buffer: BVec(buffer),
                        waiting_write,
                        waiting_read: Some(ctx.waker().clone()),
                    };
                    Poll::Pending
                } else {
                    waiting_write.take().map(|w| w.wake());
                    *outgoing = Outgoing::Open {
                        buffer: BVec(Vec::new()),
                        waiting_write,
                        waiting_read: None,
                    };
                    Poll::Ready(Ok(buffer))
                }
            }
        }
    }

    /// Read framed bytes out of the framer.
    pub async fn read(&mut self) -> Result<Vec<u8>, Error> {
        let mut lock = PollMutex::new(&self.framer.outgoing);
        poll_fn(|ctx| self.poll_read(ctx, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for FramerReader<Fmt> {
    fn drop(&mut self) {
        let framer = self.framer.clone();
        // TODO: don't detach
        Task::spawn(async move {
            std::mem::replace(&mut *framer.outgoing.lock().await, Outgoing::Closed).wake();
        })
        .detach()
    }
}

struct Deframer<Fmt: Format> {
    fmt: Fmt,
    incoming: Mutex<Incoming>,
    id: u64,
}

#[derive(Debug)]
enum Incoming {
    Parsing {
        unparsed: BVec,
        waiting_read: Option<Waker>,
        timeout: Option<Timer>,
    },
    Queuing {
        unframed: Option<BVec>,
        framed: Option<(FrameType, BVec)>,
        unparsed: BVec,
        waiting_write: Option<Waker>,
    },
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
    deframer: Arc<Deframer<Fmt>>,
}

/// Reads deframed packets from a deframer.
pub struct DeframerReader<Fmt: Format> {
    deframer: Arc<Deframer<Fmt>>,
}

/// Construct a new deframer, with an optional timeout for reads (such that bytes can be skipped).
pub fn new_deframer<Fmt: Format>(fmt: Fmt) -> (DeframerWriter<Fmt>, DeframerReader<Fmt>) {
    let deframer = Arc::new(Deframer {
        incoming: Mutex::new(Incoming::Parsing {
            unparsed: BVec(Vec::new()),
            waiting_read: None,
            timeout: fmt.deframe_timeout(false).map(Timer::new),
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
    let Deframed { frame, unframed_bytes, new_start_pos } = fmt.deframe(&unparsed)?;
    log::trace!(
        "unparsed:{:?} frame:{:?} unframed_bytes:{:?} new_start_pos:{:?}",
        unparsed,
        frame,
        unframed_bytes,
        new_start_pos
    );
    assert!(unframed_bytes <= new_start_pos);
    let mut unframed = None;
    if unframed_bytes != 0 {
        let mut unframed_vec = unparsed.split_off(unframed_bytes);
        std::mem::swap(&mut unframed_vec, &mut unparsed);
        unframed = Some(BVec(unframed_vec));
    }
    if new_start_pos != unframed_bytes {
        unparsed.drain(..(new_start_pos - unframed_bytes));
    }
    if frame.is_some() || unframed.is_some() {
        waiting_read.take().map(|w| w.wake());
        Ok(Incoming::Queuing {
            framed: frame.map(|(frame_type, bytes)| (frame_type, BVec(bytes))),
            unframed,
            unparsed: BVec(unparsed),
            waiting_write: None,
        })
    } else {
        Ok(Incoming::Parsing {
            timeout: make_timeout(fmt, unparsed.len()),
            unparsed: BVec(unparsed),
            waiting_read,
        })
    }
}

fn make_timeout<Fmt: Format>(fmt: &Fmt, unparsed_len: usize) -> Option<Timer> {
    fmt.deframe_timeout(unparsed_len > 0).map(Timer::new)
}

impl<Fmt: Format> DeframerWriter<Fmt> {
    fn poll_write(
        &self,
        ctx: &mut Context<'_>,
        bytes: &[u8],
        lock: &mut PollMutex<'_, Incoming>,
    ) -> Poll<Result<(), Error>> {
        let mut incoming = ready!(lock.poll(ctx));
        log::trace!("{} poll_write: {:?} bytes={:?}", self.deframer.id, incoming, bytes);
        match std::mem::replace(&mut *incoming, Incoming::Closed) {
            Incoming::Closed => Poll::Ready(Err(format_err!("Deframer closed during write"))),
            Incoming::Parsing { unparsed: BVec(mut unparsed), waiting_read, timeout: _ } => {
                unparsed.extend_from_slice(bytes);
                *incoming = deframe_step(unparsed, waiting_read, &self.deframer.fmt)?;
                Poll::Ready(Ok(()))
            }
            Incoming::Queuing { unparsed, framed, unframed, waiting_write: _ } => {
                *incoming = Incoming::Queuing {
                    unparsed,
                    framed,
                    unframed,
                    waiting_write: Some(ctx.waker().clone()),
                };
                Poll::Pending
            }
        }
    }

    /// Write some data into the deframer, to be deframed and read later.
    pub async fn write(&mut self, bytes: &[u8]) -> Result<(), Error> {
        if bytes.is_empty() {
            return Ok(());
        }
        let mut lock = PollMutex::new(&self.deframer.incoming);
        poll_fn(|ctx| self.poll_write(ctx, bytes, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for DeframerWriter<Fmt> {
    fn drop(&mut self) {
        let framer = self.deframer.clone();
        // TODO: don't detach
        Task::spawn(async move {
            std::mem::replace(&mut *framer.incoming.lock().await, Incoming::Closed).wake();
        })
        .detach()
    }
}

impl<Fmt: Format> DeframerReader<Fmt> {
    fn poll_read(
        &self,
        ctx: &mut Context<'_>,
        lock: &mut PollMutex<'_, Incoming>,
    ) -> Poll<Result<(Option<FrameType>, Vec<u8>), Error>> {
        let mut incoming = ready!(lock.poll(ctx));
        loop {
            log::trace!("{} poll_read: {:?}", self.deframer.id, incoming);
            break match std::mem::replace(&mut *incoming, Incoming::Closed) {
                Incoming::Closed => Poll::Ready(Err(format_err!("Deframer closed during read"))),
                Incoming::Queuing {
                    unframed: Some(BVec(unframed)),
                    framed,
                    unparsed: BVec(unparsed),
                    waiting_write,
                } => {
                    if framed.is_some() {
                        *incoming = Incoming::Queuing {
                            unframed: None,
                            framed,
                            unparsed: BVec(unparsed),
                            waiting_write,
                        };
                    } else {
                        *incoming = deframe_step(unparsed, None, &self.deframer.fmt)?;
                        waiting_write.map(|w| w.wake());
                    }
                    Poll::Ready(Ok((None, unframed)))
                }
                Incoming::Queuing {
                    unframed: None,
                    framed: Some((frame_type, BVec(bytes))),
                    unparsed: BVec(unparsed),
                    waiting_write,
                } => {
                    *incoming = deframe_step(unparsed, None, &self.deframer.fmt)?;
                    waiting_write.map(|w| w.wake());
                    Poll::Ready(Ok((Some(frame_type), bytes)))
                }
                Incoming::Queuing { unframed: None, framed: None, .. } => unreachable!(),
                Incoming::Parsing { unparsed, timeout: None, waiting_read: _ } => {
                    *incoming = Incoming::Parsing {
                        unparsed,
                        waiting_read: Some(ctx.waker().clone()),
                        timeout: None,
                    };
                    Poll::Pending
                }
                Incoming::Parsing {
                    unparsed: BVec(mut unparsed),
                    timeout: Some(mut timeout),
                    waiting_read,
                } => match timeout.poll_unpin(ctx) {
                    Poll::Pending => {
                        *incoming = Incoming::Parsing {
                            unparsed: BVec(unparsed),
                            waiting_read: Some(ctx.waker().clone()),
                            timeout: Some(timeout),
                        };
                        Poll::Pending
                    }
                    Poll::Ready(()) => {
                        let mut unframed = unparsed.split_off(1);
                        std::mem::swap(&mut unframed, &mut unparsed);
                        log::trace!("timeout - drop byte {:?} rest {:?}", unframed, unparsed);
                        waiting_read.map(|w| w.wake());
                        *incoming = Incoming::Queuing {
                            unframed: Some(BVec(unframed)),
                            framed: None,
                            unparsed: BVec(unparsed),
                            waiting_write: None,
                        };
                        continue;
                    }
                },
            };
        }
    }

    /// Read one frame from the deframer.
    pub async fn read(&mut self) -> Result<(Option<FrameType>, Vec<u8>), Error> {
        let mut lock = PollMutex::new(&self.deframer.incoming);
        poll_fn(|ctx| self.poll_read(ctx, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for DeframerReader<Fmt> {
    fn drop(&mut self) {
        let framer = self.deframer.clone();
        // TODO: don't detach
        Task::spawn(async move {
            std::mem::replace(&mut *framer.incoming.lock().await, Incoming::Closed).wake();
        })
        .detach()
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
            FrameType::Overnet => outgoing.write_u8(0u8)?,
            FrameType::OvernetHello => outgoing.write_u8(255u8)?,
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
                return Ok(Deframed { frame: None, unframed_bytes: start, new_start_pos: start });
            }
            let frame_type = match buf[0] {
                0u8 => FrameType::Overnet,
                255u8 => FrameType::OvernetHello,
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
                return Ok(Deframed { frame: None, unframed_bytes: start, new_start_pos: start });
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
            return Ok(Deframed {
                frame: Some((frame_type, frame.to_vec())),
                unframed_bytes: start,
                new_start_pos: start + 8 + len,
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
            FrameType::Overnet => outgoing.write_u8(0u8)?,
            FrameType::OvernetHello => outgoing.write_u8(255u8)?,
        }
        outgoing.write_u16::<byteorder::LittleEndian>((bytes.len() - 1) as u16)?;
        outgoing.extend_from_slice(bytes);
        Ok(())
    }

    fn deframe(&self, bytes: &[u8]) -> Result<Deframed, Error> {
        log::trace!("DEFRAME: buffer_len={}", bytes.len());
        if bytes.len() <= 4 {
            return Ok(Deframed { frame: None, unframed_bytes: 0, new_start_pos: 0 });
        }
        let frame_type = match bytes[0] {
            0u8 => FrameType::Overnet,
            255u8 => FrameType::OvernetHello,
            _ => return Err(format_err!("Bad frame type {:?}", bytes[0])),
        };
        log::trace!("DEFRAME: frame_type={:?}", frame_type);
        let len = 1 + (u16::from_le_bytes([bytes[1], bytes[2]]) as usize);
        log::trace!("DEFRAME: frame_len={}", len);
        if bytes.len() < 3 + len {
            // Not enough bytes to deframe: done for now.
            return Ok(Deframed { frame: None, unframed_bytes: 0, new_start_pos: 0 });
        }
        let frame = &bytes[3..3 + len];
        return Ok(Deframed {
            frame: Some((frame_type, frame.to_vec())),
            unframed_bytes: 0,
            new_start_pos: 3 + len,
        });
    }

    fn deframe_timeout(&self, _have_pending_bytes: bool) -> Option<Duration> {
        None
    }
}

#[cfg(test)]
mod test {

    use super::*;

    fn join(mut a: Vec<u8>, mut b: Vec<u8>) -> Vec<u8> {
        a.append(&mut b);
        a
    }

    #[fuchsia_async::run(1, test)]
    async fn simple_frame() -> Result<(), Error> {
        let (mut framer_writer, mut framer_reader) = new_framer(LosslessBinary, 1024);
        framer_writer.write(FrameType::Overnet, &[1, 2, 3, 4]).await?;
        let (mut deframer_writer, mut deframer_reader) = new_deframer(LosslessBinary);
        deframer_writer.write(framer_reader.read().await?.as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, (Some(FrameType::Overnet), vec![1, 2, 3, 4]));
        framer_writer.write(FrameType::Overnet, &[5, 6, 7, 8]).await?;
        deframer_writer.write(framer_reader.read().await?.as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, (Some(FrameType::Overnet), vec![5, 6, 7, 8]));
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn simple_frame_lossy_binary() -> Result<(), Error> {
        let (mut framer_writer, mut framer_reader) =
            new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
        framer_writer.write(FrameType::Overnet, &[1, 2, 3, 4]).await?;
        let (mut deframer_writer, mut deframer_reader) =
            new_deframer(LossyBinary::new(Duration::from_millis(100)));
        deframer_writer.write(framer_reader.read().await?.as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, (Some(FrameType::Overnet), vec![1, 2, 3, 4]));
        framer_writer.write(FrameType::Overnet, &[5, 6, 7, 8]).await?;
        deframer_writer.write(framer_reader.read().await?.as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, (Some(FrameType::Overnet), vec![5, 6, 7, 8]));
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn large_frame() -> Result<(), Error> {
        let big_slice = vec![0u8; 100000];
        let (mut framer_writer, _framer_reader) = new_framer(LosslessBinary, 1024);
        assert!(framer_writer.write(FrameType::Overnet, &big_slice).await.is_err());
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn skip_junk_start_0() -> Result<(), Error> {
        let (mut framer_writer, mut framer_reader) =
            new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
        framer_writer.write(FrameType::Overnet, &[1, 2, 3, 4]).await?;
        let (mut deframer_writer, mut deframer_reader) =
            new_deframer(LossyBinary::new(Duration::from_millis(100)));
        deframer_writer.write(join(vec![0], framer_reader.read().await?).as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, (None, vec![0]));
        assert_eq!(deframer_reader.read().await?, (Some(FrameType::Overnet), vec![1, 2, 3, 4]));
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn skip_junk_start_1() -> Result<(), Error> {
        let (mut framer_writer, mut framer_reader) =
            new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
        framer_writer.write(FrameType::Overnet, &[1, 2, 3, 4]).await?;
        let (mut deframer_writer, mut deframer_reader) =
            new_deframer(LossyBinary::new(Duration::from_millis(100)));
        deframer_writer.write(join(vec![1], framer_reader.read().await?).as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, (None, vec![1]));
        assert_eq!(deframer_reader.read().await?, (Some(FrameType::Overnet), vec![1, 2, 3, 4]));
        Ok(())
    }
}
