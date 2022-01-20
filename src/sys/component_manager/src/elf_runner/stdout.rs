// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::config::StreamSink,
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    cm_logger::{fmt::FmtArgsLogger, scoped::ScopedLogger},
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_process as fproc, fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    futures::AsyncReadExt,
    log::Level,
    runner::component::ComponentNamespace,
    std::{boxed::Box, num::NonZeroUsize, sync::Arc},
    zx::HandleBased,
};

const STDOUT_FD: i32 = 1;
const STDERR_FD: i32 = 2;
const SVC_DIRECTORY_NAME: &str = "/svc";
const SYSLOG_PROTOCOL_NAME: &str = LogSinkMarker::NAME;
const NEWLINE: u8 = b'\n';

/// Max size for message when draining input stream socket. This number is
/// slightly smaller than size allowed by Archivist (LogSink service implementation).
const MAX_MESSAGE_SIZE: usize = 30720;

/// Bind stdout or stderr streams to syslog. This function binds either or both
/// output streams to syslog depending on value provided for each streams'
/// StreamSink. If the value for an output stream is set to StreamSink::Log,
/// that stream's file descriptor will be bound to syslog. All writes on that
// fd will be forwarded to syslog and will register as log entries. For stdout,
// the messages will be tagged with severity INFO. For stderr, the messages
// will be tagged with severity WARN. A task is created to listen to writes on
// the appropriate file descriptor and forward the message to syslog. This
// function returns both the task for each file descriptor and its
// corresponding HandleInfo.
pub async fn bind_streams_to_syslog(
    ns: &ComponentNamespace,
    stdout_sink: StreamSink,
    stderr_sink: StreamSink,
) -> Result<(Vec<fasync::Task<()>>, Vec<fproc::HandleInfo>), Error> {
    let mut tasks: Vec<fasync::Task<()>> = Vec::new();
    let mut handles: Vec<fproc::HandleInfo> = Vec::new();
    let mut logger: Option<Arc<ScopedLogger>> = None;

    for (fd, level, sink) in
        [(STDOUT_FD, Level::Info, stdout_sink), (STDERR_FD, Level::Warn, stderr_sink)].iter()
    {
        match sink {
            StreamSink::Log => {
                let logger_clone = match logger.as_ref() {
                    Some(logger) => logger.clone(),
                    None => {
                        let new_logger: Arc<ScopedLogger> =
                            Arc::new(create_namespace_logger(ns).await?);
                        logger = Some(new_logger.clone());
                        new_logger
                    }
                };

                let (task, handle) = forward_fd_to_syslog(logger_clone, *fd, *level)?;

                tasks.push(task);
                handles.push(handle);
            }
            StreamSink::None => {}
        }
    }

    Ok((tasks, handles))
}

async fn create_namespace_logger(ns: &ComponentNamespace) -> Result<ScopedLogger, Error> {
    let (_, dir) = ns
        .items()
        .iter()
        .find(|(path, _)| path == SVC_DIRECTORY_NAME)
        .ok_or(anyhow!("Didn't find {} directory in component's namespace!", SVC_DIRECTORY_NAME))?;

    ScopedLogger::from_directory(&dir, SYSLOG_PROTOCOL_NAME.to_owned()).await
}

fn forward_fd_to_syslog(
    logger: Arc<ScopedLogger>,
    fd: i32,
    level: Level,
) -> Result<(fasync::Task<()>, fproc::HandleInfo), Error> {
    let (rx, hnd) = new_socket_bound_to_fd(fd)?;
    let mut writer = SyslogWriter::new(logger, level);
    let task = fasync::Task::spawn(async move {
        if let Err(err) = drain_lines(rx, &mut writer).await {
            log::warn!("Draining output stream, fd {}, failed: {}", fd, err);
        }
    });

    Ok((task, hnd))
}

fn new_socket_bound_to_fd(fd: i32) -> Result<(zx::Socket, fproc::HandleInfo), Error> {
    let (tx, rx) = zx::Socket::create(zx::SocketOpts::STREAM)
        .map_err(|s| anyhow!("Failed to create socket: {}", s))?;

    Ok((
        rx,
        fproc::HandleInfo {
            handle: tx.into_handle(),
            id: HandleInfo::new(HandleType::FileDescriptor, fd as u16).as_raw(),
        },
    ))
}

/// Drains all bytes from socket and writes messages to writer. Bytes read
/// are split into lines and separated into chunks no greater than
/// MAX_MESSAGE_SIZE.
async fn drain_lines(socket: zx::Socket, writer: &mut dyn LogWriter) -> Result<(), Error> {
    let mut buffer = vec![0; MAX_MESSAGE_SIZE * 2];
    let mut offset = 0;

    let mut socket = fasync::Socket::from_socket(socket)
        .map_err(|s| anyhow!("Failed to create fasync::socket from zx::socket: {}", s))?;
    while let Some(bytes_read) = NonZeroUsize::new(
        socket
            .read(&mut buffer[offset..])
            .await
            .map_err(|err| anyhow!("Failed to read socket: {}", err))?,
    ) {
        let bytes_read = bytes_read.get();
        let end_pos = offset + bytes_read;

        if let Some(last_newline_pos) = buffer[..end_pos].iter().rposition(|&x| x == NEWLINE) {
            for line in buffer[..last_newline_pos].split(|&x| x == NEWLINE) {
                for chunk in line.chunks(MAX_MESSAGE_SIZE) {
                    let () = writer.write(chunk).await?;
                }
            }

            buffer.copy_within(last_newline_pos + 1..end_pos, 0);
            offset = end_pos - last_newline_pos - 1;
        } else {
            offset += bytes_read;

            while offset >= MAX_MESSAGE_SIZE {
                let () = writer.write(&buffer[..MAX_MESSAGE_SIZE]).await?;
                buffer.copy_within(MAX_MESSAGE_SIZE..offset, 0);
                offset -= MAX_MESSAGE_SIZE;
            }
        }
    }

    // We need to write any remaining bytes, while still maintaining
    // constraints of MAX_MESSAGE_SIZE.
    if offset != 0 {
        for chunk in buffer[..offset].chunks(MAX_MESSAGE_SIZE) {
            let () = writer.write(&chunk).await?;
        }
    }

    Ok(())
}

/// Object capable of writing a stream of bytes.
#[async_trait]
trait LogWriter: Send {
    async fn write(&mut self, bytes: &[u8]) -> Result<(), Error>;
}

struct SyslogWriter {
    logger: Arc<ScopedLogger>,
    level: Level,
}

impl SyslogWriter {
    pub fn new(logger: Arc<ScopedLogger>, level: Level) -> Self {
        Self { logger, level }
    }
}

#[async_trait]
impl LogWriter for SyslogWriter {
    async fn write(&mut self, bytes: &[u8]) -> Result<(), Error> {
        let msg = String::from_utf8_lossy(&bytes);
        self.logger.log(self.level, format_args!("{}", msg));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::testing::test_helpers::{
            create_fs_with_mock_logsink, get_message_logged_to_socket, MockServiceFs,
            MockServiceRequest,
        },
        anyhow::{anyhow, format_err, Context, Error},
        assert_matches::assert_matches,
        async_trait::async_trait,
        fidl_fuchsia_component_runner as fcrunner,
        fidl_fuchsia_logger::LogSinkRequest,
        fuchsia_async::futures::{channel::mpsc, try_join},
        fuchsia_zircon as zx,
        futures::{FutureExt, SinkExt, StreamExt},
        log::Level,
        rand::{
            distributions::{Alphanumeric, DistString as _},
            thread_rng,
        },
        std::{
            convert::TryFrom,
            sync::{Arc, Mutex},
        },
    };

    #[async_trait]
    impl LogWriter for mpsc::Sender<String> {
        async fn write(&mut self, bytes: &[u8]) -> Result<(), Error> {
            let message =
                std::str::from_utf8(&bytes).expect("Failed to decode bytes to utf8.").to_owned();
            let () =
                self.send(message).await.expect("Failed to send message to other end of mpsc.");
            Ok(())
        }
    }

    #[fuchsia::test]
    async fn syslog_writer_decodes_valid_utf8_message() -> Result<(), Error> {
        let (dir, ns_entries) = create_fs_with_mock_logsink()?;

        let ((), actual) = try_join!(
            write_to_syslog_or_panic(ns_entries, b"Hello World!"),
            read_message_from_syslog(dir)
        )?;

        assert_eq!(actual, Some("Hello World!".to_owned()));
        Ok(())
    }

    #[fuchsia::test]
    async fn syslog_writer_decodes_non_utf8_message() -> Result<(), Error> {
        let (dir, ns_entries) = create_fs_with_mock_logsink()?;

        let ((), actual) = try_join!(
            write_to_syslog_or_panic(ns_entries, b"Hello \xF0\x90\x80World!"),
            read_message_from_syslog(dir)
        )?;

        assert_eq!(actual, Some("Hello �World!".to_owned()));
        Ok(())
    }

    #[fuchsia::test]
    async fn drain_lines_splits_into_max_size_chunks() -> Result<(), Error> {
        let (tx, rx) = create_sockets()?;
        let (mut sender, recv) = create_mock_logger();
        let msg = get_random_string(MAX_MESSAGE_SIZE * 4);

        let () = take_and_write_to_socket(tx, &msg)?;
        let (actual, ()) =
            try_join!(recv.collect().map(Result::<Vec<String>, Error>::Ok), async move {
                drain_lines(rx, &mut sender).await
            })?;

        assert_eq!(
            actual,
            msg.as_bytes()
                .chunks(MAX_MESSAGE_SIZE)
                .map(|bytes| std::str::from_utf8(bytes).expect("Bytes are not utf8.").to_owned())
                .collect::<Vec<String>>()
        );

        Ok(())
    }

    #[fuchsia::test]
    async fn drain_lines_splits_at_newline() -> Result<(), Error> {
        let (tx, rx) = create_sockets()?;
        let (mut sender, recv) = create_mock_logger();
        let msg = std::iter::repeat_with(|| {
            Alphanumeric.sample_string(&mut thread_rng(), MAX_MESSAGE_SIZE - 1)
        })
        .take(3)
        .collect::<Vec<_>>()
        .join("\n");

        let () = take_and_write_to_socket(tx, &msg)?;
        let (actual, ()) =
            try_join!(recv.collect().map(Result::<Vec<String>, Error>::Ok), async move {
                drain_lines(rx, &mut sender).await
            })?;

        assert_eq!(actual, msg.split("\n").map(str::to_owned).collect::<Vec<String>>());
        Ok(())
    }

    #[fuchsia::test]
    async fn drain_lines_writes_when_message_is_received() -> Result<(), Error> {
        let (tx, rx) = create_sockets()?;
        let (mut sender, mut recv) = create_mock_logger();
        let messages: Vec<String> = vec!["Hello!\n".to_owned(), "World!\n".to_owned()];

        let ((), ()) = try_join!(async move { drain_lines(rx, &mut sender).await }, async move {
            for mut message in messages.into_iter() {
                let () = write_to_socket(&tx, &message)?;
                let logged_messaged =
                    recv.next().await.context("Receiver channel closed. Got no message.")?;
                // Logged message should strip '\n' so we need to do the same before assertion.
                message.pop();
                assert_eq!(logged_messaged, message);
            }

            Ok(())
        })?;

        Ok(())
    }

    #[fuchsia::test]
    async fn drain_lines_waits_for_entire_lines() -> Result<(), Error> {
        let (tx, rx) = create_sockets()?;
        let (mut sender, mut recv) = create_mock_logger();

        let ((), ()) = try_join!(async move { drain_lines(rx, &mut sender).await }, async move {
            let () = write_to_socket(&tx, "Hello\nWorld")?;
            let logged_messaged =
                recv.next().await.context("Receiver channel closed. Got no message.")?;
            assert_eq!(logged_messaged, "Hello");
            let () = write_to_socket(&tx, "Hello\nAgain")?;
            std::mem::drop(tx);
            let logged_messaged =
                recv.next().await.context("Receiver channel closed. Got no message.")?;
            assert_eq!(logged_messaged, "WorldHello");
            let logged_messaged =
                recv.next().await.context("Receiver channel closed. Got no message.")?;
            assert_eq!(logged_messaged, "Again");
            Ok(())
        })?;

        Ok(())
    }

    #[fuchsia::test]
    async fn drain_lines_return_error_if_stream_polls_err() -> Result<(), Error> {
        let (tx, rx) = create_sockets()?;
        // A closed socket should yield an error when stream is polled.
        let () = rx.half_close()?;
        let () = tx.half_close()?;
        let (mut sender, _recv) = create_mock_logger();

        let result = drain_lines(rx, &mut sender).await;

        assert_matches!(result, Err(_));
        Ok(())
    }

    async fn write_to_syslog_or_panic(
        ns_entries: Vec<fcrunner::ComponentNamespaceEntry>,
        message: &[u8],
    ) -> Result<(), Error> {
        let ns = ComponentNamespace::try_from(ns_entries)
            .context("Failed to create ComponentNamespace")?;
        let logger = create_namespace_logger(&ns).await.context("Failed to create ScopedLogger")?;
        let mut writer = SyslogWriter::new(Arc::new(logger), Level::Info);
        writer.write(message).await.context("Failed to write message")?;

        Ok(())
    }

    async fn read_message_from_syslog(
        dir: MockServiceFs<'static>,
    ) -> Result<Option<String>, Error> {
        let message_logged = Arc::new(Mutex::new(Option::<String>::None));
        dir.for_each_concurrent(None, |request: MockServiceRequest| match request {
            MockServiceRequest::LogSink(mut r) => {
                let message_logged_copy = Arc::clone(&message_logged);
                async move {
                    match r.next().await.expect("stream error").expect("fidl error") {
                        LogSinkRequest::Connect { socket, .. } => {
                            *message_logged_copy.lock().unwrap() =
                                get_message_logged_to_socket(socket);
                        }
                        LogSinkRequest::ConnectStructured { .. } => {
                            panic!("Unexpected call to `ConnectStructured`");
                        }
                        LogSinkRequest::WaitForInterestChange { responder: _ } => {
                            panic!("Unexpected call to `WaitForInterestChange`")
                        }
                    }
                }
            }
        })
        .await;

        let message_logged =
            message_logged.lock().map_err(|_| anyhow!("Failed to lock mutex"))?.clone();
        Ok(message_logged)
    }

    fn take_and_write_to_socket(socket: zx::Socket, message: &str) -> Result<(), Error> {
        write_to_socket(&socket, &message)
    }

    fn write_to_socket(socket: &zx::Socket, message: &str) -> Result<(), Error> {
        let bytes_written =
            socket.write(message.as_bytes()).context("Failed to write to socket")?;
        match bytes_written == message.len() {
            true => Ok(()),
            false => Err(format_err!("Bytes written to socket doesn't match len of message. Message len = {}. Bytes written = {}", message.len(), bytes_written)),
        }
    }

    fn create_mock_logger() -> (mpsc::Sender<String>, mpsc::Receiver<String>) {
        mpsc::channel::<String>(20)
    }

    fn create_sockets() -> Result<(zx::Socket, zx::Socket), Error> {
        zx::Socket::create(zx::SocketOpts::STREAM).context("Failed to create socket")
    }

    fn get_random_string(size: usize) -> String {
        Alphanumeric.sample_string(&mut thread_rng(), size)
    }
}
