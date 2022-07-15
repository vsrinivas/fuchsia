// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate provides helper functions to collect test diagnostics.

use {
    futures::{
        channel::mpsc,
        io::AsyncRead,
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    parking_lot::Mutex,
    std::{cell::RefCell, io::Write, pin::Pin, sync::Arc},
};

pub use crate::diagnostics::LogStream;

mod diagnostics;

thread_local! {
    static BUFFER: RefCell<[u8; 2048]> = RefCell::new([0; 2048]);
}

/// Future that executes a function when bytes are available on a socket.
pub struct SocketReadFut<'a, T, F>
where
    F: FnMut(Option<&[u8]>) -> Result<T, std::io::Error> + Unpin,
{
    socket: &'a mut fidl::AsyncSocket,
    on_read_fn: F,
}

impl<'a, T, F> SocketReadFut<'a, T, F>
where
    F: FnMut(Option<&[u8]>) -> Result<T, std::io::Error> + Unpin,
{
    pub fn new(socket: &'a mut fidl::AsyncSocket, on_read_fn: F) -> Self {
        Self { socket, on_read_fn }
    }
}

impl<'a, T, F> Future for SocketReadFut<'a, T, F>
where
    F: FnMut(Option<&[u8]>) -> Result<T, std::io::Error> + Unpin,
{
    type Output = Result<T, std::io::Error>;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        BUFFER.with(|b| {
            let mut b = b.borrow_mut();
            let mut_self = self.get_mut();
            let len = ready!(Pin::new(&mut mut_self.socket).poll_read(cx, &mut *b)?);
            match len {
                0 => Poll::Ready((mut_self.on_read_fn)(None)),
                l => Poll::Ready((mut_self.on_read_fn)(Some(&b[..l]))),
            }
        })
    }
}

pub async fn collect_and_send_string_output(
    socket: fidl::Socket,
    mut sender: mpsc::Sender<String>,
) -> Result<(), anyhow::Error> {
    let mut async_socket = fidl::AsyncSocket::from_socket(socket)?;
    loop {
        let maybe_string = SocketReadFut::new(&mut async_socket, |maybe_buf| {
            Ok(maybe_buf.map(|buf| String::from_utf8_lossy(buf).into()))
        })
        .await?;
        match maybe_string {
            Some(string) => sender.send(string).await?,
            None => return Ok(()),
        }
    }
}

/// A writer that buffers content in memory for some duration before flushing the contents to
/// an inner writer. After the duration elapses, any new bytes are written immediately to the
/// inner writer. Calling flush() also immediately flushes the contents.
/// Errors that occur when flushing on timeout are returned at the next write() or flush()
/// call. Therefore, callers should make sure to call flush before the StdoutBuffer goes out of
/// scope.
pub struct StdoutBuffer<W: Write + Send + 'static> {
    inner: Arc<Mutex<StdoutBufferInner<W>>>,
    _timer: fuchsia_async::Task<()>,
}

impl<W: Write + Send + 'static> StdoutBuffer<W> {
    /// Crates new StdoutBuffer and starts the timer on log buffering.
    /// `duration`: Buffers log for this duration or till done() is called.
    /// `sender`: Channel to send logs on.
    /// `max_capacity`: Flush log if buffer size exceeds this value. This will not cancel the timer
    /// and all the logs would be flushed once timer expires.
    pub fn new(duration: std::time::Duration, writer: W, max_capacity: usize) -> Self {
        let (inner, timer) = StdoutBufferInner::new(duration, writer, max_capacity);
        Self { inner, _timer: timer }
    }
}

impl<W: Write + Send + 'static> Write for StdoutBuffer<W> {
    fn flush(&mut self) -> Result<(), std::io::Error> {
        self.inner.lock().flush()
    }

    fn write(&mut self, bytes: &[u8]) -> Result<usize, std::io::Error> {
        self.inner.lock().write(bytes)
    }
}

struct StdoutBufferInner<W: Write + Send + 'static> {
    writer: W,
    /// Whether to buffer logs or not.
    buffer: Option<Vec<u8>>,
    stop_buffer_error: Option<std::io::Error>,
    max_capacity: usize,
}

impl<W: Write + Send + 'static> StdoutBufferInner<W> {
    fn new(
        duration: std::time::Duration,
        writer: W,
        max_capacity: usize,
    ) -> (Arc<Mutex<Self>>, fuchsia_async::Task<()>) {
        let new_self = Arc::new(Mutex::new(StdoutBufferInner {
            writer,
            buffer: Some(Vec::with_capacity(max_capacity)),
            stop_buffer_error: None,
            max_capacity,
        }));

        let timer = fuchsia_async::Timer::new(duration);
        let log_buffer = Arc::downgrade(&new_self);
        let f = async move {
            timer.await;
            if let Some(log_buffer) = log_buffer.upgrade() {
                log_buffer.lock().stop_buffering();
            }
        };

        (new_self, fuchsia_async::Task::spawn(f))
    }

    fn stop_buffering(&mut self) {
        if let Some(buf) = self.buffer.take() {
            if let Err(e) = self.writer.write_all(&buf) {
                self.stop_buffer_error = Some(e);
            }
        }
    }
}

impl<W: Write + Send + 'static> Write for StdoutBufferInner<W> {
    fn flush(&mut self) -> Result<(), std::io::Error> {
        self.stop_buffering();
        match self.stop_buffer_error.take() {
            Some(e) => Err(e),
            None => self.writer.flush(),
        }
    }

    fn write(&mut self, bytes: &[u8]) -> Result<usize, std::io::Error> {
        if let Some(e) = self.stop_buffer_error.take() {
            return Err(e);
        }
        match self.buffer.as_mut() {
            None => self.writer.write(bytes),
            Some(buf) if buf.len() + bytes.len() > self.max_capacity => {
                self.writer.write_all(&buf)?;
                buf.truncate(0);
                self.writer.write(bytes)
            }
            Some(buf) => Write::write(buf, bytes),
        }
    }
}

impl<W: Write + Send + 'static> Drop for StdoutBufferInner<W> {
    fn drop(&mut self) {
        let _ = self.flush();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::HandleBased;
    use futures::StreamExt;
    use pretty_assertions::assert_eq;

    #[fuchsia_async::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/104006) re-enable this test when de-flaked
    async fn collect_test_stdout() {
        let (sock_server, sock_client) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("Failed while creating socket");

        let (sender, mut recv) = mpsc::channel(1);

        let fut =
            fuchsia_async::Task::spawn(collect_and_send_string_output(sock_client, sender.into()));

        sock_server.write(b"test message 1").expect("Can't write msg to socket");
        sock_server.write(b"test message 2").expect("Can't write msg to socket");
        sock_server.write(b"test message 3").expect("Can't write msg to socket");

        let mut msg = recv.next().await;

        assert_eq!(msg, Some("test message 1test message 2test message 3".into()));

        // can receive messages multiple times
        sock_server.write(b"test message 4").expect("Can't write msg to socket");
        msg = recv.next().await;

        assert_eq!(msg, Some("test message 4".into()));

        // messages can be read after socket server is closed.
        sock_server.write(b"test message 5").expect("Can't write msg to socket");
        sock_server.into_handle(); // this will drop this handle and close it.
        fut.await.expect("log collection should not fail");

        msg = recv.next().await;

        assert_eq!(msg, Some("test message 5".into()));

        // socket was closed, this should return None
        msg = recv.next().await;
        assert_eq!(msg, None);
    }

    /// Host side executor doesn't have a fake timer, so these tests only run on device for now.
    #[cfg(target_os = "fuchsia")]
    mod stdout {
        use {
            super::*, fuchsia_async::TestExecutor, fuchsia_zircon::DurationNum,
            pretty_assertions::assert_eq, std::ops::Add,
        };

        struct MutexBytes(Arc<Mutex<Vec<u8>>>);

        impl Write for MutexBytes {
            fn flush(&mut self) -> Result<(), std::io::Error> {
                Write::flush(&mut *self.0.lock())
            }

            fn write(&mut self, bytes: &[u8]) -> Result<usize, std::io::Error> {
                Write::write(&mut *self.0.lock(), bytes)
            }
        }

        #[test]
        fn log_buffer_without_timeout() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let output = Arc::new(Mutex::new(vec![]));
            let writer = MutexBytes(output.clone());
            let (log_buffer, mut timeout_task) =
                StdoutBufferInner::new(std::time::Duration::from_secs(5), writer, 100);

            write!(log_buffer.lock(), "message1").expect("write message");
            assert_eq!(*output.lock(), b"");
            write!(log_buffer.lock(), "message2").expect("write message");
            assert_eq!(*output.lock(), b"");

            assert_eq!(executor.run_until_stalled(&mut timeout_task), Poll::Pending);
            assert_eq!(*output.lock(), b"");

            log_buffer.lock().flush().expect("flush buffer");
            assert_eq!(*output.lock(), b"message1message2");
        }

        #[test]
        fn log_buffer_flush_on_drop() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let output = Arc::new(Mutex::new(vec![]));
            let writer = MutexBytes(output.clone());
            let (log_buffer, mut timeout_task) =
                StdoutBufferInner::new(std::time::Duration::from_secs(5), writer, 100);

            write!(log_buffer.lock(), "message1").expect("write message");
            assert_eq!(*output.lock(), b"");
            write!(log_buffer.lock(), "message2").expect("write message");
            assert_eq!(*output.lock(), b"");

            assert_eq!(executor.run_until_stalled(&mut timeout_task), Poll::Pending);
            assert_eq!(*output.lock(), b"");

            drop(log_buffer);
            assert_eq!(*output.lock(), b"message1message2");
        }

        #[test]
        fn log_buffer_with_timeout() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let output = Arc::new(Mutex::new(vec![]));
            let writer = MutexBytes(output.clone());
            let (log_buffer, mut timeout_task) =
                StdoutBufferInner::new(std::time::Duration::from_secs(5), writer, 100);

            write!(log_buffer.lock(), "message1").expect("write message");
            assert_eq!(*output.lock(), b"");
            write!(log_buffer.lock(), "message2").expect("write message");
            assert_eq!(*output.lock(), b"");

            assert_eq!(executor.run_until_stalled(&mut timeout_task), Poll::Pending);
            assert_eq!(*output.lock(), b"");

            executor.set_fake_time(executor.now().add(6.seconds()));
            executor.wake_next_timer();
            assert_eq!(executor.run_until_stalled(&mut timeout_task), Poll::Ready(()));
            assert_eq!(*output.lock(), b"message1message2");
        }

        #[test]
        fn log_buffer_capacity_reached() {
            let _executor = TestExecutor::new_with_fake_time().unwrap();
            let output = Arc::new(Mutex::new(vec![]));
            let writer = MutexBytes(output.clone());
            let (log_buffer, _timeout_task) =
                StdoutBufferInner::new(std::time::Duration::from_secs(5), writer, 10);

            write!(log_buffer.lock(), "message1").expect("write message");
            assert_eq!(*output.lock(), b"");
            write!(log_buffer.lock(), "message2").expect("write message");
            assert_eq!(*output.lock(), b"message1message2");

            // capacity was reached but buffering is still on, so next msg should buffer
            write!(log_buffer.lock(), "message1").expect("write message");
            assert_eq!(*output.lock(), b"message1message2");
            write!(log_buffer.lock(), "message2").expect("write message");
            assert_eq!(*output.lock(), b"message1message2message1message2");
        }
    }
}
