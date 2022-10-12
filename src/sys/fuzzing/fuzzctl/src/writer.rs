// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    diagnostics_data::{LogsData, Severity},
    serde_json::to_vec_pretty,
    std::cell::RefCell,
    std::fmt::{Debug, Display},
    std::fs::{create_dir_all, File},
    std::io::{self, Write},
    std::path::{Path, PathBuf},
    std::rc::Rc,
    std::sync::{Arc, Mutex},
    termion::{color, style},
};

/// `Writer` handles formatting and delivering output from both the plugin and fuzzer.
///
/// Callers can use this object to configure how output should be formatted, suppressed, etc. The
/// underlying `OutputSink` is responsible for determining where output actually goes. The plugin
/// uses `print` and `error`, while the fuzzer uses `write_all` and `log`. Fuzzer output can be
/// paused and resumed, with data being buffered between those calls.
#[derive(Debug)]
pub struct Writer<O: OutputSink> {
    output: O,
    muted: bool,
    use_colors: bool,
    file: Rc<RefCell<Option<File>>>,
    buffered: Arc<Mutex<Option<Vec<Buffered>>>>,
}

/// `OutputSink`s takes output and writes it to some destination.
pub trait OutputSink: Clone + Debug + 'static {
    // Writes bytes directly to stdout.
    fn write_all(&self, _buf: &[u8]);

    // Writes a displayable message.
    fn print<D: Display>(&self, _message: D);

    // Writes an error message.
    fn error<D: Display>(&self, _message: D);
}

#[derive(Debug)]
enum Buffered {
    Data(Vec<u8>),
    Log(String),
}

impl<O: OutputSink> Writer<O> {
    /// Creates a new `Writer`.
    ///
    /// This object will display data received by methods like `print` by formatting it and sending
    /// it to the given `output` sink.
    pub fn new(output: O) -> Self {
        Self {
            output,
            muted: false,
            use_colors: true,
            file: Rc::new(RefCell::new(None)),
            buffered: Arc::new(Mutex::new(None)),
        }
    }

    /// If true, suppresses output except for plugin errors.
    pub fn mute(&mut self, muted: bool) {
        self.muted = muted
    }

    /// If true, embeds ANSI escape sequences in the output to add colors and styles.
    pub fn use_colors(&mut self, use_colors: bool) {
        self.use_colors = use_colors
    }

    /// Creates a new `Writer` that is a clone of this object, except that it duplicates its output
    /// and writes it to a file created from `dirname` and `filename`.
    pub fn tee<P: AsRef<Path>, S: AsRef<str>>(&self, dirname: P, filename: S) -> Result<Writer<O>> {
        let dirname = dirname.as_ref();
        create_dir_all(dirname)
            .with_context(|| format!("failed to create directory: {}", dirname.display()))?;
        let mut path = PathBuf::from(dirname);
        path.push(filename.as_ref());
        let file = File::options()
            .append(true)
            .create(true)
            .open(&path)
            .with_context(|| format!("failed to open file: {}", path.display()))?;
        Ok(Self {
            output: self.output.clone(),
            muted: self.muted,
            use_colors: self.use_colors,
            file: Rc::new(RefCell::new(Some(file))),
            buffered: Arc::clone(&self.buffered),
        })
    }

    /// Writes a displayable message to the `OutputSink`.
    ///
    /// This method is used with output from the `ffx fuzz` plugin.
    pub fn print<D: Display>(&self, message: D) {
        if self.muted {
            return;
        }
        let formatted = format!("{}{}{}", self.yellow(), message, self.reset());
        self.output.print(formatted);
    }

    /// Like `print`, except that it also adds a newline.
    pub fn println<D: Display>(&self, message: D) {
        self.print(format!("{}\n", message));
    }

    /// Writes a displayable error to the `OutputSink`.
    ///
    /// This method is used with output from the `ffx fuzz` plugin.
    pub fn error<D: Display>(&self, message: D) {
        let formatted =
            format!("{}{}ERROR: {}{}\n", self.bold(), self.red(), message, self.reset());
        self.output.error(formatted);
    }

    /// Writes bytes directly to the `OutputSink`.
    ///
    /// This method is used with output from the fuzzer. If `pause` is called, data passed to this
    /// method will be buffered until `resume` is called.
    pub fn write_all(&self, buf: &[u8]) {
        self.write_all_to_file(buf).expect("failed to write data to file");
        if self.muted {
            return;
        }
        let mut buffered = self.buffered.lock().unwrap();
        match buffered.as_mut() {
            Some(buffered) => buffered.push(Buffered::Data(buf.to_vec())),
            None => self.output.write_all(buf),
        };
    }

    /// Writes a structured log entry to the `OutputSink`.
    ///
    /// This method is used with output from the fuzzer. If `pause` is called, data passed to this
    /// method will be buffered until `resume` is called.
    ///
    /// The display format loosely imitates that of `ffx log` as implemented by that plugin's
    /// `DefaultLogFormatter`.
    pub fn log(&self, logs_data: LogsData) {
        let mut serialized = to_vec_pretty(&logs_data).expect("failed to serialize");
        serialized.push('\n' as u8);
        self.write_all_to_file(&serialized).expect("failed to write log to file");
        if self.muted {
            return;
        }
        let color_and_style = match logs_data.metadata.severity {
            Severity::Fatal => format!("{}{}", self.bold(), self.red()),
            Severity::Error => self.red(),
            Severity::Warn => self.yellow(),
            _ => String::default(),
        };
        let severity = &format!("{}", logs_data.metadata.severity)[..1];
        let location = match (&logs_data.metadata.file, &logs_data.metadata.line) {
            (Some(filename), Some(line)) => format!(": [{}:{}]", filename, line),
            (Some(filename), None) => format!(": [{}]", filename),
            _ => String::default(),
        };
        let formatted = format!(
            "[{ts:05.3}][{moniker}][{tags}][{textfmt}{sev}{reset}]{loc} {textfmt}{msg}{reset}\n",
            ts = logs_data.metadata.timestamp as f64 / 1_000_000_000 as f64,
            moniker = logs_data.moniker,
            tags = logs_data.tags().map(|t| t.join(",")).unwrap_or(String::default()),
            textfmt = color_and_style,
            sev = severity,
            reset = self.reset(),
            loc = location,
            msg = logs_data.msg().unwrap_or("<missing message>"),
        );
        let mut buffered = self.buffered.lock().unwrap();
        match buffered.as_mut() {
            Some(buffered) => buffered.push(Buffered::Log(formatted)),
            None => self.output.print(formatted),
        };
    }

    /// Pauses the display of fuzzer output.
    ///
    /// When this is called, any data passed to `write_all` or `log` will be buffered until `resume`
    /// is called.
    pub fn pause(&self) {
        let mut buffered = self.buffered.lock().unwrap();
        if buffered.is_none() {
            *buffered = Some(Vec::new());
        }
    }

    /// Resumes the display of fuzzer output.
    ///
    /// When this is called, any data passed to `write_all` or `log` since `pause` was invoked will
    /// be sent to the `OutputSink`.
    pub fn resume(&self) {
        let buffered = match self.buffered.lock().unwrap().take() {
            Some(buffered) => buffered,
            None => return,
        };
        for message in buffered.into_iter() {
            match message {
                Buffered::Data(bytes) => self.output.write_all(&bytes),
                Buffered::Log(formatted) => self.output.print(formatted),
            }
        }
    }

    fn write_all_to_file(&self, buf: &[u8]) -> Result<()> {
        let mut file = self.file.borrow_mut();
        if let Some(file) = file.as_mut() {
            file.write_all(buf).context("failed to write to file")?;
        }
        Ok(())
    }

    fn bold(&self) -> String {
        if self.use_colors {
            style::Bold.to_string()
        } else {
            String::default()
        }
    }

    fn yellow(&self) -> String {
        if self.use_colors {
            color::Fg(color::Yellow).to_string()
        } else {
            String::default()
        }
    }

    fn red(&self) -> String {
        if self.use_colors {
            color::Fg(color::Red).to_string()
        } else {
            String::default()
        }
    }

    fn reset(&self) -> String {
        if self.use_colors {
            style::Reset.to_string()
        } else {
            String::default()
        }
    }
}

impl<O: OutputSink> Clone for Writer<O> {
    fn clone(&self) -> Self {
        Self {
            output: self.output.clone(),
            muted: self.muted,
            use_colors: self.use_colors,
            file: Rc::clone(&self.file),
            buffered: Arc::clone(&self.buffered),
        }
    }
}

/// `StdioSink` sends output to standard output and standard error.
#[derive(Clone, Debug)]
pub struct StdioSink {
    /// Indicates this sink is connected to a tty, and can support raw terminal mode.
    pub is_tty: bool,
}

impl OutputSink for StdioSink {
    fn write_all(&self, buf: &[u8]) {
        self.raw_write(io::stdout(), buf).expect("failed to write to stdout");
    }

    fn print<D: Display>(&self, message: D) {
        let formatted = format!("{}", message);
        self.raw_write(io::stdout(), formatted.as_bytes()).expect("failed to write to stdout");
    }

    fn error<D: Display>(&self, message: D) {
        let formatted = format!("{}\n", message);
        self.raw_write(io::stderr(), formatted.as_bytes()).expect("failed to write to stderr");
    }
}

impl StdioSink {
    fn raw_write<W: Write>(&self, mut w: W, buf: &[u8]) -> Result<()> {
        if !self.is_tty {
            w.write_all(buf)?;
            return Ok(());
        }
        let bufs = buf.split_inclusive(|&c| c == '\n' as u8);
        for buf in bufs.into_iter() {
            let last = buf.last().unwrap();
            if *last == '\n' as u8 {
                // TTY may be in "raw" mode; move back to the start of the line on newline.
                w.write_all(&buf[0..buf.len() - 1])?;
                w.write_all("\r\n".as_bytes())?;
            } else {
                w.write_all(buf)?;
            }
        }
        Ok(())
    }
}
