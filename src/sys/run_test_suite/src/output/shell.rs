// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use log::error;
use parking_lot::Mutex;
use std::{
    collections::HashMap,
    io::{Error, Write},
    sync::{atomic::AtomicU32, Arc},
    time::Duration,
};

/// A handle around an inner writer. This serves as a "multiplexing" writer that
/// writes bytes from multiple sources into a single serial destination, typically
/// to stdout.
/// Output sent to a handle is buffered until a newline is encountered, then the
/// buffered output is written to the inner writer.
/// The handle also supports prepending a prefix to the start of each buffer. This
/// helps preserve existing behavior where prefixes are added to the start of stdout
/// and log lines to help a developer understand what produced some output.
struct ShellWriterHandle<W: 'static + Write + Send + Sync> {
    inner: Arc<Mutex<W>>,
    buffer: Vec<u8>,
    /// Prefix, if any, to prepend to output before writing to the inner writer.
    prefix: Option<Vec<u8>>,
}

impl<W: 'static + Write + Send + Sync> ShellWriterHandle<W> {
    const NEWLINE_BYTE: u8 = b'\n';
    const BUFFER_CAPACITY: usize = 1024;

    /// Create a new handle to a wrapped writer.
    fn new_handle(inner: Arc<Mutex<W>>, prefix: Option<String>) -> Self {
        Self {
            inner,
            buffer: Vec::with_capacity(Self::BUFFER_CAPACITY),
            prefix: prefix.map(String::into_bytes),
        }
    }

    /// Write a full line to the inner writer.
    fn write_bufs(writer: &mut W, bufs: &[&[u8]]) -> Result<(), Error> {
        for buf in bufs {
            writer.write_all(buf)?;
        }
        Ok(())
    }
}

impl<W: 'static + Write + Send + Sync> Write for ShellWriterHandle<W> {
    fn write(&mut self, buf: &[u8]) -> Result<usize, Error> {
        // find the last newline in the buffer. In case multiple lines are written as once,
        // we should write once to the inner writer and add our prefix only once. This helps
        // avoid spamming the output with prefixes in case many lines are present.
        let newline_pos = buf
            .iter()
            .rev()
            .position(|byte| *byte == Self::NEWLINE_BYTE)
            .map(|pos_from_end| buf.len() - pos_from_end - 1);
        // In case we'd exceed the buffer, just wrte everything, but append a newline to avoid
        // interspersing.
        let (final_byte_pos, append_newline) = match newline_pos {
            // no newline, pushing all to buffer would exceed capacity
            None if self.buffer.len() + buf.len() > Self::BUFFER_CAPACITY => (buf.len() - 1, true),
            None => {
                self.buffer.extend_from_slice(buf);
                return Ok(buf.len());
            }
            // newline exists, but the rest of buf would exceed capacity.
            Some(pos) if buf.len() - pos > Self::BUFFER_CAPACITY => (buf.len() - 1, true),
            Some(pos) => (pos, false),
        };

        let mut bufs_to_write = vec![];
        if let Some(prefix) = self.prefix.as_ref() {
            bufs_to_write.push(prefix.as_slice());
        }
        if !self.buffer.is_empty() {
            bufs_to_write.push(self.buffer.as_slice());
        }
        bufs_to_write.push(&buf[..final_byte_pos + 1]);
        if append_newline {
            bufs_to_write.push(&[Self::NEWLINE_BYTE]);
        }

        Self::write_bufs(&mut self.inner.lock(), bufs_to_write.as_slice())?;

        self.buffer.clear();
        self.buffer.extend_from_slice(&buf[final_byte_pos + 1..]);
        Ok(buf.len())
    }

    fn flush(&mut self) -> Result<(), Error> {
        let mut writer = self.inner.lock();
        if !self.buffer.is_empty() {
            self.buffer.push(Self::NEWLINE_BYTE);
            let mut bufs_to_write = vec![];
            if let Some(prefix) = &self.prefix {
                bufs_to_write.push(prefix.as_slice());
            }
            bufs_to_write.push(self.buffer.as_slice());

            Self::write_bufs(&mut writer, bufs_to_write.as_slice())?;
            self.buffer.clear();
        }
        writer.flush()
    }
}

impl<W: 'static + Write + Send + Sync> std::ops::Drop for ShellWriterHandle<W> {
    fn drop(&mut self) {
        let _ = self.flush();
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::io::ErrorKind;

    #[test]
    fn single_handle() {
        let output: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![]));
        let mut write_handle = ShellWriterHandle::new_handle(output.clone(), None);

        assert_eq!(write_handle.write(b"hello world").unwrap(), b"hello world".len(),);
        assert!(output.lock().is_empty());

        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len(),);
        assert_eq!(output.lock().as_slice(), b"hello world\n");

        assert_eq!(write_handle.write(b"flushed output").unwrap(), b"flushed output".len(),);
        write_handle.flush().unwrap();
        assert_eq!(output.lock().as_slice(), b"hello world\nflushed output\n");
    }

    #[test]
    fn single_handle_with_prefix() {
        let output: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![]));
        let mut write_handle =
            ShellWriterHandle::new_handle(output.clone(), Some("[prefix] ".to_string()));

        assert_eq!(write_handle.write(b"hello world").unwrap(), b"hello world".len(),);
        assert!(output.lock().is_empty());

        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len(),);
        assert_eq!(output.lock().as_slice(), b"[prefix] hello world\n");

        assert_eq!(write_handle.write(b"flushed output").unwrap(), b"flushed output".len(),);
        write_handle.flush().unwrap();
        assert_eq!(output.lock().as_slice(), b"[prefix] hello world\n[prefix] flushed output\n");
    }

    #[test]
    fn single_handle_multiple_line() {
        let output: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![]));
        let mut write_handle = ShellWriterHandle::new_handle(output.clone(), None);
        const WRITE_BYTES: &[u8] = b"This is a \nmultiline output \nwithout newline termination";
        assert_eq!(write_handle.write(WRITE_BYTES).unwrap(), WRITE_BYTES.len(),);
        assert_eq!(output.lock().as_slice(), b"This is a \nmultiline output \n");
        write_handle.flush().unwrap();
        assert_eq!(
            output.lock().as_slice(),
            b"This is a \nmultiline output \nwithout newline termination\n"
        );
        output.lock().clear();

        const TERMINATED_BYTES: &[u8] = b"This is \nnewline terminated \noutput\n";
        assert_eq!(write_handle.write(TERMINATED_BYTES).unwrap(), TERMINATED_BYTES.len(),);
        assert_eq!(output.lock().as_slice(), b"This is \nnewline terminated \noutput\n");
    }

    #[test]
    fn single_handle_exceed_buffer_in_single_write() {
        const CAPACITY: usize = ShellWriterHandle::<Vec<u8>>::BUFFER_CAPACITY;
        // each case consists of a sequence of pairs, where each pair is a string to write, and
        // the expected output after writing the string.
        let cases = vec![
            (
                "exceed in one write",
                vec![("a".repeat(CAPACITY + 1), format!("{}\n", "a".repeat(CAPACITY + 1)))],
            ),
            (
                "exceed on second write",
                vec![
                    ("a".to_string(), "".to_string()),
                    ("a".repeat(CAPACITY + 1), format!("{}\n", "a".repeat(CAPACITY + 2))),
                ],
            ),
            (
                "exceed in one write, with newline",
                vec![(
                    format!("\n{}", "a".repeat(CAPACITY + 1)),
                    format!("\n{}\n", "a".repeat(CAPACITY + 1)),
                )],
            ),
            (
                "exceed in two writes, with newline",
                vec![
                    ("a".to_string(), "".to_string()),
                    (
                        format!("\n{}", "a".repeat(CAPACITY + 1)),
                        format!("a\n{}\n", "a".repeat(CAPACITY + 1)),
                    ),
                ],
            ),
        ];

        for (case_name, writes) in cases.into_iter() {
            let output: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![]));
            let mut write_handle = ShellWriterHandle::new_handle(output.clone(), None);
            for (write_no, (to_write, expected)) in writes.into_iter().enumerate() {
                assert_eq!(
                    write_handle.write(to_write.as_bytes()).unwrap(),
                    to_write.as_bytes().len(),
                    "Got wrong number of bytes written for write {:?} in case {}",
                    write_no,
                    case_name
                );
                assert_eq!(
                    String::from_utf8(output.lock().clone()).unwrap(),
                    expected,
                    "Buffer contains unexpected contents after write {:?} in case {}",
                    write_no,
                    case_name
                )
            }
        }
    }

    #[test]
    fn single_handle_with_prefix_multiple_line() {
        let output: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![]));
        let mut write_handle =
            ShellWriterHandle::new_handle(output.clone(), Some("[prefix] ".to_string()));
        const WRITE_BYTES: &[u8] = b"This is a \nmultiline output \nwithout newline termination";
        assert_eq!(write_handle.write(WRITE_BYTES).unwrap(), WRITE_BYTES.len(),);
        // Note we 'chunk' output in each write to avoid spamming the prefix, so the second
        // line won't contain the prefix.
        assert_eq!(output.lock().as_slice(), b"[prefix] This is a \nmultiline output \n");
        write_handle.flush().unwrap();
        assert_eq!(
            output.lock().as_slice(),
            "[prefix] This is a \nmultiline output \n[prefix] without newline termination\n"
                .as_bytes()
        );
        output.lock().clear();

        const TERMINATED_BYTES: &[u8] = b"This is \nnewline terminated \noutput\n";
        assert_eq!(write_handle.write(TERMINATED_BYTES).unwrap(), TERMINATED_BYTES.len(),);
        assert_eq!(output.lock().as_slice(), b"[prefix] This is \nnewline terminated \noutput\n");
    }

    #[test]
    fn multiple_handles() {
        let output: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![]));
        let mut handle_1 = ShellWriterHandle::new_handle(output.clone(), Some("[1] ".to_string()));
        let mut handle_2 = ShellWriterHandle::new_handle(output.clone(), Some("[2] ".to_string()));

        write!(handle_1, "hi from 1").unwrap();
        write!(handle_2, "hi from 2").unwrap();
        assert!(output.lock().is_empty());
        write!(handle_1, "\n").unwrap();
        assert_eq!(output.lock().as_slice(), "[1] hi from 1\n".as_bytes());
        write!(handle_2, "\n").unwrap();
        assert_eq!(output.lock().as_slice(), "[1] hi from 1\n[2] hi from 2\n".as_bytes());
    }

    // The following tests verify behavior of the shell writer when the inner writer
    // exhibits some allowed edge cases.

    #[test]
    fn output_written_when_inner_writer_writes_partial_buffer() {
        /// A writer that writes at most 3 bytes at a time.
        struct PartialOutputWriter(Vec<u8>);
        impl Write for PartialOutputWriter {
            fn write(&mut self, buf: &[u8]) -> Result<usize, Error> {
                if buf.len() >= 3 {
                    self.0.write(&buf[..3])
                } else {
                    self.0.write(buf)
                }
            }

            fn flush(&mut self) -> Result<(), Error> {
                self.0.flush()
            }
        }

        let output = Arc::new(Mutex::new(PartialOutputWriter(vec![])));
        let mut write_handle =
            ShellWriterHandle::new_handle(output.clone(), Some("[prefix] ".to_string()));
        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert!(output.lock().0.is_empty());
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().0.as_slice(), b"[prefix] hello\n");

        output.lock().0.clear();
        let mut write_handle_2 =
            ShellWriterHandle::new_handle(output.clone(), Some("[prefix2] ".to_string()));

        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert_eq!(write_handle_2.write(b"world").unwrap(), b"world".len());
        assert!(output.lock().0.is_empty());
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().0.as_slice(), b"[prefix] hello\n");
        assert_eq!(write_handle_2.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().0.as_slice(), b"[prefix] hello\n[prefix2] world\n");
    }

    #[test]
    fn output_written_when_inner_writer_returns_interrupted() {
        /// A writer that returns interrupted on the first write attempt
        struct InterruptWriter {
            buf: Vec<u8>,
            returned_interrupt: bool,
        };
        impl Write for InterruptWriter {
            fn write(&mut self, buf: &[u8]) -> Result<usize, Error> {
                if !self.returned_interrupt {
                    self.returned_interrupt = true;
                    Err(ErrorKind::Interrupted.into())
                } else {
                    self.buf.write(buf)
                }
            }

            fn flush(&mut self) -> Result<(), Error> {
                self.buf.flush()
            }
        }

        let output =
            Arc::new(Mutex::new(InterruptWriter { buf: vec![], returned_interrupt: false }));
        let mut write_handle =
            ShellWriterHandle::new_handle(output.clone(), Some("[prefix] ".to_string()));
        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert!(output.lock().buf.is_empty());
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().buf.as_slice(), b"[prefix] hello\n");

        output.lock().buf.clear();
        let mut write_handle_2 =
            ShellWriterHandle::new_handle(output.clone(), Some("[prefix2] ".to_string()));

        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert_eq!(write_handle_2.write(b"world").unwrap(), b"world".len());
        assert!(output.lock().buf.is_empty());
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().buf.as_slice(), b"[prefix] hello\n");
        assert_eq!(write_handle_2.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().buf.as_slice(), b"[prefix] hello\n[prefix2] world\n");
    }
}
