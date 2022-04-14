// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    simplelog::{CombinedLogger, Config, ConfigBuilder, LevelFilter, SimpleLogger, WriteLogger},
    std::fs::{create_dir_all, remove_file, rename, File, OpenOptions},
    std::io::{ErrorKind, Read, Seek, SeekFrom, Write},
    std::path::PathBuf,
    std::str::FromStr,
    std::sync::atomic::{AtomicBool, Ordering},
};

const LOG_DIR: &str = "log.dir";
const LOG_ROTATIONS: &str = "log.rotations";
const LOG_ROTATE_SIZE: &str = "log.rotate_size";
const LOG_ENABLED: &str = "log.enabled";
const LOG_LEVEL: &str = "log.level";
pub const LOG_PREFIX: &str = "ffx";
const TIME_FORMAT: &str = "%b %d %H:%M:%S";

static LOG_ENABLED_FLAG: AtomicBool = AtomicBool::new(true);

fn config() -> Config {
    // Sets the target level to "Error" so that all logs show their module
    // target in the logs.
    ConfigBuilder::new()
        .set_target_level(LevelFilter::Error)
        .set_time_to_local(true)
        .set_time_format_str(TIME_FORMAT)
        .build()
}

struct DisableableSimpleLogger {
    logger: Box<SimpleLogger>,
}

pub fn disable_stdio_logging() {
    LOG_ENABLED_FLAG.store(false, Ordering::Relaxed);
}

impl DisableableSimpleLogger {
    pub fn new(logger: Box<SimpleLogger>) -> Box<Self> {
        Box::new(Self { logger })
    }

    pub fn is_enabled(&self) -> bool {
        LOG_ENABLED_FLAG.load(Ordering::Relaxed)
    }
}

impl log::Log for DisableableSimpleLogger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        self.is_enabled() && self.logger.enabled(metadata)
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.is_enabled() {
            self.logger.log(record);
        }
    }

    fn flush(&self) {
        if self.is_enabled() {
            self.logger.flush();
        }
    }
}

impl simplelog::SharedLogger for DisableableSimpleLogger {
    fn level(&self) -> log::LevelFilter {
        if self.is_enabled() {
            self.logger.level()
        } else {
            log::LevelFilter::Off
        }
    }

    fn config(&self) -> Option<&Config> {
        self.logger.config()
    }

    fn as_log(self: Box<Self>) -> Box<dyn log::Log> {
        Box::new(*self)
    }
}

pub async fn log_file(name: &str, rotate: bool) -> Result<std::fs::File> {
    let mut log_path: PathBuf = super::get(LOG_DIR).await?;
    let log_rotations: Option<u64> = super::get(LOG_ROTATIONS).await?;
    let log_rotations = log_rotations.unwrap_or(0);
    create_dir_all(&log_path)?;
    log_path.push(format!("{}.log", name));

    if rotate && log_rotations > 0 {
        let mut rot_path = log_path.clone();

        let log_rotate_size: Option<u64> = super::get(LOG_ROTATE_SIZE).await?;
        if let Some(log_rotate_size) = log_rotate_size {
            // log.rotate_size was set. We only rotate if the current file is bigger than that size,
            // so open the current file and, if it's smaller than that size, return it.
            match OpenOptions::new().write(true).append(true).create(false).open(&log_path) {
                Ok(mut f) => {
                    if f.seek(SeekFrom::End(0)).context("checking log file size")? < log_rotate_size
                    {
                        return Ok(f);
                    }
                }
                Err(e) if e.kind() == ErrorKind::NotFound => (),
                other => {
                    other.context("opening log file")?;
                    unreachable!();
                }
            }
        }

        rot_path.set_file_name(format!("{}.log.{}", name, log_rotations - 1));
        match remove_file(&rot_path) {
            Err(e) if e.kind() == ErrorKind::NotFound => (),
            other => other.context("deleting stale logs")?,
        }

        for rotation in (0..log_rotations - 1).rev() {
            let prev_path = rot_path.clone();
            rot_path.set_file_name(format!("{}.log.{}", name, rotation));
            match rename(&rot_path, prev_path) {
                Err(e) if e.kind() == ErrorKind::NotFound => (),
                other => other.context("rotating log files")?,
            }
        }

        if let Some(log_rotate_size) = log_rotate_size {
            // When we move the most recent log into rotation, truncate it if it is larger than the
            // rotation length.
            match OpenOptions::new().read(true).create(false).open(&log_path) {
                Ok(mut f) => {
                    f.seek(SeekFrom::End(-(log_rotate_size as i64)))
                        .context("seeking through old log file")?;
                    let mut new = OpenOptions::new()
                        .write(true)
                        .create(true)
                        .open(rot_path)
                        .context("opening rotating log file")?;
                    new.write_all(b"<truncated for length>")
                        .context("writing log truncation notice")?;
                    let mut buf = [0; 4096];
                    loop {
                        let got = f.read(&mut buf).context("reading old log file")?;
                        if got == 0 {
                            break;
                        }
                        new.write_all(&buf[..got]).context("writing truncated log file")?;
                    }
                    match remove_file(&log_path) {
                        Err(e) if e.kind() == ErrorKind::NotFound => (),
                        other => other.context("deleting stale untruncated log")?,
                    }
                }
                Err(e) if e.kind() == ErrorKind::NotFound => (),
                other => {
                    other.context("opening old log file")?;
                    unreachable!();
                }
            }
        } else {
            match rename(&log_path, rot_path) {
                Err(e) if e.kind() == ErrorKind::NotFound => (),
                other => other.context("rotating log files")?,
            }
        }
    }

    OpenOptions::new()
        .write(true)
        .append(true)
        .create(true)
        .open(log_path)
        .context("opening log file")
}

pub async fn is_enabled() -> bool {
    super::get(LOG_ENABLED).await.unwrap_or(false)
}

pub async fn filter_level() -> LevelFilter {
    super::get::<String, _>(LOG_LEVEL)
        .await
        .ok()
        .map(|str| {
            // Ideally we could log here, but there may be no log sink, so fall back to a default
            LevelFilter::from_str(&str).unwrap_or(LevelFilter::Debug)
        })
        .unwrap_or(LevelFilter::Debug)
}

pub async fn init(log_to_stdio: bool, log_to_file: bool) -> Result<()> {
    let file: Option<File> = if log_to_file && is_enabled().await {
        Some(log_file(LOG_PREFIX, true).await?)
    } else {
        None
    };

    let level = filter_level().await;

    CombinedLogger::init(get_loggers(log_to_stdio, file, level)).context("initializing logger")
}

fn get_loggers(
    stdio: bool,
    file: Option<File>,
    level: LevelFilter,
) -> Vec<Box<dyn simplelog::SharedLogger>> {
    let mut loggers: Vec<Box<dyn simplelog::SharedLogger>> = vec![];

    // The daemon logs to stdio, and is redirected to file by spawn_daemon, which enables
    // panics and backtraces to also be included.
    if stdio {
        loggers.push(DisableableSimpleLogger::new(SimpleLogger::new(level, config())));
    }

    if let Some(file) = file {
        let writer = std::io::LineWriter::new(file);
        loggers.push(WriteLogger::new(level, config(), writer));
    }

    loggers
}

#[cfg(test)]
mod test {
    use super::*;
    use log::Log;

    #[test]
    fn test_get_loggers() {
        let loggers = get_loggers(false, None, LevelFilter::Debug);
        assert!(loggers.len() == 0);

        // SimpleLogger (error logs to stderr, all other levels to stdout)
        let loggers = get_loggers(true, None, LevelFilter::Debug);
        assert!(loggers.len() == 1);

        // WriteLogger (error logs to stderr, all other logs to file)
        let loggers = get_loggers(false, Some(tempfile::tempfile().unwrap()), LevelFilter::Debug);
        assert!(loggers.len() == 1);

        // SimpleLogger & WriteLogger (error logs to stderr, all other levels to stdout and file)
        let loggers = get_loggers(true, Some(tempfile::tempfile().unwrap()), LevelFilter::Debug);
        assert!(loggers.len() == 2);
    }

    #[test]
    fn test_disable_logger() {
        let logger = DisableableSimpleLogger::new(SimpleLogger::new(LevelFilter::Debug, config()));
        let metadata =
            log::MetadataBuilder::new().level(log::Level::Error).target("test-target").build();

        assert!(logger.enabled(&metadata));
        disable_stdio_logging();
        assert!(!logger.enabled(&metadata));

        // This might not be necessary but restores the shared state to what it was before the test
        LOG_ENABLED_FLAG.store(true, Ordering::Relaxed);
    }
}
