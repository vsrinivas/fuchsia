// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles framing/deframing of stream links

use anyhow::{format_err, Error};
use async_utils::mutex_ticket::MutexTicket;
use fuchsia_async::Timer;
use futures::{future::poll_fn, lock::Mutex, prelude::*, ready, task::AtomicWaker};
use std::sync::{
    atomic::{AtomicBool, Ordering},
    Arc,
};
use std::task::{Context, Poll};
use std::time::Duration;

/// Describes a framing format.
pub trait Format: Send + Sync + 'static {
    /// Write a frame of `frame_type` with payload `bytes` into `outgoing`.
    fn frame(&self, bytes: &[u8], outgoing: &mut Vec<u8>) -> Result<(), Error>;

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
    fn frame(&self, bytes: &[u8], outgoing: &mut Vec<u8>) -> Result<(), Error> {
        self.as_ref().frame(bytes, outgoing)
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
    pub frame: Option<Vec<u8>>,
}

/// Manages framing of messages into a byte stream.
struct Framer<Fmt: Format> {
    fmt: Fmt,
    max_queued: usize,
    is_closed: AtomicBool,
    waiting_read: AtomicWaker,
    waiting_write: AtomicWaker,
    buffer: Mutex<BVec>,
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
        is_closed: AtomicBool::new(false),
        waiting_read: AtomicWaker::new(),
        waiting_write: AtomicWaker::new(),
        buffer: Mutex::new(BVec(Vec::new())),
    });
    (FramerWriter { framer: framer.clone() }, FramerReader { framer })
}

impl<Fmt: Format> FramerWriter<Fmt> {
    fn poll_write(
        &self,
        ctx: &mut Context<'_>,
        bytes: &[u8],
        lock: &mut MutexTicket<'_, BVec>,
    ) -> Poll<Result<(), Error>> {
        let mut buffer = ready!(lock.poll(ctx));

        // Only write to the framer if it isn't at max capacity
        let result = if buffer.0.len() >= self.framer.max_queued {
            // Register for wake on read
            self.framer.waiting_write.register(ctx.waker());

            Poll::Pending
        } else {
            // Frame bytes into the buffer
            self.framer.fmt.frame(bytes, &mut buffer.0)?;

            // Stop waking up on read
            self.framer.waiting_write.take();
            // Wake up the reader (if any)
            self.framer.waiting_read.take().map(|w| w.wake());

            Poll::Ready(Ok(()))
        };

        // If the reader has already closed the framer then we fail even if we
        // managed to write bytes into the buffer. Note that wakers must be
        // registered before checking `is_closed` to prevent a race.
        if self.framer.is_closed.load(Ordering::Acquire) {
            Poll::Ready(Err(format_err!("Framer closed during write")))
        } else {
            result
        }
    }

    /// Write a frame into the framer.
    pub async fn write(&mut self, bytes: &[u8]) -> Result<(), Error> {
        let mut lock = MutexTicket::new(&self.framer.buffer);
        poll_fn(|ctx| self.poll_write(ctx, bytes, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for FramerWriter<Fmt> {
    fn drop(&mut self) {
        // Signal to the other end that the framer is closed
        self.framer.is_closed.store(true, Ordering::Release);
        // Wake up reader in case it was waiting for more bytes to be written
        self.framer.waiting_read.take().map(|w| w.wake());
    }
}

impl<Fmt: Format> FramerReader<Fmt> {
    fn poll_read(
        &self,
        ctx: &mut Context<'_>,
        lock: &mut MutexTicket<'_, BVec>,
    ) -> Poll<Result<Vec<u8>, Error>> {
        let mut buffer = ready!(lock.poll(ctx));

        if buffer.0.is_empty() {
            // Register for wake on write
            self.framer.waiting_read.register(ctx.waker());

            // If the writer has closed the framer and we've read all the
            // remaining bytes, we're finally allowed to fail. Note that wakers
            // must be registered before checking `is_closed` to prevent a race.
            if self.framer.is_closed.load(Ordering::Acquire) {
                Poll::Ready(Err(format_err!("Framer closed during read")))
            } else {
                Poll::Pending
            }
        } else {
            // Take all of the available bytes to return them
            let buffer = core::mem::replace(&mut buffer.0, Vec::new());

            // Stop waking up on write
            self.framer.waiting_read.take();
            // Wake up the writer (if any)
            self.framer.waiting_write.take().map(|w| w.wake());

            Poll::Ready(Ok(buffer))
        }
    }

    /// Read framed bytes out of the framer.
    pub async fn read(&mut self) -> Result<Vec<u8>, Error> {
        let mut lock = MutexTicket::new(&self.framer.buffer);
        poll_fn(|ctx| self.poll_read(ctx, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for FramerReader<Fmt> {
    fn drop(&mut self) {
        // Signal to the other end that the framer is closed
        self.framer.is_closed.store(true, Ordering::Release);
        // Wake up writer
        self.framer.waiting_write.take().map(|w| w.wake());
    }
}

struct Deframer<Fmt: Format> {
    fmt: Fmt,
    max_queued: usize,
    is_closed: AtomicBool,
    waiting_read: AtomicWaker,
    waiting_write: AtomicWaker,
    incoming: Mutex<Incoming>,
}

#[derive(Debug)]
struct Incoming {
    unparsed: BVec,
    timeout: Option<Timer>,
    timeout_bytes: usize,
    pending_frame: Option<BVec>,
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
pub fn new_deframer<Fmt: Format>(
    fmt: Fmt,
    max_queued: usize,
) -> (DeframerWriter<Fmt>, DeframerReader<Fmt>) {
    let deframer = Arc::new(Deframer {
        fmt,
        max_queued,
        is_closed: AtomicBool::new(false),
        waiting_read: AtomicWaker::new(),
        waiting_write: AtomicWaker::new(),
        incoming: Mutex::new(Incoming {
            unparsed: BVec(Vec::new()),
            timeout: None,
            timeout_bytes: 0,
            pending_frame: None,
        }),
    });
    (DeframerWriter { deframer: deframer.clone() }, DeframerReader { deframer })
}

#[derive(Debug, PartialEq)]
/// Bytes ready by a DeframerReader.
pub enum ReadBytes {
    /// A frame to be processed.
    Framed(Vec<u8>),
    /// Garbage skipped between frames.
    Unframed(Vec<u8>),
}

impl<Fmt: Format> DeframerWriter<Fmt> {
    fn poll_write(
        &self,
        ctx: &mut Context<'_>,
        bytes: &[u8],
        lock: &mut MutexTicket<'_, Incoming>,
    ) -> Poll<Result<(), Error>> {
        let mut incoming = ready!(lock.poll(ctx));

        let result = if incoming.unparsed.0.len() >= self.deframer.max_queued {
            // Register for wake on read
            self.deframer.waiting_write.register(ctx.waker());

            Poll::Pending
        } else {
            // Copy bytes into buffer
            incoming.unparsed.0.extend_from_slice(bytes);

            // Stop waking up on read
            self.deframer.waiting_write.take();
            // Wake up the reader
            self.deframer.waiting_read.take().map(|w| w.wake());

            Poll::Ready(Ok(()))
        };

        // If the reader has already closed the deframer, then we fail even if
        // we managed to write bytes into the buffer. Note that wakers must be
        // registered before checking `is_closed` to prevent a reace.
        if self.deframer.is_closed.load(Ordering::Acquire) {
            Poll::Ready(Err(format_err!("Deframer closed during write")))
        } else {
            result
        }
    }

    /// Write some data into the deframer, to be deframed and read later.
    pub async fn write(&mut self, bytes: &[u8]) -> Result<(), Error> {
        let mut lock = MutexTicket::new(&self.deframer.incoming);
        poll_fn(|ctx| self.poll_write(ctx, bytes, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for DeframerWriter<Fmt> {
    fn drop(&mut self) {
        // Signal to the other end that the deframer is closed
        self.deframer.is_closed.store(true, Ordering::Release);
        // Wake up reader in case it was waiting for more bytes to be written
        self.deframer.waiting_read.take().map(|w| w.wake());
    }
}

fn deframe_step<Fmt: Format>(
    unparsed: &mut Vec<u8>,
    fmt: &Fmt,
    pending_frame: &mut Option<BVec>,
) -> Result<Option<ReadBytes>, Error> {
    let Deframed { frame, unframed_bytes, new_start_pos } = fmt.deframe(&unparsed)?;
    assert!(unframed_bytes <= new_start_pos);

    let mut unframed = None;
    if unframed_bytes != 0 {
        let mut unframed_vec = unparsed.split_off(unframed_bytes);
        std::mem::swap(&mut unframed_vec, unparsed);
        unframed = Some(ReadBytes::Unframed(unframed_vec));
    }
    if new_start_pos != unframed_bytes {
        unparsed.drain(..(new_start_pos - unframed_bytes));
    }

    let frame = frame.map(BVec);
    if unframed.is_some() {
        *pending_frame = frame;
        Ok(unframed)
    } else {
        Ok(frame.map(|f| ReadBytes::Framed(f.0)))
    }
}

impl<Fmt: Format> DeframerReader<Fmt> {
    fn poll_read(
        &self,
        ctx: &mut Context<'_>,
        lock: &mut MutexTicket<'_, Incoming>,
    ) -> Poll<Result<ReadBytes, Error>> {
        let mut incoming = ready!(lock.poll(ctx));
        let incoming = &mut *incoming;

        if let Some(pending_frame) = incoming.pending_frame.take() {
            // We don't have to unregister a waker because there's no
            // opportunity to register a waker between the last poll and this
            // one.
            // We also don't have to wake up the writer because we haven't
            // changed any of its pending conditions (we haven't modified the
            // length of the unparsed buffer).
            // Similarly, we don't have to cancel the timeout.

            Poll::Ready(Ok(ReadBytes::Framed(pending_frame.0)))
        } else if let Some(next_frame) =
            deframe_step(&mut incoming.unparsed.0, &self.deframer.fmt, &mut incoming.pending_frame)?
        {
            // We successfully read a frame. `deframe_step` may have also placed
            // a frame into `pending_frame`.

            // Stop waking up on write
            self.deframer.waiting_read.take();
            // Wake up the writer (if any) because a successful deframe
            // means that we removed bytes from `unparsed`.
            self.deframer.waiting_write.take().map(|w| w.wake());

            // Cancel the timeout (if any).
            drop(incoming.timeout.take());

            Poll::Ready(Ok(next_frame))
        } else {
            // No frames are available. Register for wake on write
            // unconditionally.
            self.deframer.waiting_read.register(ctx.waker());

            // If the writer has closed the framer, we're finally allowed to
            // fail. Note that wakers must be registered before checking
            // `is_closed` to prevent a race.
            if self.deframer.is_closed.load(Ordering::Acquire) {
                Poll::Ready(Err(format_err!("Framer closed during read")))
            } else if incoming.unparsed.0.len() > 0 {
                // Set a timeout for discarding the first byte from the unparsed
                // stream.
                if incoming.timeout.is_none() || incoming.unparsed.0.len() != incoming.timeout_bytes
                {
                    incoming.timeout = self
                        .deframer
                        .fmt
                        .deframe_timeout(incoming.unparsed.0.len() > 0)
                        .map(Timer::new);
                    incoming.timeout_bytes = incoming.unparsed.0.len();
                }

                // Check if we have a timeout and it polls as ready
                if incoming.timeout.as_mut().map(|timeout| timeout.poll_unpin(ctx).is_ready())
                    == Some(true)
                {
                    // When the timeout finishes, we treat the first unparsed
                    // bytes as unframed.
                    let mut unframed = incoming.unparsed.0.split_off(1);
                    std::mem::swap(&mut unframed, &mut incoming.unparsed.0);

                    // Stop waking up on write
                    self.deframer.waiting_read.take();
                    // Wake up the writer (if any) because a timeout means that
                    // we removed bytes from `unparsed`.
                    self.deframer.waiting_write.take().map(|w| w.wake());

                    Poll::Ready(Ok(ReadBytes::Unframed(unframed)))
                } else {
                    Poll::Pending
                }
            } else {
                // Cancel the timeout (if any) and wait to be woken up on write.
                drop(incoming.timeout.take());

                Poll::Pending
            }
        }
    }

    /// Read one frame from the deframer.
    pub async fn read(&mut self) -> Result<ReadBytes, Error> {
        let mut lock = MutexTicket::new(&self.deframer.incoming);
        poll_fn(|ctx| self.poll_read(ctx, &mut lock)).await
    }
}

impl<Fmt: Format> Drop for DeframerReader<Fmt> {
    fn drop(&mut self) {
        // Signal to the other end that the deframer is closed
        self.deframer.is_closed.store(true, Ordering::Release);
        // Wake up writer
        self.deframer.waiting_write.take().map(|w| w.wake());
    }
}

#[cfg(test)]
mod test {

    use super::*;
    use byteorder::WriteBytesExt;
    use crc::crc32;

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

        fn deframe_timeout(&self, have_pending_bytes: bool) -> Option<Duration> {
            if have_pending_bytes {
                Some(self.duration_per_byte)
            } else {
                None
            }
        }
    }

    fn join(mut a: Vec<u8>, mut b: Vec<u8>) -> Vec<u8> {
        a.append(&mut b);
        a
    }

    #[fuchsia_async::run(1, test)]
    async fn simple_frame_lossy_binary() -> Result<(), Error> {
        let (mut framer_writer, mut framer_reader) =
            new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
        framer_writer.write(&[1, 2, 3, 4]).await?;
        let (mut deframer_writer, mut deframer_reader) =
            new_deframer(LossyBinary::new(Duration::from_millis(100)), 1024);
        deframer_writer.write(framer_reader.read().await?.as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, ReadBytes::Framed(vec![1, 2, 3, 4]));
        framer_writer.write(&[5, 6, 7, 8]).await?;
        deframer_writer.write(framer_reader.read().await?.as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, ReadBytes::Framed(vec![5, 6, 7, 8]));
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn skip_junk_start_0() -> Result<(), Error> {
        let (mut framer_writer, mut framer_reader) =
            new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
        framer_writer.write(&[1, 2, 3, 4]).await?;
        let (mut deframer_writer, mut deframer_reader) =
            new_deframer(LossyBinary::new(Duration::from_millis(100)), 1024);
        deframer_writer.write(join(vec![0], framer_reader.read().await?).as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, ReadBytes::Unframed(vec![0]));
        assert_eq!(deframer_reader.read().await?, ReadBytes::Framed(vec![1, 2, 3, 4]));
        Ok(())
    }

    #[fuchsia_async::run(1, test)]
    async fn skip_junk_start_1() -> Result<(), Error> {
        let (mut framer_writer, mut framer_reader) =
            new_framer(LossyBinary::new(Duration::from_millis(100)), 1024);
        framer_writer.write(&[1, 2, 3, 4]).await?;
        let (mut deframer_writer, mut deframer_reader) =
            new_deframer(LossyBinary::new(Duration::from_millis(100)), 1024);
        deframer_writer.write(join(vec![1], framer_reader.read().await?).as_slice()).await?;
        assert_eq!(deframer_reader.read().await?, ReadBytes::Unframed(vec![1]));
        assert_eq!(deframer_reader.read().await?, ReadBytes::Framed(vec![1, 2, 3, 4]));
        Ok(())
    }
}
