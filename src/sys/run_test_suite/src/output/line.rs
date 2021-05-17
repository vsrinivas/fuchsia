// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::{Error, ErrorKind, Write};
use vte::{Parser, Perform};

/// A trait for objects that write line delimited strings.
pub trait WriteLine {
    /// Writes a string and terminates it will a line break.
    fn write_line(&mut self, s: &str) -> Result<(), Error> {
        self.write_line_segments(&[s])
    }

    /// Write a series of segments, and terminate it with a line break.
    fn write_line_segments(&mut self, segments: &[&str]) -> Result<(), Error>;
}

impl<W: Write> WriteLine for W {
    fn write_line_segments(&mut self, segments: &[&str]) -> Result<(), Error> {
        for segment in segments {
            self.write_all(segment.as_bytes())?;
        }
        writeln!(self)
    }
}

impl WriteLine for Box<dyn WriteLine + Send + Sync> {
    fn write_line_segments(&mut self, segments: &[&str]) -> Result<(), Error> {
        self.as_mut().write_line_segments(segments)
    }
}

/// A writer that writes to two writers.
pub struct MultiplexedWriter<A: WriteLine, B: WriteLine> {
    a: A,
    b: B,
}

impl<A: WriteLine, B: WriteLine> WriteLine for MultiplexedWriter<A, B> {
    fn write_line_segments(&mut self, segments: &[&str]) -> Result<(), Error> {
        self.a.write_line_segments(segments)?;
        self.b.write_line_segments(segments)
    }
}

impl<A: WriteLine, B: WriteLine> MultiplexedWriter<A, B> {
    pub fn new(a: A, b: B) -> Self {
        Self { a, b }
    }
}

/// A wrapper around a `Write` that filters out ANSI escape sequences before writing to the
/// wrapped object.
pub struct AnsiFilterWriter<W: WriteLine> {
    inner: W,
}

impl<W: WriteLine> AnsiFilterWriter<W> {
    pub fn new(inner: W) -> Self {
        Self { inner }
    }
}

impl<W: WriteLine> WriteLine for AnsiFilterWriter<W> {
    fn write_line_segments(&mut self, segments: &[&str]) -> Result<(), Error> {
        match segments.len() {
            1 => self.write_line(segments[0]),
            // It's possible to do this without this copy. This is currently unused as the ansi
            // filter is always top of the chain, but if this needs to be nested lower in a chain
            // of filters we should implement it properly.
            _ => self.write_line(&segments.join("")),
        }
    }

    fn write_line(&mut self, s: &str) -> Result<(), Error> {
        let bytes = s.as_bytes();
        let mut parser = Parser::new();
        let mut segments = vec![];

        // Contains range [x1, x2) for the last known chunk of non-ANSI characters
        let mut last_known_printable_chunk: Option<(usize, usize)> = None;

        for (idx, byte) in bytes.iter().enumerate() {
            let mut new_printable_bytes = 0;
            parser.advance(&mut PrintableBytes(&mut new_printable_bytes), *byte);
            let new_char_start_idx = match new_printable_bytes {
                0 => None,
                num_bytes => Some(idx + 1 - num_bytes),
            };

            match (last_known_printable_chunk, new_char_start_idx) {
                // new char is part of old chunk
                (Some(prev_chunk), Some(new_char_idx)) if new_char_idx <= prev_chunk.1 => {
                    last_known_printable_chunk = Some((prev_chunk.0, idx + 1));
                }
                // new char is part of a new chunk
                (Some(prev_chunk), Some(new_char_idx)) => {
                    let new_segment = std::str::from_utf8(&bytes[prev_chunk.0..prev_chunk.1])
                        .map_err(|_| ErrorKind::InvalidData)?;
                    segments.push(new_segment);
                    last_known_printable_chunk = Some((new_char_idx, idx + 1));
                }
                (Some(_), None) => (),
                (None, Some(new_char_idx)) => {
                    last_known_printable_chunk = Some((new_char_idx, idx + 1));
                }
                (None, None) => (),
            }
        }
        if let Some(chunk) = last_known_printable_chunk {
            let new_segment = std::str::from_utf8(&bytes[chunk.0..chunk.1])
                .map_err(|_| ErrorKind::InvalidData)?;
            segments.push(new_segment);
        }
        self.inner.write_line_segments(&segments)
    }
}

/// A `Perform` implementation that tracks how many previous bytes constitute a valid UTF-8
/// character. This relies on strings in rust always being UTF-8 encoded.
struct PrintableBytes<'a>(&'a mut usize);

const PRINTABLE_COMMAND_CHARS: [u8; 3] = ['\n' as u8, '\r' as u8, '\t' as u8];

impl<'a> Perform for PrintableBytes<'a> {
    fn print(&mut self, c: char) {
        *self.0 = c.len_utf8();
    }

    fn execute(&mut self, code: u8) {
        if PRINTABLE_COMMAND_CHARS.contains(&code) {
            *self.0 = 1;
        }
    }
    fn hook(&mut self, _: &[i64], _: &[u8], _: bool) {}
    fn put(&mut self, _: u8) {}
    fn unhook(&mut self) {}
    fn osc_dispatch(&mut self, _: &[&[u8]]) {}
    fn csi_dispatch(&mut self, _: &[i64], _: &[u8], _: bool, _: char) {}
    fn esc_dispatch(&mut self, _: &[i64], _: &[u8], _: bool, _: u8) {}
}

#[cfg(test)]
mod test {
    use super::*;
    use ansi_term::{Color, Style};

    #[test]
    fn multiplexed_writer() {
        const WRITTEN: &str = "test output";
        const EXPECTED: &str = "test output\n";

        let mut buf_1: Vec<u8> = vec![];
        let mut buf_2: Vec<u8> = vec![];
        let mut multiplexed_writer = MultiplexedWriter::new(&mut buf_1, &mut buf_2);

        multiplexed_writer.write_line(WRITTEN).expect("write_line failed");
        assert_eq!(std::str::from_utf8(&buf_1).unwrap(), EXPECTED);
        assert_eq!(std::str::from_utf8(&buf_2).unwrap(), EXPECTED);
    }

    #[test]
    fn no_ansi_unaffected() {
        let cases = vec![
            "simple_case",
            "newline\ncase",
            "[INFO]: some log () <>",
            "1",
            "こんにちは",
            "מבחן 15 מבחן 20",
        ];

        for case in cases {
            let mut output: Vec<u8> = vec![];
            let mut filter_writer = AnsiFilterWriter::new(&mut output);
            filter_writer.write_line(case).expect("write_line failed");

            assert_eq!(
                format!("{}\n", case),
                String::from_utf8(output).expect("Failed to parse UTF8"),
            );
        }
    }

    #[test]
    fn ansi_filtered() {
        let cases = vec![
            (format!("{}", Color::Blue.paint("blue string")), "blue string\n"),
            (format!("{}", Color::Blue.bold().paint("newline\nstr")), "newline\nstr\n"),
            (format!("{}", Color::Blue.bold().paint("tab\tstr")), "tab\tstr\n"),
            (format!("{}", Style::new().bold().paint("bold")), "bold\n"),
            (
                format!(
                    "{} {}",
                    Style::new().bold().paint("bold"),
                    Style::new().bold().paint("bold-2")
                ),
                "bold bold-2\n",
            ),
            (format!("{}", Style::new().bold().paint("")), "\n"),
            (format!("no format, {}", Color::Blue.paint("format")), "no format, format\n"),
        ];

        for (case, expected) in cases {
            let mut output: Vec<u8> = vec![];
            let mut filter_writer = AnsiFilterWriter::new(&mut output);
            filter_writer.write_line(&case).expect("write_line failed");

            assert_eq!(expected, String::from_utf8(output).expect("Couldn't parse utf8"));
        }
    }

    #[test]
    fn ansi_multiline_filtered() {
        // Ensure ansi escapes passed through multiple lines still filtered.
        let multiline = format!("{}", Color::Blue.paint("multiline\nstring"));
        let split = multiline.split_ascii_whitespace().collect::<Vec<_>>();
        assert_eq!(split.len(), 2);

        let mut output: Vec<u8> = vec![];
        let mut filter_writer = AnsiFilterWriter::new(&mut output);

        for s in split {
            filter_writer.write_line(&s).expect("write_line failed");
        }

        assert_eq!("multiline\nstring\n", String::from_utf8(output).expect("Couldn't parse utf8"));
    }

    #[test]
    fn malformed_ansi_contained() {
        // Ensure malformed ansi is contained to a single line
        let malformed = "\u{1b}[31mmalformed\u{1b}";
        let okay = format!("{}", Color::Blue.paint("okay"));

        let mut output: Vec<u8> = vec![];
        let mut filter_writer = AnsiFilterWriter::new(&mut output);

        filter_writer.write_line(malformed).expect("write_line failed");
        filter_writer.write_line(&okay).expect("write_line failed");
        assert_eq!("malformed\nokay\n", String::from_utf8(output).expect("Couldn't parse utf8"));
    }
}
