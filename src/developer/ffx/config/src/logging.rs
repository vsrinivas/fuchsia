// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    rand::Rng,
    std::{
        fs::{create_dir_all, remove_file, rename, File, OpenOptions},
        io::{ErrorKind, Read, Seek, SeekFrom, Write},
        path::PathBuf,
        str::FromStr,
        sync::{
            atomic::{AtomicBool, Ordering},
            Mutex,
        },
    },
    tracing::Metadata,
    tracing_subscriber::{
        filter::{self, LevelFilter},
        prelude::*,
        Layer,
    },
};

const LOG_DIR: &str = "log.dir";
const LOG_ROTATIONS: &str = "log.rotations";
const LOG_ROTATE_SIZE: &str = "log.rotate_size";
const LOG_ENABLED: &str = "log.enabled";
const LOG_TARGET_LEVELS: &str = "log.target_levels";
const LOG_LEVEL: &str = "log.level";
pub const LOG_PREFIX: &str = "ffx";
const TIME_FORMAT: &str = "%b %d %H:%M:%S";

static LOG_ENABLED_FLAG: AtomicBool = AtomicBool::new(true);

lazy_static::lazy_static! {
    static ref LOGGING_ID: u64 = generate_id();
}

pub fn disable_stdio_logging() {
    LOG_ENABLED_FLAG.store(false, Ordering::Relaxed);
}

fn generate_id() -> u64 {
    rand::thread_rng().gen::<u64>()
}

pub async fn log_file(name: &str, rotate: bool) -> Result<std::fs::File> {
    let mut log_path: PathBuf = super::query(LOG_DIR).get().await?;
    let log_rotations: Option<u64> = super::query(LOG_ROTATIONS).get().await?;
    let log_rotations = log_rotations.unwrap_or(0);
    create_dir_all(&log_path)?;
    log_path.push(format!("{}.log", name));

    if rotate && log_rotations > 0 {
        let mut rot_path = log_path.clone();

        let log_rotate_size: Option<u64> = super::query(LOG_ROTATE_SIZE).get().await?;
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
                    let size = f.seek(SeekFrom::End(0)).context("checking size of old log file")?;
                    let log_rotate_size = std::cmp::min(size, log_rotate_size);
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
    super::query(LOG_ENABLED).get().await.unwrap_or(false)
}

async fn filter_level() -> LevelFilter {
    super::query(LOG_LEVEL)
        .get::<String>()
        .await
        .ok()
        .map(|str| {
            // Ideally we could log here, but there may be no log sink, so fall back to a default
            LevelFilter::from_str(&str).unwrap_or(LevelFilter::DEBUG)
        })
        .unwrap_or(LevelFilter::DEBUG)
}

pub async fn init(log_to_stdio: bool, log_to_file: bool) -> Result<()> {
    let file: Option<File> = if log_to_file && is_enabled().await {
        Some(log_file(LOG_PREFIX, true).await?)
    } else {
        None
    };

    let level = filter_level().await;

    configure_subscribers(log_to_stdio, file, level).await;

    Ok(())
}

struct DisableableFilter;

impl<S> tracing_subscriber::layer::Filter<S> for DisableableFilter {
    fn enabled(
        &self,
        _meta: &Metadata<'_>,
        _cx: &tracing_subscriber::layer::Context<'_, S>,
    ) -> bool {
        LOG_ENABLED_FLAG.load(Ordering::Relaxed)
    }
}

async fn target_levels() -> Vec<(String, LevelFilter)> {
    // Parse the targets from the config. Ideally we'd log errors, but since there might be no log
    // sink, filter out any unexpected values.

    if let Ok(targets) = super::query(LOG_TARGET_LEVELS).get::<serde_json::Value>().await {
        if let serde_json::Value::Object(o) = targets {
            return o
                .into_iter()
                .filter_map(|(target, level)| {
                    if let serde_json::Value::String(level) = level {
                        if let Ok(level) = LevelFilter::from_str(&level) {
                            return Some((target, level));
                        }
                    }
                    None
                })
                .collect();
        }
    }

    vec![]
}

async fn configure_subscribers(stdio: bool, file: Option<File>, level: LevelFilter) {
    let filter_targets =
        filter::Targets::new().with_targets(target_levels().await).with_default(level);

    let stdio_layer = if stdio {
        let event_format = LogFormat::new(*LOGGING_ID);
        let format = tracing_subscriber::fmt::layer()
            .event_format(event_format)
            .with_filter(DisableableFilter)
            .with_filter(filter_targets.clone());
        Some(format)
    } else {
        None
    };

    let file_layer = file.map(|f| {
        let event_format = LogFormat::new(*LOGGING_ID);
        let writer = Mutex::new(std::io::LineWriter::new(f));
        let format = tracing_subscriber::fmt::layer()
            .event_format(event_format)
            .with_writer(writer)
            .with_filter(filter_targets);
        format
    });

    tracing_subscriber::registry().with(stdio_layer).with(file_layer).init();
}

#[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
struct LogTimer;

impl tracing_subscriber::fmt::time::FormatTime for LogTimer {
    fn format_time(&self, w: &mut tracing_subscriber::fmt::format::Writer<'_>) -> std::fmt::Result {
        let time = chrono::Local::now().format(TIME_FORMAT);
        write!(w, "{}", time)
    }
}

#[derive(Default, Debug, Copy, Clone, Eq, PartialEq)]
struct LogFormat {
    id: u64,
    display_thread_id: bool,
    display_filename: bool,
    display_line_number: bool,
    display_target: bool,
    timer: LogTimer,
}

impl LogFormat {
    fn new(id: u64) -> Self {
        LogFormat { id, display_target: true, ..Default::default() }
    }
}

impl<S, N> tracing_subscriber::fmt::FormatEvent<S, N> for LogFormat
where
    S: tracing::Subscriber + for<'a> tracing_subscriber::registry::LookupSpan<'a>,
    N: for<'a> tracing_subscriber::fmt::FormatFields<'a> + 'static,
{
    fn format_event(
        &self,
        ctx: &tracing_subscriber::fmt::FmtContext<'_, S, N>,
        mut writer: tracing_subscriber::fmt::format::Writer<'_>,
        event: &tracing::Event<'_>,
    ) -> std::fmt::Result {
        use tracing_log::NormalizeEvent;
        use tracing_subscriber::fmt::time::FormatTime;
        use tracing_subscriber::fmt::FormatFields;

        let normalized_meta = event.normalized_metadata();
        let meta = normalized_meta.as_ref().unwrap_or_else(|| event.metadata());

        if self.timer.format_time(&mut writer).is_err() {
            writer.write_str("<unknown time>")?;
        }
        writer.write_char(' ')?;

        write!(writer, "[{:0>20?}] ", self.id)?;

        match *meta.level() {
            tracing::Level::TRACE => write!(writer, "TRACE ")?,
            tracing::Level::DEBUG => write!(writer, "DEBUG ")?,
            tracing::Level::INFO => write!(writer, "INFO ")?,
            tracing::Level::WARN => write!(writer, "WARN ")?,
            tracing::Level::ERROR => write!(writer, "ERROR ")?,
        }

        if self.display_thread_id {
            write!(writer, "{:0>2?} ", std::thread::current().id())?;
        }

        let full_ctx = FullCtx::new(ctx, event.parent());
        write!(writer, "{}", full_ctx)?;

        if self.display_target {
            write!(writer, "{}: ", meta.target())?;
        }

        let line_number = if self.display_line_number { meta.line() } else { None };

        if self.display_filename {
            if let Some(filename) = meta.file() {
                write!(writer, "{}:{}", filename, if line_number.is_some() { "" } else { " " })?;
            }
        }

        if let Some(line_number) = line_number {
            write!(writer, "{}: ", line_number)?;
        }

        ctx.format_fields(writer.by_ref(), event)?;
        writeln!(writer)
    }
}

struct FullCtx<'a, S, N>
where
    S: tracing::Subscriber + for<'lookup> tracing_subscriber::registry::LookupSpan<'lookup>,
    N: for<'writer> tracing_subscriber::fmt::FormatFields<'writer> + 'static,
{
    ctx: &'a tracing_subscriber::fmt::FmtContext<'a, S, N>,
    span: Option<&'a tracing::span::Id>,
}

impl<'a, S, N: 'a> FullCtx<'a, S, N>
where
    S: tracing::Subscriber + for<'lookup> tracing_subscriber::registry::LookupSpan<'lookup>,
    N: for<'writer> tracing_subscriber::fmt::FormatFields<'writer> + 'static,
{
    fn new(
        ctx: &'a tracing_subscriber::fmt::FmtContext<'a, S, N>,
        span: Option<&'a tracing::span::Id>,
    ) -> Self {
        Self { ctx, span }
    }
}

impl<'a, S, N> std::fmt::Display for FullCtx<'a, S, N>
where
    S: tracing::Subscriber + for<'lookup> tracing_subscriber::registry::LookupSpan<'lookup>,
    N: for<'writer> tracing_subscriber::fmt::FormatFields<'writer> + 'static,
{
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        use std::fmt::Write;
        let mut seen = false;

        let span = self.span.and_then(|id| self.ctx.span(id)).or_else(|| self.ctx.lookup_current());

        let scope = span.into_iter().flat_map(|span| span.scope().from_root());

        for span in scope {
            write!(f, "{}", span.metadata().name())?;
            seen = true;

            let ext = span.extensions();
            let fields = &ext
                .get::<tracing_subscriber::fmt::FormattedFields<N>>()
                .expect("Unable to find FormattedFields in extensions; this is a bug");
            if !fields.is_empty() {
                write!(f, "{{{}}}", fields)?;
            }
            f.write_char(':')?;
        }

        if seen {
            f.write_char(' ')?;
        }
        Ok(())
    }
}
