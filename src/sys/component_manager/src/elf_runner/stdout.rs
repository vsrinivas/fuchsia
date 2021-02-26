// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        logging::FmtArgsLogger,
        namespace::{get_logger_from_proxy, NamespaceLogger},
    },
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_logger::LogSinkMarker,
    fidl_fuchsia_process as fproc, fuchsia_async as fasync,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    log::Level,
    runner::{
        component::ComponentNamespace,
        log::{buffer_and_drain_logger, LogError, LogWriter, LoggerStream},
    },
    std::sync::Arc,
    zx::HandleBased,
};

const STDOUT_FD: i32 = 1;
const STDERR_FD: i32 = 2;
const SVC_DIRECTORY_NAME: &str = "/svc";
const SYSLOG_PROTOCOL_NAME: &str = LogSinkMarker::NAME;

/// Capture and forward the stdout and stderr output streams to syslog. The
/// connection to syslog is scoped to the component's namespace. Upon success,
/// a pair of Tasks and HandleInfo is returned. The Tasks are threads that contain
/// the listener for each output stream. The HandleInfos are handles containing
/// the file descriptors (stdout or stderr) bound to the listening socket.
pub async fn forward_stdout_to_syslog(
    ns: &ComponentNamespace,
) -> Result<(Vec<fasync::Task<()>>, Vec<fproc::HandleInfo>), Error> {
    let logger: Arc<NamespaceLogger> = Arc::new(create_namespace_logger(ns).await?);
    let mut hnds: Vec<fproc::HandleInfo> = Vec::new();
    let mut tasks: Vec<fasync::Task<()>> = Vec::new();

    for (fd, level) in [(STDOUT_FD, Level::Info), (STDERR_FD, Level::Warn)].iter() {
        let (stream, hnd) = new_stream_bound_to_fd(*fd)?;

        let mut writer = SyslogWriter::new(logger.clone(), *level);

        tasks.push(fasync::Task::spawn(async move {
            if let Err(err) = buffer_and_drain_logger(stream, Box::new(&mut writer)).await {
                log::warn!("Draining stdout logging failed: {}", err);
            }
        }));

        hnds.push(hnd);
    }

    Ok((tasks, hnds))
}

async fn create_namespace_logger(ns: &ComponentNamespace) -> Result<NamespaceLogger, Error> {
    let (_, dir) = ns
        .items()
        .iter()
        .find(|(path, _)| path == SVC_DIRECTORY_NAME)
        .ok_or(anyhow!("Didn't find {} directory in component's namespace!", SVC_DIRECTORY_NAME))?;

    get_logger_from_proxy(&dir, SYSLOG_PROTOCOL_NAME.to_owned()).await
}

fn new_stream_bound_to_fd(fd: i32) -> Result<(LoggerStream, fproc::HandleInfo), Error> {
    let (client, log) = zx::Socket::create(zx::SocketOpts::STREAM)
        .map_err(|s| anyhow!("Failed to create socket: {}", s))?;

    Ok((
        LoggerStream::new(client)
            .map_err(|s| anyhow!("Failed to create LoggerStream from socket: {}", s))?,
        fproc::HandleInfo {
            handle: log.into_handle(),
            id: HandleInfo::new(HandleType::FileDescriptor, fd as u16).as_raw(),
        },
    ))
}

struct SyslogWriter {
    logger: Arc<NamespaceLogger>,
    level: Level,
}

impl SyslogWriter {
    pub fn new(logger: Arc<NamespaceLogger>, level: Level) -> Self {
        Self { logger, level }
    }
}

#[async_trait]
impl LogWriter for SyslogWriter {
    async fn write(&mut self, bytes: &[u8]) -> Result<usize, LogError> {
        let msg = String::from_utf8_lossy(&bytes);
        self.logger.log(self.level, format_args!("{}", msg));
        Ok(bytes.len())
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
        anyhow::{anyhow, Context, Error},
        fidl_fuchsia_component_runner as fcrunner,
        fidl_fuchsia_logger::LogSinkRequest,
        fuchsia_async::{self as fasync, futures::try_join},
        futures::StreamExt,
        log::Level,
        std::{
            convert::TryFrom,
            sync::{Arc, Mutex},
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn syslog_writer_decodes_valid_utf8_message() -> Result<(), Error> {
        let (dir, ns_entries) = create_fs_with_mock_logsink()?;

        let ((), actual) = try_join!(
            write_to_syslog_or_panic(ns_entries, b"Hello World!"),
            read_message_from_syslog(dir)
        )?;

        assert_eq!(actual, Some("Hello World!".to_owned()));
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn syslog_writer_decodes_non_utf8_message() -> Result<(), Error> {
        let (dir, ns_entries) = create_fs_with_mock_logsink()?;

        let ((), actual) = try_join!(
            write_to_syslog_or_panic(ns_entries, b"Hello \xF0\x90\x80World!"),
            read_message_from_syslog(dir)
        )?;

        assert_eq!(actual, Some("Hello �World!".to_owned()));
        Ok(())
    }

    async fn write_to_syslog_or_panic(
        ns_entries: Vec<fcrunner::ComponentNamespaceEntry>,
        message: &[u8],
    ) -> Result<(), Error> {
        let ns = ComponentNamespace::try_from(ns_entries)
            .context("Failed to create ComponentNamespace")?;
        let logger =
            create_namespace_logger(&ns).await.context("Failed to create NamespaceLogger")?;
        let mut writer = SyslogWriter::new(Arc::new(logger), Level::Info);
        let _ = writer.write(message).await.context("Failed to write message")?;

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
                    }
                }
            }
        })
        .await;

        let message_logged =
            message_logged.lock().map_err(|_| anyhow!("Failed to lock mutex"))?.clone();
        Ok(message_logged)
    }
}
