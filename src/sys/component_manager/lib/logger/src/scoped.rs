// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fmt::{FmtArgsLogger, LOGGER},
    anyhow::{Context, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_logger::LogSinkProxy,
    fuchsia_syslog::{get_fx_logger_level as fx_log_level, Logger},
    fuchsia_zircon as zx,
    log::Level,
    std::{fmt::Arguments, path::PathBuf},
};

/// Writes to a LogSink socket obtained from the LogSinkProtocol. The
/// implementation falls back to using a default logger if LogSink is
/// unavailable.
pub struct ScopedLogger {
    logger: Logger,
}

impl ScopedLogger {
    fn new(logger: Logger) -> ScopedLogger {
        ScopedLogger { logger }
    }

    /// Instantiate a ScopedLogger by connecting to the provided path within
    /// the directory.
    pub async fn from_directory(
        dir: &fio::DirectoryProxy,
        path: String,
    ) -> Result<ScopedLogger, Error> {
        let log_sink_node = io_util::open_node(
            &dir,
            &PathBuf::from(path.trim_start_matches("/")),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_SERVICE,
        )?;
        let mut sink = LogSinkProxy::from_channel(log_sink_node.into_channel().unwrap());
        Ok(ScopedLogger::new(connect_to_logger(&mut sink).await?))
    }

    pub fn get_logger(&self) -> &Logger {
        &self.logger
    }
}

impl FmtArgsLogger for ScopedLogger {
    fn log(&self, level: Level, args: Arguments<'_>) {
        if self.logger.is_connected() {
            self.logger.log_f(fx_log_level(level), args, None);
        } else {
            LOGGER.log(level, args);
        }
    }
}

async fn connect_to_logger(sink: &fidl_fuchsia_logger::LogSinkProxy) -> Result<Logger, Error> {
    let (tx, rx) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
        .context("failed to create socket for LogSink connection")?;
    sink.connect(rx).context("failed to connect logger socket with LogSink")?;
    // IMPORTANT: Set tags to empty so attributed tags will be generated by the
    // logger. The logger should see this capability request as coming from the
    // component and use its default tags for messages coming via this socket.
    let tags = vec![];
    let logger = fuchsia_syslog::build_with_tags_and_socket(tx, &tags)
        .context("logger could not be built")?;
    Ok(logger)
}
