// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::ffx_bail,
    ffx_config::get,
    ffx_core::ffx_plugin,
    ffx_daemon_log_args::LogCommand,
    log::error,
    notify::Watcher,
    std::{
        collections::VecDeque,
        fs::File,
        io::{self, BufRead, BufReader, Seek},
        path::{Path, PathBuf},
        sync::{mpsc, Arc, Mutex},
        time::Duration,
    },
};

#[ffx_plugin()]
pub async fn daemon(cmd: LogCommand) -> Result<()> {
    if !get("log.enabled").await? {
        ffx_bail!("Logging is not enabled.");
    }
    let mut log_path: PathBuf = get("log.dir").await?;
    log_path.push("ffx.daemon.log");
    let mut log_file = LogFile::new(&log_path);

    let stdout = io::stdout();
    let mut out = stdout.lock();

    if let Some(line_count) = cmd.line_count {
        log_file.write_all_tail(&mut out, line_count)?;
    } else {
        log_file.write_all(&mut out)?;
    }

    if cmd.follow {
        let (_watcher, rx) = LogWatcher::new(&log_path)?;

        loop {
            rx.recv().unwrap();
            log_file.write_all(&mut out)?;
        }
    }

    Ok(())
}

struct LogFile<'a> {
    path: &'a Path,
    len: u64,
}

impl<'a> LogFile<'a> {
    fn new(path: &'a Path) -> Self {
        Self { path, len: 0 }
    }

    /// Open the log and return the position next to print logs from.
    fn open_log(&mut self) -> Result<(File, u64)> {
        let mut file = File::open(&self.path).context("failed to open daemon log")?;

        // Always start from the beginning if we haven't read from the file.
        if self.len == 0 {
            return Ok((file, 0));
        }

        // Otherwise, see if the file size has shrunk, which suggests the file was truncated.
        let file_len = file.metadata()?.len();

        if file_len < self.len {
            eprintln!("{}: file truncated", self.path.display());
            Ok((file, 0))
        } else {
            file.seek(io::SeekFrom::Start(self.len))?;
            Ok((file, self.len))
        }
    }

    /// Print out a file from the seek position to the end of the file.
    fn write_all(&mut self, out: &mut impl io::Write) -> Result<()> {
        let (file, mut pos) = self.open_log()?;
        let mut reader = BufReader::new(file);
        let mut buf = String::new();

        loop {
            let n = reader.read_line(&mut buf)?;
            if n == 0 {
                break;
            }
            pos += n as u64;
            out.write_all(buf.as_bytes())?;
            out.flush()?;
            buf.clear();
        }

        self.len = pos;

        Ok(())
    }

    /// Print out the last `line_count` lines of the daemon log file. This always prints from the
    /// beginning of the file.
    fn write_all_tail(&mut self, out: &mut impl io::Write, line_count: usize) -> Result<()> {
        let (file, mut pos) = self.open_log()?;
        let mut reader = BufReader::new(file);

        // Read though the file line by line and push them into a deque of the `line_count` size.
        let mut lines = VecDeque::with_capacity(line_count);
        let mut buf = String::new();

        loop {
            let n = reader.read_line(&mut buf)?;
            if n == 0 {
                break;
            }
            pos += n as u64;
            if lines.len() == line_count {
                lines.pop_front();
            }
            lines.push_back(buf.clone());
            buf.clear();
        }

        for line in lines {
            out.write_all(line.as_bytes())?;
            out.flush()?;
        }

        self.len = pos;

        Ok(())
    }
}

// FIXME(https://github.com/notify-rs/notify/pull/336) We can use Watcher as a trait object once
// this PR lands. That would let us get rid of much of this duplication.
enum LogWatcher {
    Recommended(notify::RecommendedWatcher),
    Poll(notify::PollWatcher),
}

impl LogWatcher {
    fn new(path: &Path) -> Result<(Self, mpsc::Receiver<()>)> {
        // Next, set up a watch, and wait for the file to change.
        let (tx, rx) = mpsc::sync_channel(1);
        let tx = Arc::new(Mutex::new(tx));
        let make_event_fn = move || {
            let tx = Arc::clone(&tx);
            move |event: notify::Result<notify::Event>| {
                match event {
                    Ok(_) => {
                        // Signal that the log changed. It's okay if the channel is full, since that
                        // just means the receiver hasn't processed a previous event yet. It's also
                        // okay if we're disconnected, since that just means we're shutting down.
                        let _ = tx.lock().expect("lock not to be poisoned").try_send(());
                    }
                    Err(err) => {
                        error!("error reading events: {}", err);
                    }
                }
            }
        };

        match notify::immediate_watcher(make_event_fn()) {
            Ok(mut watcher) => {
                watcher.watch(path, notify::RecursiveMode::NonRecursive)?;
                Ok((LogWatcher::Recommended(watcher), rx))
            }
            Err(_) => {
                // Fall back to a polling watcher.
                let mut watcher = notify::PollWatcher::with_delay(
                    Arc::new(Mutex::new(make_event_fn())),
                    Duration::from_secs(1),
                )?;
                watcher.watch(path, notify::RecursiveMode::NonRecursive)?;
                Ok((LogWatcher::Poll(watcher), rx))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{fs, io::Write},
    };

    fn write_log(file: &mut impl Write, lines: std::ops::Range<usize>) {
        for line_idx in lines {
            writeln!(file, "line {}", line_idx).unwrap();
        }
    }

    #[test]
    fn display_x_most_recent_from_many_logs() {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        write_log(&mut tmp, 1..20);

        let mut log_file = LogFile::new(tmp.path());
        let mut actual = vec![];
        log_file.write_all_tail(&mut actual, 10).unwrap();

        let mut expected = vec![];
        write_log(&mut expected, 10..20);
        assert_eq!(std::str::from_utf8(&actual).unwrap(), std::str::from_utf8(&expected).unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn display_x_most_recent_from_not_enough_logs() {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        write_log(&mut tmp, 1..5);

        let mut log_file = LogFile::new(tmp.path());
        let mut actual = vec![];
        log_file.write_all_tail(&mut actual, 10).unwrap();

        let mut expected = vec![];
        write_log(&mut expected, 1..5);
        assert_eq!(std::str::from_utf8(&actual).unwrap(), std::str::from_utf8(&expected).unwrap());
    }

    #[test]
    fn test_follow() {
        let tmp = tempfile::NamedTempFile::new().unwrap().into_temp_path();
        let mut log_file = LogFile::new(&tmp);
        let (_watcher, rx) = LogWatcher::new(&tmp).unwrap();

        let mut file = fs::OpenOptions::new().append(true).open(&tmp).unwrap();
        file.set_len(0).unwrap();
        file.write_all(b"hello\n").unwrap();
        drop(file);

        // Wait to get signaled that the file changed, then read the file.
        rx.recv_timeout(Duration::from_secs(5)).unwrap();
        let mut buf = vec![];
        log_file.write_all(&mut buf).unwrap();
        assert_eq!(&buf, b"hello\n");

        // Next, append some more to the log, which should be appended to our buffer.
        let mut file = fs::OpenOptions::new().append(true).open(&tmp).unwrap();
        file.write_all(b"world\n").unwrap();
        drop(file);

        // Wait to detect the change.
        rx.recv_timeout(Duration::from_secs(5)).unwrap();
        log_file.write_all(&mut buf).unwrap();
        assert_eq!(&buf, b"hello\nworld\n");

        // Make sure if the file is truncated we read from the beginning.
        let mut file = fs::OpenOptions::new().write(true).truncate(true).open(&tmp).unwrap();
        file.write_all(b"wee\n").unwrap();
        drop(file);

        // Wait to detect the change.
        rx.recv_timeout(Duration::from_secs(5)).unwrap();
        buf.clear();
        log_file.write_all(&mut buf).unwrap();
        assert_eq!(&buf, b"wee\n");
    }

    #[test]
    fn test_follow_last_logs() {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        write_log(&mut tmp, 1..20);
        let tmp = tmp.into_temp_path();

        let mut log_file = LogFile::new(&tmp);
        let (_watcher, rx) = LogWatcher::new(&tmp).unwrap();

        // Collect the last few lines.
        let mut actual = vec![];
        log_file.write_all_tail(&mut actual, 10).unwrap();

        // Make sure we only get the last few lines.
        let mut expected = vec![];
        write_log(&mut expected, 10..20);
        assert_eq!(std::str::from_utf8(&actual).unwrap(), std::str::from_utf8(&expected).unwrap());

        // Append a few more lines to the log.
        let mut file = fs::OpenOptions::new().append(true).open(&tmp).unwrap();
        write_log(&mut file, 21..40);
        drop(file);

        // Wait to get signaled that the file changed, then read the file.
        rx.recv_timeout(Duration::from_secs(5)).unwrap();

        // Read in the next set of logs. We should get all the logs written.
        let mut actual = vec![];
        log_file.write_all(&mut actual).unwrap();
        let mut expected = vec![];
        write_log(&mut expected, 21..40);
        assert_eq!(std::str::from_utf8(&actual).unwrap(), std::str::from_utf8(&expected).unwrap());
    }
}
