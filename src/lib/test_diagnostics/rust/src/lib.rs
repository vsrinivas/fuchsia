// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate provides helper functions to collect test diagnostics.

use {
    anyhow::Context as _,
    fidl::handle::AsHandleRef,
    futures::{
        channel::mpsc,
        io::{self, AsyncRead},
        lock::Mutex,
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    log::*,
    std::{cell::RefCell, marker::Unpin, pin::Pin, sync::Arc},
};

pub use crate::diagnostics::LogStream;

mod diagnostics;

/// Buffer logs for this duration before flushing them.
const LOG_BUFFERING_DURATION: std::time::Duration = std::time::Duration::from_secs(5);

/// Maximum log buffer size.
const LOG_BUFFER_SIZE: usize = 4096;

#[must_use = "futures/streams"]
pub struct StdoutStream {
    socket: fidl::AsyncSocket,
}
impl Unpin for StdoutStream {}

thread_local! {
    pub static BUFFER:
        RefCell<[u8; 2048]> = RefCell::new([0; 2048]);
}

impl StdoutStream {
    /// Creates a new `StdoutStream` for given `socket`.
    pub fn new(socket: fidl::Socket) -> Result<StdoutStream, anyhow::Error> {
        let stream = StdoutStream {
            socket: fidl::AsyncSocket::from_socket(socket).context("Invalid zircon socket")?,
        };
        Ok(stream)
    }
}

fn process_stdout_bytes(bytes: &[u8]) -> String {
    // TODO(anmittal): Change this to consider break in logs and handle it.
    let log = String::from_utf8_lossy(bytes);
    log.to_string()
}

impl Stream for StdoutStream {
    type Item = io::Result<String>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        BUFFER.with(|b| {
            let mut b = b.borrow_mut();
            let len = ready!(Pin::new(&mut self.socket).poll_read(cx, &mut *b)?);
            if len == 0 {
                return Poll::Ready(None);
            }
            Poll::Ready(Some(process_stdout_bytes(&b[0..len])).map(Ok))
        })
    }
}

struct LogOpt {
    /// Socket of which test serves stdout.
    stdout_socket: fidl::Socket,
    /// Send Log Event over this channel.
    sender: mpsc::Sender<String>,
    /// Duration to buffer the logs before flushing them.
    buffering_duration: std::time::Duration,
    /// MAx buffer size before logs are flushed.
    buffer_size: usize,
}

pub async fn collect_and_send_string_output(
    socket: fidl::Socket,
    sender: mpsc::Sender<String>,
) -> Result<(), anyhow::Error> {
    collect_and_send_string_output_internal(LogOpt {
        stdout_socket: socket,
        sender,
        buffering_duration: LOG_BUFFERING_DURATION,
        buffer_size: LOG_BUFFER_SIZE,
    })
    .await
}
/// Internal method that put a listener on `stdout_socket`, process and send test stdout logs
/// asynchronously in the background. Returns immediately if the provided socket is an invalid
/// handle.
async fn collect_and_send_string_output_internal(log_opt: LogOpt) -> Result<(), anyhow::Error> {
    if log_opt.stdout_socket.as_handle_ref().is_invalid() {
        return Ok(());
    }

    let mut stream = match StdoutStream::new(log_opt.stdout_socket) {
        Err(e) => {
            error!("Stdout Logger: Failed to create fuchsia async socket: {:?}", e);
            return Ok(());
        }
        Ok(stream) => stream,
    };
    let mut log_buffer =
        StdoutBuffer::new(log_opt.buffering_duration, log_opt.sender, log_opt.buffer_size);

    while let Some(log) = stream.try_next().await.context("Error reading stdout log msg")? {
        log_buffer.send_log(&log).await?;
    }
    log_buffer.done().await
}

/// Buffers logs in memory for `duration` before sending it out.
/// This will not buffer any more logs after timer expires and will flush all
/// subsequent logs instantly.
/// Clients may call done() to obtain any errors. If not called, done() will be called when the
/// buffer is dropped and any errors will be suppressed.
struct StdoutBuffer {
    inner: Arc<Mutex<StdoutBufferInner>>,
    timer: fuchsia_async::Task<()>,
}

impl StdoutBuffer {
    /// Crates new StdoutBuffer and starts the timer on log buffering.
    /// `duration`: Buffers log for this duration or till done() is called.
    /// `sender`: Channel to send logs on.
    /// `max_capacity`: Flush log if buffer size exceeds this value. This will not cancel the timer
    /// and all the logs would be flushed once timer expires.
    pub fn new(
        duration: std::time::Duration,
        sender: mpsc::Sender<String>,
        max_capacity: usize,
    ) -> Self {
        let inner = StdoutBufferInner::new(sender, max_capacity);
        let timer = fuchsia_async::Timer::new(duration);
        let log_buffer = Arc::downgrade(&inner);
        let f = async move {
            timer.await;
            if let Some(log_buffer) = log_buffer.upgrade() {
                let mut log_buffer = log_buffer.lock().await;
                if let Err(e) = log_buffer.stop_buffering().await {
                    log_buffer.set_error(e);
                }
            }
        };

        let timer = fuchsia_async::Task::spawn(f);

        Self { inner, timer }
    }

    /// This will abort the timer (if not already fired) and then flush all
    /// the logs in buffer.
    /// This function should be called before the object is dropped.
    /// Returns error due flushing the logs.
    pub async fn done(self) -> Result<(), anyhow::Error> {
        // abort timer so that it is not fired in the future.
        self.timer.cancel().await;

        let mut inner = self.inner.lock().await;
        inner.flush().await?;
        inner.error.take().map_or_else(|| Ok(()), Err)
    }

    /// This will instantly send logs over the channel if timer has already
    /// fired, otherwise this will buffer the logs.
    ///
    /// Returns error due flushing the logs.
    pub async fn send_log(&mut self, message: &str) -> Result<(), anyhow::Error> {
        let mut inner = self.inner.lock().await;
        inner.send_log(message).await
    }
}

struct StdoutBufferInner {
    logs: String,
    sender: mpsc::Sender<String>,
    /// Whether to buffer logs or not.
    buffer: bool,
    error: Option<anyhow::Error>,
    max_capacity: usize,
}

impl StdoutBufferInner {
    fn new(sender: mpsc::Sender<String>, max_capacity: usize) -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(StdoutBufferInner {
            logs: String::with_capacity(max_capacity),
            sender: sender,
            buffer: true,
            error: None,
            max_capacity,
        }))
    }

    async fn stop_buffering(&mut self) -> Result<(), anyhow::Error> {
        self.buffer = false;
        self.flush().await
    }

    fn set_error(&mut self, e: anyhow::Error) {
        self.error = Some(e);
    }

    async fn flush(&mut self) -> Result<(), anyhow::Error> {
        if !self.logs.is_empty() {
            let ret = Self::send(&mut self.sender, &self.logs).await;
            self.logs.truncate(0);
            return ret;
        }

        Ok(())
    }

    async fn send(sender: &mut mpsc::Sender<String>, message: &str) -> Result<(), anyhow::Error> {
        sender.send(message.into()).await.context("Error sending stdout msg")
    }

    async fn send_log(&mut self, message: &str) -> Result<(), anyhow::Error> {
        if self.buffer {
            self.logs.push_str(message);
            if self.logs.len() >= self.max_capacity {
                return self.flush().await;
            }
            return Ok(());
        }
        Self::send(&mut self.sender, message).await
    }
}

impl Drop for StdoutBufferInner {
    fn drop(&mut self) {
        if !self.logs.is_empty() {
            let message = self.logs.clone();
            let mut sender = self.sender.clone();
            fuchsia_async::Task::spawn(async move {
                if let Err(e) = StdoutBufferInner::send(&mut sender, &message).await {
                    warn!("Error sending logs for {}", e);
                }
            })
            .detach();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::HandleBased;
    use futures::StreamExt;
    use pretty_assertions::assert_eq;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn collect_test_stdout() {
        let (sock_server, sock_client) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).expect("Failed while creating socket");

        let (sender, mut recv) = mpsc::channel(1);

        let fut = fuchsia_async::Task::spawn(collect_and_send_string_output_internal(LogOpt {
            stdout_socket: sock_client,
            sender: sender.into(),
            buffering_duration: std::time::Duration::from_millis(1),
            buffer_size: LOG_BUFFER_SIZE,
        }));

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
            super::*,
            fuchsia_async::{pin_mut, TestExecutor},
            fuchsia_zircon::DurationNum,
            matches::assert_matches,
            pretty_assertions::assert_eq,
            std::ops::Add,
        };

        fn send_msg(
            executor: &mut fuchsia_async::TestExecutor,
            log_buffer: &mut StdoutBuffer,
            msg: &str,
        ) {
            let f = async {
                log_buffer.send_log(&msg).await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
        }

        fn recv_msg<T>(
            executor: &mut fuchsia_async::TestExecutor,
            recv: &mut mpsc::Receiver<T>,
        ) -> Poll<Option<T>> {
            let f = recv.next();
            pin_mut!(f);
            executor.run_until_stalled(&mut f)
        }

        #[test]
        fn log_buffer_without_timeout() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sender, mut recv) = mpsc::channel(1);
            let mut log_buffer =
                StdoutBuffer::new(std::time::Duration::from_secs(5), sender.into(), 100);
            let msg1 = "message1".to_owned();
            let msg2 = "message2".to_owned();

            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);
            send_msg(&mut executor, &mut log_buffer, &msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);

            let f = async {
                log_buffer.done().await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
            let mut expected_msg = msg1;
            expected_msg.push_str(&msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(expected_msg)));
        }

        #[test]
        fn log_buffer_with_timeout() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sender, mut recv) = mpsc::channel(1);
            let mut log_buffer =
                StdoutBuffer::new(std::time::Duration::from_secs(5), sender.into(), 100);
            let msg1 = "message1".to_owned();
            let msg2 = "message2".to_owned();

            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);
            send_msg(&mut executor, &mut log_buffer, &msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);

            executor.set_fake_time(executor.now().add(6.seconds()));
            executor.wake_next_timer();
            let mut expected_msg = msg1.clone();
            expected_msg.push_str(&msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(expected_msg)));

            // timer fired, no more buffering should happen.
            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(msg1)));

            send_msg(&mut executor, &mut log_buffer, &msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(msg2)));

            let f = async {
                log_buffer.done().await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
        }

        #[test]
        fn log_buffer_capacity_reached() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sender, mut recv) = mpsc::channel(1);
            let mut log_buffer =
                StdoutBuffer::new(std::time::Duration::from_secs(5), sender.into(), 10);
            let msg1 = "message1".to_owned();
            let msg2 = "message2".to_owned();

            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);
            send_msg(&mut executor, &mut log_buffer, &msg2);
            let mut expected_msg = msg1.clone();
            expected_msg.push_str(&msg2);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(expected_msg)));

            // capacity was reached but buffering is still on, so next msg should buffer
            send_msg(&mut executor, &mut log_buffer, &msg1);
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Pending);

            let f = async {
                log_buffer.done().await.unwrap();
            };
            pin_mut!(f);
            assert_eq!(executor.run_until_stalled(&mut f), Poll::Ready(()));
            assert_eq!(recv_msg(&mut executor, &mut recv), Poll::Ready(Some(msg1)));
        }

        #[test]
        fn collect_test_stdout_when_socket_closed() {
            let mut executor = TestExecutor::new_with_fake_time().unwrap();
            let (sock_server, sock_client) = fidl::Socket::create(fidl::SocketOpts::STREAM)
                .expect("Failed while creating socket");

            let (sender, mut recv) = mpsc::channel(1);
            let mut fut = collect_and_send_string_output_internal(LogOpt {
                stdout_socket: sock_client,
                sender: sender.into(),
                buffering_duration: std::time::Duration::from_secs(10),
                buffer_size: LOG_BUFFER_SIZE,
            })
            .boxed();

            sock_server.write(b"test message 1").expect("Can't write msg to socket");
            sock_server.write(b"test message 2").expect("Can't write msg to socket");
            sock_server.write(b"test message 3").expect("Can't write msg to socket");
            sock_server.into_handle(); // this will drop this handle and close it.

            // timer is never fired but we should still receive logs.
            assert_matches!(executor.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
            assert_eq!(
                recv_msg(&mut executor, &mut recv),
                Poll::Ready(Some("test message 1test message 2test message 3".into()))
            );
        }
    }
}
