// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::writer::{OutputSink, Writer},
    anyhow::{anyhow, Context as _, Result},
    diagnostics_data::LogsData,
    fidl_fuchsia_fuzzer as fuzz,
    futures::io::ReadHalf,
    futures::{join, AsyncReadExt},
    serde_json::Deserializer,
    std::cell::RefCell,
    std::path::Path,
};

/// Compostion of `SocketForwarder`s for standard output, standard errors, and system logs.
#[derive(Debug)]
pub struct Forwarder<O: OutputSink> {
    stdout: SocketForwarder<O>,
    stderr: SocketForwarder<O>,
    syslog: SocketForwarder<O>,
    writer: Writer<O>,
}

impl<O: OutputSink> Forwarder<O> {
    /// Creates a `Forwarder` that can forward data from the given sockets to the `writer`.
    ///
    /// If an `output` path is provided, output will be duplicated to the following files under that
    /// directory:
    ///
    ///   * fuzzer.stdout.txt
    ///   * fuzzer.stderr.txt
    ///   * fuzzer.syslog.json
    ///
    pub fn try_new<P: AsRef<Path>>(
        stdout: fidl::Socket,
        stderr: fidl::Socket,
        syslog: fidl::Socket,
        output: Option<P>,
        writer: &Writer<O>,
    ) -> Result<Self> {
        let (out, err, log) = match output.as_ref() {
            Some(output) => {
                let out = writer
                    .tee(output, "fuzzer.stdout.txt")
                    .context("failed to create file for stdout")?;
                let err = writer
                    .tee(output, "fuzzer.stderr.txt")
                    .context("failed to create file for stderr")?;
                let log = writer
                    .tee(output, "fuzzer.syslog.json")
                    .context("failed to create file for syslog")?;
                (out, err, log)
            }
            None => (writer.clone(), writer.clone(), writer.clone()),
        };
        let stdout = SocketForwarder::try_new(stdout, out)
            .context("failed to create forwarder for stdout")?;
        let stderr = SocketForwarder::try_new(stderr, err)
            .context("failed to create forwarder for stderr")?;
        let syslog = SocketForwarder::try_new(syslog, log)
            .context("failed to create forwarder for syslog")?;
        Ok(Self { stdout, stderr, syslog, writer: writer.clone() })
    }

    pub fn writer(&self) -> &Writer<O> {
        &self.writer
    }

    /// Forwards output from the fuzzer to the `Writer` using each of the `SocketForwarder`s.
    pub async fn forward_all(&self) -> Result<()> {
        let results = join!(
            self.stdout.forward_text(),
            self.stderr.forward_text(),
            self.syslog.forward_json(),
        );
        results.0.context("failed to forward stdout")?;
        results.1.context("failed to forward stderr")?;
        results.2.context("failed to forward syslog")?;
        Ok(())
    }
}

#[derive(Debug)]
struct SocketForwarder<O: OutputSink> {
    reader: RefCell<ReadHalf<fidl::AsyncSocket>>,
    writer: Writer<O>,
}

impl<O: OutputSink> SocketForwarder<O> {
    fn try_new(socket: fidl::Socket, writer: Writer<O>) -> Result<Self> {
        let socket = fidl::AsyncSocket::from_socket(socket).context("failed to convert socket")?;
        let (reader, _) = socket.split();
        Ok(Self { reader: RefCell::new(reader), writer })
    }

    async fn forward_text(&self) -> Result<()> {
        let mut reader = self.reader.borrow_mut();
        let mut buf: [u8; 2048] = [0; 2048];
        let mut raw = Vec::new();
        let newline = '\n' as u8;
        let done_marker = format!("{}\n", fuzz::DONE_MARKER);
        let done_marker = done_marker.as_bytes();
        loop {
            match reader.read(&mut buf).await.context("failed to read text data from socket")? {
                0 => {
                    self.writer.write_all(&raw);
                    return Ok(());
                }
                num_read => raw.extend_from_slice(&buf[0..num_read]),
            };
            let data = raw;
            raw = Vec::new();
            for message in data.split_inclusive(|&x| x == newline) {
                if message == done_marker {
                    return Ok(());
                } else if message.last() == Some(&newline) {
                    self.writer.write_all(&message);
                } else {
                    raw = message.to_vec();
                }
            }
        }
    }

    async fn forward_json(&self) -> Result<()> {
        let mut reader = self.reader.borrow_mut();
        let mut buf: [u8; 2048] = [0; 2048];
        let mut raw = Vec::new();
        loop {
            match reader.read(&mut buf).await.context("failed to read JSON data from socket")? {
                0 => {
                    self.writer.write_all(&raw);
                    return Ok(());
                }
                num_read => raw.extend_from_slice(&buf[0..num_read]),
            };
            let deserializer = Deserializer::from_slice(&raw);
            let mut stream = deserializer.into_iter::<Vec<LogsData>>();
            while let Some(items) = stream.next() {
                let logs_data = match items {
                    Err(e) if e.is_eof() => break,
                    other => other,
                }
                .map_err(|e| anyhow!(format!("serde_json: {:?}", e)))
                .context("failed to deserialize")?;
                for log_data in logs_data.into_iter() {
                    if let Some(message) = log_data.msg() {
                        if message == fuzz::DONE_MARKER {
                            return Ok(());
                        }
                    }
                    self.writer.log(log_data);
                }
            }
            let num_read = stream.byte_offset();
            raw.drain(0..num_read);
        }
    }
}

#[cfg(test)]
pub mod test_fixtures {
    use {
        anyhow::Result,
        diagnostics_data::{BuilderArgs, LogsDataBuilder, Severity},
        futures::AsyncWriteExt,
    };

    /// Generates a system log entry from the given `msg` and sends it to the given `socket`.
    ///
    /// This mimics logs generated using //src/lib/diagnostics/data. These logs include:
    ///   * A timestamp in nanoseconds when the log was generated.
    ///   * The URL of the component generating the log.
    ///   * The component moniker (https://fuchsia.dev/fuchsia-src/reference/components/moniker)
    ///   * The log severity, e.g. fatal, error, warning, etc.
    ///   * The log message.
    ///
    /// See also `writer::Writer::log` which formats these entries for display.
    pub async fn send_log_entry<S: AsRef<str>>(
        socket: &mut fidl::AsyncSocket,
        msg: S,
    ) -> Result<()> {
        let builder_args = BuilderArgs {
            timestamp_nanos: 0.into(),
            component_url: Some(String::default()),
            moniker: "moniker".to_string(),
            severity: Severity::Info,
        };
        let builder = LogsDataBuilder::new(builder_args);
        let logs_data = vec![builder.set_message(msg.as_ref()).build()];
        let bytes = serde_json::to_vec(&logs_data)?;
        socket.write_all(&bytes).await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::test_fixtures::send_log_entry,
        super::{Forwarder, SocketForwarder},
        crate::util::test_fixtures::Test,
        anyhow::{Error, Result},
        diagnostics_data::LogsData,
        fidl::{Socket, SocketOpts},
        fidl_fuchsia_fuzzer as fuzz,
        futures::{join, AsyncWriteExt},
        std::fs,
    };

    #[fuchsia::test]
    async fn test_forward_text() -> Result<()> {
        let mut test = Test::try_new()?;
        let (tx, rx) = Socket::create(SocketOpts::STREAM)?;
        let forwarder = SocketForwarder::try_new(rx, test.writer().clone())?;
        let forward_fut = forwarder.forward_text();
        let socket_fut = || async move {
            let mut tx = fidl::AsyncSocket::from_socket(tx)?;
            tx.write_all(b"hello\nworld!\n").await?;
            let done_marker = format!("{}\n", fuzz::DONE_MARKER);
            tx.write_all(done_marker.as_bytes()).await?;
            Ok::<(), Error>(())
        };
        test.output_matches("hello");
        test.output_matches("world!");
        let socket_fut = socket_fut();
        let results = join!(forward_fut, socket_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_forward_json() -> Result<()> {
        let mut test = Test::try_new()?;
        let (tx, rx) = Socket::create(SocketOpts::STREAM)?;
        let forwarder = SocketForwarder::try_new(rx, test.writer().clone())?;
        let forward_fut = forwarder.forward_json();
        let socket_fut = || async move {
            let mut tx = fidl::AsyncSocket::from_socket(tx)?;
            send_log_entry(&mut tx, "hello world").await?;
            send_log_entry(&mut tx, fuzz::DONE_MARKER).await?;
            Ok::<(), Error>(())
        };
        let socket_fut = socket_fut();
        let results = join!(forward_fut, socket_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        test.output_matches("[0.000][moniker][][I] hello world");
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_forward_all() -> Result<()> {
        let mut test = Test::try_new()?;
        let (stdout_tx, stdout_rx) = Socket::create(SocketOpts::STREAM)?;
        let (stderr_tx, stderr_rx) = Socket::create(SocketOpts::STREAM)?;
        let (syslog_tx, syslog_rx) = Socket::create(SocketOpts::STREAM)?;
        let forwarder =
            Forwarder::try_new::<&str>(stdout_rx, stderr_rx, syslog_rx, None, test.writer())?;
        let forward_fut = forwarder.forward_all();

        let done_marker = format!("{}\n", fuzz::DONE_MARKER);
        let done_marker_bytes = done_marker.as_bytes();

        let a_done_marker = format!("a{}\n", fuzz::DONE_MARKER);
        test.output_matches(a_done_marker.clone());

        let done_marker_a = format!("{}a\n", fuzz::DONE_MARKER);
        test.output_matches(done_marker_a.clone());

        let socket_fut = || async move {
            let mut stdout_tx = fidl::AsyncSocket::from_socket(stdout_tx)?;
            let mut stderr_tx = fidl::AsyncSocket::from_socket(stderr_tx)?;
            let mut syslog_tx = fidl::AsyncSocket::from_socket(syslog_tx)?;

            // Streams can be sent in any order
            send_log_entry(&mut syslog_tx, fuzz::DONE_MARKER).await?;

            // Data sent after the done marker should not be received.
            stdout_tx.write_all(done_marker_bytes).await?;
            stdout_tx.write_all(b"after\n").await?;

            // Done marker must be exactly delimited by newlines, and can arrive in pieces.
            stderr_tx.write_all(a_done_marker.as_bytes()).await?;
            stderr_tx.write_all(done_marker_a.as_bytes()).await?;
            for i in 0..done_marker_bytes.len() {
                stderr_tx.write_all(&done_marker_bytes[i..i + 1]).await?;
            }
            stderr_tx.write_all(b"after\n").await?;
            Ok::<(), Error>(())
        };

        let socket_fut = socket_fut();
        let results = join!(forward_fut, socket_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        test.verify_output()
    }

    #[fuchsia::test]
    async fn test_forward_to_file() -> Result<()> {
        let test = Test::try_new()?;
        let output_dir = test.create_dir("output")?;

        let (stdout_tx, stdout_rx) = Socket::create(SocketOpts::STREAM)?;
        let (stderr_tx, stderr_rx) = Socket::create(SocketOpts::STREAM)?;
        let (syslog_tx, syslog_rx) = Socket::create(SocketOpts::STREAM)?;
        let forwarder =
            Forwarder::try_new(stdout_rx, stderr_rx, syslog_rx, Some(&output_dir), test.writer())?;
        let forward_fut = forwarder.forward_all();
        let sockets_fut = || async move {
            // Write all in one shot.
            let mut stdout_tx = fidl::AsyncSocket::from_socket(stdout_tx)?;
            stdout_tx.write_all(b"hello world!").await?;

            // Write all in pieces.
            let mut stderr_tx = fidl::AsyncSocket::from_socket(stderr_tx)?;
            stderr_tx.write_all(b"hel").await?;
            stderr_tx.write_all(b"lo ").await?;
            stderr_tx.write_all(b"wor").await?;
            stderr_tx.write_all(b"ld!").await?;

            // Write JSON. This should be made prettier when copying, e.g. newlines, spaces, etc.
            let mut syslog_tx = fidl::AsyncSocket::from_socket(syslog_tx)?;
            send_log_entry(&mut syslog_tx, "hello world!").await?;
            Ok::<(), Error>(())
        };
        let sockets_fut = sockets_fut();
        let results = join!(forward_fut, sockets_fut);
        assert!(results.0.is_ok());
        assert!(results.1.is_ok());
        assert_eq!(fs::read(output_dir.join("fuzzer.stdout.txt"))?, b"hello world!");
        assert_eq!(fs::read(output_dir.join("fuzzer.stderr.txt"))?, b"hello world!");
        let data = fs::read(output_dir.join("fuzzer.syslog.json"))?;
        let logs_data: LogsData = serde_json::from_slice(&data)?;
        assert_eq!(logs_data.msg(), Some("hello world!"));
        Ok(())
    }
}
