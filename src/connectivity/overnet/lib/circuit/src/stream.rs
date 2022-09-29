// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::{Error, Result};
use crate::protocol;

use futures::channel::oneshot;
use std::collections::VecDeque;
use std::sync::Arc;
use std::sync::Mutex as SyncMutex;

/// Internal state of a stream. See `stream()`.
#[derive(Debug)]
struct State {
    /// The ring buffer itself.
    deque: VecDeque<u8>,
    /// How many bytes are readable. This is different from the length of the deque, as we may allow
    /// bytes to be in the deque that are "initialized but unavailable." Mostly that just
    /// accommodates a quirk of Rust's memory model; we have to initialize all bytes before we show
    /// them to the user, even if they're there just to be overwritten, and if we pop the bytes out
    /// of the deque Rust counts them as uninitialized again, so to avoid duplicating the
    /// initialization process we just leave the initialized-but-unwritten bytes in the deque.
    readable: usize,
    /// If the reader needs to sleep, it puts a oneshot sender here so it can be woken up again. It
    /// also lists how many bytes should be available before it should be woken up.
    notify_readable: Option<(oneshot::Sender<()>, usize)>,
    /// Whether this stream is closed. I.e. whether either the `Reader` or `Writer` has been dropped.
    closed: bool,
}

/// Read half of a stream. See `stream()`.
pub struct Reader(Arc<SyncMutex<State>>);

impl Reader {
    /// Read bytes from the stream.
    ///
    /// The reader will wait until there are *at least* `size` bytes to read, Then it will call the
    /// given callback with a slice containing all available bytes to read.
    ///
    /// If the callback processes data successfully, it should return `Ok` with a tuple containing
    /// a value of the user's choice, and the number of bytes used. If the number of bytes returned
    /// from the callback is less than what was available in the buffer, the unused bytes will
    /// appear at the start of the buffer for subsequent read calls. It is allowable to `peek` at
    /// the bytes in the buffer by returning a number of bytes read that is smaller than the number
    /// of bytes actually used.
    ///
    /// If the callback returns `Error::BufferTooShort` and the expected buffer value contained in
    /// the error is larger than the data that was provided, we will wait again until there are
    /// enough bytes to satisfy the error and then call the callback again. If the callback returns
    /// `Error::BufferTooShort` but the buffer should have been long enough according to the error,
    /// `Error::CallbackRejectedBuffer` is returned. Other errors from the callback are returned
    /// as-is from `read` itself.
    ///
    /// If there are no bytes available to read and the `Writer` for this stream has already been
    /// dropped, `read` returns `Error::ConnectionClosed`. If there are *not enough* bytes available
    /// to be read and the `Writer` has been dropped, `read` returns `Error::BufferTooSmall`. This
    /// is the only time `read` should return `Error::BufferTooSmall`.
    ///
    /// Panics if the callback returns a number of bytes greater than the size of the buffer.
    pub async fn read<F, U>(&self, mut size: usize, mut f: F) -> Result<U>
    where
        F: FnMut(&[u8]) -> Result<(U, usize)>,
    {
        loop {
            let receiver = {
                let mut state = self.0.lock().unwrap();

                if state.closed && size == 0 {
                    return Err(Error::ConnectionClosed);
                }

                if state.readable >= size {
                    let (first, _) = state.deque.as_slices();

                    let first = if first.len() >= size {
                        first
                    } else {
                        state.deque.make_contiguous();
                        state.deque.as_slices().0
                    };

                    debug_assert!(first.len() >= size);

                    let first = &first[..std::cmp::min(first.len(), state.readable)];
                    let (ret, consumed) = match f(first) {
                        Err(Error::BufferTooShort(s)) => {
                            if s < first.len() {
                                return Err(Error::CallbackRejectedBuffer(s, first.len()));
                            }

                            size = s;
                            continue;
                        }
                        other => other?,
                    };

                    if consumed > first.len() {
                        panic!("Read claimed to consume more bytes than it was given!");
                    }

                    state.readable -= consumed;
                    state.deque.drain(..consumed);
                    return Ok(ret);
                }

                if state.closed {
                    if state.readable > 0 {
                        return Err(Error::BufferTooShort(size));
                    } else {
                        return Err(Error::ConnectionClosed);
                    }
                }

                let (sender, receiver) = oneshot::channel();
                state.notify_readable = Some((sender, size));
                receiver
            };

            let _ = receiver.await;
        }
    }

    /// Read a protocol message from the stream. This is just a quick way to wire
    /// `ProtocolObject::try_from_bytes` in to `read`.
    pub async fn read_protocol_message<P: protocol::ProtocolMessage>(&self) -> Result<P> {
        self.read(P::MIN_SIZE, P::try_from_bytes).await
    }

    /// This writes the given protocol message to the stream at the *beginning* of the stream,
    /// meaning that it will be the next thing read off the stream.
    pub(crate) fn push_back_protocol_message<P: protocol::ProtocolMessage>(
        &self,
        message: &P,
    ) -> Result<()> {
        let size = message.byte_size();
        let mut state = self.0.lock().unwrap();
        let readable = state.readable;
        state.deque.resize(readable + size, 0);
        state.deque.rotate_right(size);
        let (first, _) = state.deque.as_mut_slices();

        let mut first = if first.len() >= size {
            first
        } else {
            state.deque.make_contiguous();
            state.deque.as_mut_slices().0
        };

        let got = message.write_bytes(&mut first)?;
        debug_assert!(got == size);
        state.readable += size;

        if let Some((sender, size)) = state.notify_readable.take() {
            if size <= state.readable {
                let _ = sender.send(());
            } else {
                state.notify_readable = Some((sender, size));
            }
        }

        Ok(())
    }
}

impl Drop for Reader {
    fn drop(&mut self) {
        let mut state = self.0.lock().unwrap();
        state.closed = true;
    }
}

/// Write half of a stream. See `stream()`.
pub struct Writer(Arc<SyncMutex<State>>);

impl Writer {
    /// Write data to this stream.
    ///
    /// Space for `size` bytes is allocated in the stream immediately, and then the callback is
    /// invoked with a mutable slice to that region so that it may populate it. The slice given to
    /// the callback *may* be larger than requested but will never be smaller.
    ///
    /// The callback should return `Ok` with the number of bytes actually written, which may be less
    /// than `size`. If the callback returns an error, that error is returned from `write` as-is.
    /// Note that we do not specially process `Error::BufferTooSmall` as with `Reader::read`.
    ///
    /// Panics if the callback returns a number of bytes greater than the size of the buffer.
    pub fn write<F>(&self, size: usize, f: F) -> Result<()>
    where
        F: FnOnce(&mut [u8]) -> Result<usize>,
    {
        let mut state = self.0.lock().unwrap();

        if state.closed {
            return Err(Error::ConnectionClosed);
        }

        let total_size = state.readable + size;

        if state.deque.len() < total_size {
            let total_size = std::cmp::max(total_size, state.deque.capacity());
            state.deque.resize(total_size, 0);
        }

        let readable = state.readable;
        let (first, second) = state.deque.as_mut_slices();

        let slice = if first.len() > readable {
            &mut first[readable..]
        } else {
            &mut second[(readable - first.len())..]
        };

        let slice = if slice.len() >= size {
            slice
        } else {
            state.deque.make_contiguous();
            &mut state.deque.as_mut_slices().0[readable..]
        };

        debug_assert!(slice.len() >= size);
        let size = f(slice)?;

        if size > slice.len() {
            panic!("Write claimed to produce more bytes than buffer had space for!");
        }

        state.readable += size;

        if let Some((sender, size)) = state.notify_readable.take() {
            if size <= state.readable {
                let _ = sender.send(());
            } else {
                state.notify_readable = Some((sender, size));
            }
        }

        Ok(())
    }

    /// Write a protocol message to the stream. This is just a quick way to wire
    /// `ProtocolObject::write_bytes` in to `write`.
    pub fn write_protocol_message<P: protocol::ProtocolMessage>(&self, message: &P) -> Result<()> {
        self.write(message.byte_size(), |mut buf| message.write_bytes(&mut buf))
    }
}

impl Drop for Writer {
    fn drop(&mut self) {
        let mut state = self.0.lock().unwrap();
        state.closed = true;
        state.notify_readable.take().map(|x| {
            let _ = x.0.send(());
        });
    }
}

/// Creates a unidirectional stream of bytes.
///
/// The `Reader` and `Writer` share an expanding ring buffer. This allows sending bytes between
/// tasks with minimal extra allocations or copies.
pub fn stream() -> (Reader, Writer) {
    let reader = Arc::new(SyncMutex::new(State {
        deque: VecDeque::new(),
        readable: 0,
        notify_readable: None,
        closed: false,
    }));
    let writer = Arc::clone(&reader);

    (Reader(reader), Writer(writer))
}

#[cfg(test)]
mod test {
    use super::*;

    impl protocol::ProtocolMessage for [u8; 4] {
        const MIN_SIZE: usize = 4;
        fn byte_size(&self) -> usize {
            4
        }

        fn write_bytes<W: std::io::Write>(&self, out: &mut W) -> Result<usize> {
            out.write_all(self)?;
            Ok(4)
        }

        fn try_from_bytes(bytes: &[u8]) -> Result<(Self, usize)> {
            if bytes.len() < 4 {
                return Err(Error::BufferTooShort(4));
            }

            Ok((bytes[..4].try_into().unwrap(), 4))
        }
    }

    #[fuchsia::test]
    async fn stream_test() {
        let (reader, writer) = stream();
        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let got = reader.read(4, |buf| Ok((buf[..4].to_vec(), 4))).await.unwrap();

        assert_eq!(vec![1, 2, 3, 4], got);

        writer
            .write(2, |buf| {
                buf[..2].copy_from_slice(&[9, 10]);
                Ok(2)
            })
            .unwrap();

        let got = reader.read(6, |buf| Ok((buf[..6].to_vec(), 6))).await.unwrap();

        assert_eq!(vec![5, 6, 7, 8, 9, 10], got);
    }

    #[fuchsia::test]
    async fn push_back_test() {
        let (reader, writer) = stream();
        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let got = reader.read(4, |buf| Ok((buf[..4].to_vec(), 4))).await.unwrap();

        assert_eq!(vec![1, 2, 3, 4], got);

        reader.push_back_protocol_message(&[4, 3, 2, 1]).unwrap();

        writer
            .write(2, |buf| {
                buf[..2].copy_from_slice(&[9, 10]);
                Ok(2)
            })
            .unwrap();

        let got = reader.read(10, |buf| Ok((buf[..10].to_vec(), 6))).await.unwrap();

        assert_eq!(vec![4, 3, 2, 1, 5, 6, 7, 8, 9, 10], got);
    }

    #[fuchsia::test]
    async fn writer_sees_close() {
        let (reader, writer) = stream();
        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let got = reader.read(4, |buf| Ok((buf[..4].to_vec(), 4))).await.unwrap();

        assert_eq!(vec![1, 2, 3, 4], got);

        std::mem::drop(reader);

        assert!(matches!(
            writer.write(2, |buf| {
                buf[..2].copy_from_slice(&[9, 10]);
                Ok(2)
            }),
            Err(Error::ConnectionClosed)
        ));
    }

    #[fuchsia::test]
    async fn reader_sees_closed() {
        let (reader, writer) = stream();
        writer
            .write(8, |buf| {
                buf[..8].copy_from_slice(&[1, 2, 3, 4, 5, 6, 7, 8]);
                Ok(8)
            })
            .unwrap();

        let got = reader.read(4, |buf| Ok((buf[..4].to_vec(), 4))).await.unwrap();

        assert_eq!(vec![1, 2, 3, 4], got);

        writer
            .write(2, |buf| {
                buf[..2].copy_from_slice(&[9, 10]);
                Ok(2)
            })
            .unwrap();

        std::mem::drop(writer);

        assert!(matches!(reader.read(7, |_| Ok(((), 1))).await, Err(Error::BufferTooShort(7))));

        let got = reader.read(6, |buf| Ok((buf[..6].to_vec(), 6))).await.unwrap();

        assert_eq!(vec![5, 6, 7, 8, 9, 10], got);
        assert!(matches!(reader.read(1, |_| Ok(((), 1))).await, Err(Error::ConnectionClosed)));
    }

    #[fuchsia::test]
    async fn reader_buffer_too_short() {
        let (reader, writer) = stream();
        let (sender, receiver) = oneshot::channel();
        let mut sender = Some(sender);

        let reader_task = async move {
            let got = reader
                .read(1, |buf| {
                    if buf.len() != 4 {
                        sender.take().unwrap().send(buf.len()).unwrap();
                        Err(Error::BufferTooShort(4))
                    } else {
                        Ok((buf[..4].to_vec(), 4))
                    }
                })
                .await
                .unwrap();
            assert_eq!(vec![1, 2, 3, 4], got);
        };

        let writer_task = async move {
            writer
                .write(2, |buf| {
                    buf[..2].copy_from_slice(&[1, 2]);
                    Ok(2)
                })
                .unwrap();

            assert_eq!(2, receiver.await.unwrap());

            writer
                .write(2, |buf| {
                    buf[..2].copy_from_slice(&[3, 4]);
                    Ok(2)
                })
                .unwrap();
        };

        futures::pin_mut!(reader_task);
        futures::pin_mut!(writer_task);

        futures::future::join(reader_task, writer_task).await;
    }
}
