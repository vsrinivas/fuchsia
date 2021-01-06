// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, Error};
use async_std::{
    fs::{File, OpenOptions},
    io::{stdin, stdout},
};
use futures::{
    lock::{Mutex, MutexGuard, MutexLockFuture},
    prelude::*,
};
use overnet_core::Router;
use serial_link::{
    descriptor::{CharacterWidth, Config, Descriptor, FlowControl, Parity, StopWidth},
    run::{run, Role},
};
use std::{
    os::unix::io::{AsRawFd, FromRawFd},
    pin::Pin,
    sync::Weak,
    task::{Context, Poll},
};
use termios::{
    cfsetspeed,
    os::target::{B115200, B230400, B38400, B57600, CRTSCTS},
    tcsetattr, Termios, B110, B1200, B134, B150, B1800, B19200, B200, B2400, B300, B4800, B50,
    B600, B75, B9600, BRKINT, CS5, CS6, CS7, CS8, CSIZE, CSTOPB, ECHO, ECHONL, ICANON, ICRNL,
    IEXTEN, IGNBRK, IGNCR, INLCR, ISIG, ISTRIP, IXON, OPOST, PARENB, PARMRK, PARODD, TCSAFLUSH,
    VMIN, VTIME,
};

pub async fn run_serial_link_handlers(
    router: Weak<Router>,
    descriptors: &str,
    output_sink: impl AsyncWrite + Unpin + Send,
) -> Result<(), Error> {
    let output_sink = &Mutex::new(output_sink);
    futures::stream::iter(serial_link::descriptor::parse(&descriptors).await?.into_iter())
        .map(Ok)
        .try_for_each_concurrent(None, |desc| {
            let router = router.clone();
            async move {
                let error = match desc {
                    Descriptor::StdioPipe => {
                        run(
                            Role::Client,
                            stdin(),
                            stdout(),
                            router,
                            OutputSink::new(output_sink),
                            Some(&desc),
                        )
                        .await
                    }
                    Descriptor::Device { ref path, config } => {
                        let f = OpenOptions::new()
                            .read(true)
                            .write(true)
                            .create(false)
                            .open(path)
                            .await?;
                        apply_config(&f, config)?;
                        // async-std only allows reads or writes at a given time on an FD,
                        // but we'll need to do both. dup the fd here and use one for reading,
                        // and the other for writing.
                        let f_dup = nix::unistd::dup(f.as_raw_fd())?;
                        let f_read = unsafe { File::from_raw_fd(f_dup) };
                        let f_write = f;
                        run(
                            Role::Client,
                            f_read,
                            f_write,
                            router,
                            OutputSink::new(output_sink),
                            Some(&desc),
                        )
                        .await
                    }
                };
                log::warn!("serial loop completed with failure: {:?}", error);
                Ok(())
            }
        })
        .await
}

pub fn apply_config(f: &File, config: Config) -> Result<(), Error> {
    let baud = match config.baud_rate {
        50 => B50,
        75 => B75,
        110 => B110,
        134 => B134,
        150 => B150,
        200 => B200,
        300 => B300,
        600 => B600,
        1200 => B1200,
        1800 => B1800,
        2400 => B2400,
        4800 => B4800,
        9600 => B9600,
        19200 => B19200,
        38400 => B38400,
        57600 => B57600,
        115200 => B115200,
        230400 => B230400,
        baud => bail!("Unsupported baud rate: {}", baud),
    };
    let cs = match config.character_width {
        CharacterWidth::Bits5 => CS5,
        CharacterWidth::Bits6 => CS6,
        CharacterWidth::Bits7 => CS7,
        CharacterWidth::Bits8 => CS8,
    };
    let sb = match config.stop_width {
        StopWidth::Bits1 => 0,
        StopWidth::Bits2 => CSTOPB,
    };
    let p = match config.parity {
        Parity::None => 0,
        Parity::Even => PARENB,
        Parity::Odd => PARENB | PARODD,
    };
    let fc = match config.control_flow {
        FlowControl::None => 0,
        FlowControl::CtsRts => CRTSCTS,
    };

    let f = f.as_raw_fd();
    let t = &mut Termios::from_fd(f)?;
    cfsetspeed(t, baud)?;
    t.c_iflag &= !(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_oflag &= !OPOST;
    t.c_lflag &= !(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= !(CSIZE | CRTSCTS | CSTOPB | PARENB | PARODD);
    t.c_cflag |= cs | sb | p | fc;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(f, TCSAFLUSH, t)?;

    Ok(())
}

struct OutputSink<'a, Base> {
    mutex: &'a Mutex<Base>,
    state: WriteState<'a, Base>,
}

enum WriteState<'a, Base> {
    Idle,
    Locking(MutexLockFuture<'a, Base>),
    Writing(MutexGuard<'a, Base>),
}

impl<'a, Base: Unpin> OutputSink<'a, Base> {
    fn new(mutex: &'a Mutex<Base>) -> OutputSink<'a, Base> {
        OutputSink { mutex, state: WriteState::Idle }
    }

    fn poll_locked_op<R>(
        mut self: Pin<&mut Self>,
        ctx: &mut Context<'_>,
        f: impl Fn(Pin<&mut Base>, &mut Context<'_>) -> Poll<R>,
    ) -> Poll<R> {
        match std::mem::replace(&mut self.state, WriteState::Idle) {
            WriteState::Idle => {
                let l = self.mutex.lock();
                self.continue_locking(ctx, f, l)
            }
            WriteState::Locking(l) => self.continue_locking(ctx, f, l),
            WriteState::Writing(g) => self.continue_writing(ctx, f, g),
        }
    }

    fn continue_locking<R>(
        mut self: Pin<&mut Self>,
        ctx: &mut Context<'_>,
        f: impl Fn(Pin<&mut Base>, &mut Context<'_>) -> Poll<R>,
        mut l: MutexLockFuture<'a, Base>,
    ) -> Poll<R> {
        match l.poll_unpin(ctx) {
            Poll::Pending => {
                self.state = WriteState::Locking(l);
                Poll::Pending
            }
            Poll::Ready(g) => self.continue_writing(ctx, f, g),
        }
    }

    fn continue_writing<R>(
        mut self: Pin<&mut Self>,
        ctx: &mut Context<'_>,
        f: impl Fn(Pin<&mut Base>, &mut Context<'_>) -> Poll<R>,
        mut g: MutexGuard<'a, Base>,
    ) -> Poll<R> {
        match f(Pin::new(&mut *g), ctx) {
            Poll::Pending => {
                self.state = WriteState::Writing(g);
                Poll::Pending
            }
            Poll::Ready(r) => Poll::Ready(r),
        }
    }
}

impl<'a, Base: AsyncWrite + Unpin> AsyncWrite for OutputSink<'a, Base> {
    fn poll_write(
        self: Pin<&mut Self>,
        ctx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<Result<usize, std::io::Error>> {
        self.poll_locked_op(ctx, |b, ctx| b.poll_write(ctx, buf))
    }

    fn poll_flush(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Result<(), std::io::Error>> {
        self.poll_locked_op(ctx, |b, ctx| b.poll_flush(ctx))
    }

    fn poll_close(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Result<(), std::io::Error>> {
        self.poll_locked_op(ctx, |b, ctx| b.poll_close(ctx))
    }
}
