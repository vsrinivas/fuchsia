// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Temporary Futures extensions used during the transition from 0.2 -> 0.3.
//! These SHOUD NOT be used for new code. Instead, use the corresponding 0.3
//! functions or `async_await`-based utilities.

#![allow(missing_docs)]

use std::marker::Unpin;
use std::mem::{self, PinMut};
use futures::task::{self, Poll};
use futures::future::{Future, FutureExt};
use futures::stream::{Stream, StreamExt};
use futures::io::{self, AsyncRead, AsyncWrite};

pub trait TempStreamExt: Stream + Sized {
    fn first_elem(self) -> FirstElem<Self> {
        FirstElem { stream: self }
    }
    fn try_into_future<T, E>(self) -> TryIntoFuture<Self>
        where Self: Stream<Item = Result<T, E>> + Unpin,
    {
        TryIntoFuture { stream: Some(self) }
    }
}

impl<T: Stream + Sized> TempStreamExt for T {}

pub struct FirstElem<St> { stream: St }

impl<St> FirstElem<St> {
    // Safety: `FirstElem` is `Unpin` iff `St` is `Unpin`.
    unsafe_pinned!(stream: St);
}

impl<St: Stream> Future for FirstElem<St> {
    type Output = Option<St::Item>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context)
        -> Poll<Self::Output>
    {
        self.stream().poll_next(cx)
    }
}

pub struct TryIntoFuture<St> {
    stream: Option<St>,
}

impl<St> Unpin for TryIntoFuture<St> {}

impl<T, E, St: Stream<Item = Result<T, E>> + Unpin> Future for TryIntoFuture<St> {
    type Output = Result<(Option<T>, St), E>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context)
        -> Poll<Self::Output>
    {
        let res = ready!(self.stream.as_mut().unwrap().poll_next_unpin(cx));
        Poll::Ready(match res {
            Some(Ok(elem)) =>
                Ok((Some(elem), self.stream.take().unwrap())),
            None =>
                Ok((None, self.stream.take().unwrap())),
            Some(Err(e)) => Err(e),
        })
    }
}

pub trait TempAsyncWriteExt: AsyncWrite + Sized {
	fn write_all<T: AsRef<[u8]>>(self, buf: T) -> WriteAll<Self, T> {
		write_all(self, buf)
	}
}

impl<T: AsyncWrite + Sized> TempAsyncWriteExt for T {}

pub struct WriteAll<A, T> {
    state: WriteState<A, T>,
}

impl<A, T> Unpin for WriteAll<A, T> {}

enum WriteState<A, T> {
    Writing {
        a: A,
        buf: T,
        pos: usize,
    },
    Empty,
}

pub fn write_all<A, T>(a: A, buf: T) -> WriteAll<A, T>
    where A: AsyncWrite,
          T: AsRef<[u8]>,
{
    WriteAll {
        state: WriteState::Writing {
            a: a,
            buf: buf,
            pos: 0,
        },
    }
}

fn zero_write() -> io::Error {
    io::Error::new(io::ErrorKind::WriteZero, "zero-length write")
}

impl<A, T> Future for WriteAll<A, T>
    where A: AsyncWrite,
          T: AsRef<[u8]>,
{
    type Output = io::Result<(A, T)>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
        match self.state {
            WriteState::Writing { ref mut a, ref buf, ref mut pos } => {
                let buf = buf.as_ref();
                while *pos < buf.len() {
                    let n = try_ready!(a.poll_write(cx, &buf[*pos..]));
                    *pos += n;
                    if n == 0 {
                        return Poll::Ready(Err(zero_write()))
                    }
                }
            }
            WriteState::Empty => panic!("poll a WriteAll after it's done"),
        }

        match mem::replace(&mut self.state, WriteState::Empty) {
            WriteState::Writing { a, buf, .. } => Poll::Ready(Ok((a, buf).into())),
            WriteState::Empty => panic!(),
        }
    }
}

pub trait TempAsyncReadExt: AsyncRead + Sized {
    fn read_to_end(self, buf: Vec<u8>) -> ReadToEnd<Self> {
		read_to_end(self, buf)
    }
}

impl<T: AsyncRead + Sized> TempAsyncReadExt for T {}

pub struct ReadToEnd<A> {
    state: State<A>,
}

impl<A> Unpin for ReadToEnd<A> {}

enum State<A> {
    Reading {
        a: A,
        buf: Vec<u8>,
    },
    Empty,
}

pub fn read_to_end<A>(a: A, buf: Vec<u8>) -> ReadToEnd<A>
    where A: AsyncRead,
{
    ReadToEnd {
        state: State::Reading {
            a,
            buf,
        }
    }
}

struct Guard<'a> { buf: &'a mut Vec<u8>, len: usize }

impl<'a> Drop for Guard<'a> {
    fn drop(&mut self) {
        unsafe { self.buf.set_len(self.len); }
    }
}

// This uses an adaptive system to extend the vector when it fills. We want to
// avoid paying to allocate and zero a huge chunk of memory if the reader only
// has 4 bytes while still making large reads if the reader does have a ton
// of data to return. Simply tacking on an extra DEFAULT_BUF_SIZE space every
// time is 4,500 times (!) slower than this if the reader has a very small
// amount of data to return.
//
// Because we're extending the buffer with uninitialized data for trusted
// readers, we need to make sure to truncate that if any of this panics.
fn read_to_end_internal<R: AsyncRead>(r: &mut R, cx: &mut task::Context, buf: &mut Vec<u8>)
    -> Poll<io::Result<usize>>
{
    let start_len = buf.len();
    let mut g = Guard { len: buf.len(), buf: buf };
    let ret;
    loop {
        if g.len == g.buf.len() {
            unsafe {
                g.buf.reserve(32);
                let capacity = g.buf.capacity();
                g.buf.set_len(capacity);
                r.initializer().initialize(&mut g.buf[g.len..]);
            }
        }

        match ready!(r.poll_read(cx, &mut g.buf[g.len..])) {
            Ok(0) => {
                ret = Poll::Ready(Ok(g.len - start_len));
                break;
            }
            Ok(n) => g.len += n,
            Err(e) => {
                ret = Poll::Ready(Err(e));
                break;
            }
        }
    }

    ret
}

impl<A> Future for ReadToEnd<A>
    where A: AsyncRead,
{
    type Output = io::Result<(A, Vec<u8>)>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
		let this = &mut *self;
        match this.state {
            State::Reading { ref mut a, ref mut buf } => {
                // If we get `Ok`, then we know the stream hit EOF and we're done. If we
                // hit "would block" then all the read data so far is in our buffer, and
                // otherwise we propagate errors
                try_ready!(read_to_end_internal(a, cx, buf));
            },
            State::Empty => panic!("poll ReadToEnd after it's done"),
        }

        match mem::replace(&mut this.state, State::Empty) {
            State::Reading { a, buf } => Poll::Ready(Ok((a, buf).into())),
            State::Empty => unreachable!(),
        }
    }
}

pub trait TempFutureExt: Future + Sized {
    fn left_future<B>(self) -> Either<Self, B> {
        Either::Left(self)
    }

    fn right_future<A>(self) -> Either<A, Self> {
        Either::Right(self)
    }

    fn select<B>(self, b: B) -> Select<Self, B> {
        Select { a: self, b }
    }

    fn select_unpin<B>(self, b: B) -> SelectUnpin<Self, B>
    where
        Self: Unpin,
        B: Unpin,
    {
        SelectUnpin { a: Some(self), b: Some(b) }
    }
}

impl<T: Future + Sized> TempFutureExt for T {}

pub enum Either<A, B> {
    Left(A),
    Right(B),
}

impl<A, B> Either<A, B> {
    pub fn either<T>(
        self,
        lf: impl FnOnce(A) -> T,
        rf: impl FnOnce(B) -> T,
    ) -> T {
        match self {
            Either::Left(a) => (lf)(a),
            Either::Right(b) => (rf)(b),
        }
    }
}

impl<A: Future, B: Future<Output = A::Output>> Future for Either<A, B> {
    type Output = A::Output;
    fn poll(self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
        unsafe {
            // Safety: neither child future is ever moved
            match PinMut::get_mut_unchecked(self) {
                Either::Left(a) => PinMut::new_unchecked(a).poll(cx),
                Either::Right(b) => PinMut::new_unchecked(b).poll(cx),
            }
        }
    }
}

pub struct Select<A, B> {
    a: A,
    b: B,
}

impl<A, B> Select<A, B> {
    unsafe_pinned!(a: A);
    unsafe_pinned!(b: B);
}

impl<A: Future, B: Future> Future for Select<A, B> {
    type Output = Either<A::Output, B::Output>;
    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
        if let Poll::Ready(a) = self.a().poll(cx) {
            return Poll::Ready(Either::Left(a));
        }
        if let Poll::Ready(b) = self.b().poll(cx) {
            return Poll::Ready(Either::Right(b));
        }
        Poll::Pending
    }
}

pub struct SelectUnpin<A, B> {
    a: Option<A>,
    b: Option<B>,
}

impl<A: Future + Unpin, B: Future + Unpin> Future for SelectUnpin<A, B> {
    type Output = Either<(A::Output, B), (A, B::Output)>;
    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
        let this = &mut *self;
        if let Poll::Ready(a) = this.a.as_mut().unwrap().poll_unpin(cx) {
            return Poll::Ready(Either::Left((a, this.b.take().unwrap())));
        }
        if let Poll::Ready(b) = this.b.as_mut().unwrap().poll_unpin(cx) {
            return Poll::Ready(Either::Right((this.a.take().unwrap(), b)));
        }
        Poll::Pending
    }
}

/// A future which will copy all data from a reader into a writer.
///
/// Created by the [`copy_into`] function, this future will resolve to the number of
/// bytes copied or an error if one happens.
///
/// [`copy_into`]: fn.copy_into.html
#[derive(Debug)]
pub struct CopyInto<R, W> {
    reader: Option<R>,
    read_done: bool,
    writer: Option<W>,
    pos: usize,
    cap: usize,
    amt: u64,
    buf: Box<[u8]>,
}

impl<R, W> Unpin for CopyInto<R, W> {}

pub fn copy_into<R, W>(reader: R, writer: W) -> CopyInto<R, W> {
    CopyInto {
        reader: Some(reader),
        read_done: false,
        writer: Some(writer),
        amt: 0,
        pos: 0,
        cap: 0,
        buf: Box::new([0; 2048]),
    }
}

impl<R, W> Future for CopyInto<R, W>
    where R: AsyncRead,
          W: AsyncWrite,
{
    type Output = io::Result<(u64, R, W)>;

    fn poll(mut self: PinMut<Self>, cx: &mut task::Context) -> Poll<Self::Output> {
        let this = &mut *self;
        loop {
            // If our buffer is empty, then we need to read some data to
            // continue.
            if this.pos == this.cap && !this.read_done {
                let reader = this.reader.as_mut().unwrap();
                let n = ready!(reader.poll_read(cx, &mut this.buf)?);
                if n == 0 {
                    this.read_done = true;
                } else {
                    this.pos = 0;
                    this.cap = n;
                }
            }

            // If our buffer has some data, let's write it out!
            while this.pos < this.cap {
                let writer = this.writer.as_mut().unwrap();
                let i = ready!(writer.poll_write(cx, &this.buf[this.pos..this.cap])?);
                if i == 0 {
                    return Poll::Ready(Err(
                        io::Error::new(
                            io::ErrorKind::WriteZero,
                           "write zero byte into writer")));
                } else {
                    this.pos += i;
                    this.amt += i as u64;
                }
            }

            // If we've written al the data and we've seen EOF, flush out the
            // data and finish the transfer.
            // done with the entire transfer.
            if this.pos == this.cap && this.read_done {
                ready!(this.writer.as_mut().unwrap().poll_flush(cx)?);
                let reader = this.reader.take().unwrap();
                let writer = this.writer.take().unwrap();
                return Poll::Ready(Ok((this.amt, reader, writer)))
            }
        }
    }
}
