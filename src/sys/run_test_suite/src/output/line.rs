// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io::{Error, Write};
use vte::{Parser, Perform};

/// A wrapper around a `Write` that filters out ANSI escape sequences before writing to the
/// wrapped object.
/// AnsiFilterWriter assumes the bytes are valid UTF8, and clears its state on newline in an
/// attempt to recover from malformed inputs.
pub struct AnsiFilterWriter<W: Write> {
    inner: W,
    parser: Parser,
}

impl<W: Write> AnsiFilterWriter<W> {
    pub fn new(inner: W) -> Self {
        Self { inner, parser: Parser::new() }
    }
}

impl<W: Write> Write for AnsiFilterWriter<W> {
    fn write(&mut self, bytes: &[u8]) -> Result<usize, Error> {
        // Per Rust docs write does not need to consume all the bytes, and
        // each call to write should represent at most a single attempt to write.
        // To be as close as possible to "a single attempt to write" we write only
        // the first chunk of writable bytes.
        let mut printable_range: Option<(usize, usize)> = None;

        for (idx, byte) in bytes.iter().enumerate() {
            let mut found = FoundChars::Nothing;
            self.parser.advance(&mut found, *byte);
            if let &FoundChars::Newline(_) = &found {
                self.parser = Parser::new();
            }

            match found {
                FoundChars::Nothing => (),
                FoundChars::PrintableChars(num_bytes) | FoundChars::Newline(num_bytes) => {
                    match printable_range {
                        None => {
                            printable_range = Some((idx + 1 - num_bytes, idx + 1));
                        }
                        Some(mut range) if range.1 == idx + 1 - num_bytes => {
                            range.1 = idx + 1;
                        }
                        Some(_) => break,
                    }
                }
            }
        }
        match printable_range {
            None => Ok(bytes.len()),
            Some(range) => {
                self.inner.write_all(&bytes[range.0..range.1])?;
                Ok(range.1)
            }
        }
    }

    fn flush(&mut self) -> Result<(), Error> {
        self.inner.flush()
    }
}

/// An implementation of |Perform| that reports the characters found by the parser.
enum FoundChars {
    Nothing,
    PrintableChars(usize),
    Newline(usize),
}

const PRINTABLE_COMMAND_CHARS: [u8; 2] = ['\r' as u8, '\t' as u8];

impl Perform for FoundChars {
    fn print(&mut self, c: char) {
        *self = Self::PrintableChars(c.len_utf8());
    }

    fn execute(&mut self, code: u8) {
        if code == b'\n' {
            *self = Self::Newline(1);
        } else if PRINTABLE_COMMAND_CHARS.contains(&code) {
            *self = Self::PrintableChars(1);
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
            filter_writer.write_all(case.as_bytes()).expect("write_all failed");
            drop(filter_writer);

            assert_eq!(case, String::from_utf8(output).expect("Failed to parse UTF8"),);
        }
    }

    #[test]
    fn ansi_filtered() {
        let cases = vec![
            (format!("{}", Color::Blue.paint("blue string")), "blue string"),
            (format!("{}", Color::Blue.bold().paint("newline\nstr")), "newline\nstr"),
            (format!("{}", Color::Blue.bold().paint("tab\tstr")), "tab\tstr"),
            (format!("{}", Style::new().bold().paint("bold")), "bold"),
            (
                format!(
                    "{} {}",
                    Style::new().bold().paint("bold"),
                    Style::new().bold().paint("bold-2")
                ),
                "bold bold-2",
            ),
            (format!("{}", Style::new().bold().paint("")), ""),
            (format!("no format, {}", Color::Blue.paint("format")), "no format, format"),
        ];

        for (case, expected) in cases {
            let mut output: Vec<u8> = vec![];
            let mut filter_writer = AnsiFilterWriter::new(&mut output);
            write!(filter_writer, "{}", case).expect("write failed");

            drop(filter_writer);
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
            writeln!(filter_writer, "{}", s).expect("write failed");
        }

        drop(filter_writer);
        assert_eq!("multiline\nstring\n", String::from_utf8(output).expect("Couldn't parse utf8"));
    }

    #[test]
    fn malformed_ansi_contained() {
        // Ensure malformed ansi is contained to a single line
        let malformed = "\u{1b}[31mmalformed\u{1b}\n";
        let okay = format!("{}\n", Color::Blue.paint("okay"));

        let mut output: Vec<u8> = vec![];
        let mut filter_writer = AnsiFilterWriter::new(&mut output);

        filter_writer.write_all(malformed.as_bytes()).expect("write_all failed");
        filter_writer.write_all(okay.as_bytes()).expect("write_all failed");
        drop(filter_writer);
        assert_eq!("malformed\nokay\n", String::from_utf8(output).expect("Couldn't parse utf8"));
    }

    /// A |Write| implementation that only partially writes a buffer on write().
    struct PartialWriter<W: Write>(W);
    const PARTIAL_WRITE_BYTES: usize = 3;

    impl<W: Write> Write for PartialWriter<W> {
        fn write(&mut self, bytes: &[u8]) -> Result<usize, Error> {
            let slice_to_write = if bytes.len() < PARTIAL_WRITE_BYTES {
                bytes
            } else {
                &bytes[..PARTIAL_WRITE_BYTES]
            };
            self.0.write_all(slice_to_write)?;
            Ok(slice_to_write.len())
        }

        fn flush(&mut self) -> Result<(), Error> {
            Ok(())
        }
    }

    #[test]
    fn ansi_filter_inner_partial_write() {
        let cases = vec![
            (format!("{}", Color::Blue.paint("multiline\nstring")), "multiline\nstring"),
            ("simple no ansi".to_string(), "simple no ansi"),
            ("a\nb\nc\nd".to_string(), "a\nb\nc\nd"),
        ];

        for (unfiltered, filtered) in cases.iter() {
            let mut output: Vec<u8> = vec![];
            let mut filter_writer = AnsiFilterWriter::new(PartialWriter(&mut output));
            filter_writer.write_all(unfiltered.as_bytes()).expect("write all");
            drop(filter_writer);
            assert_eq!(&String::from_utf8(output).expect("couldn't parse UTF8"), filtered);
        }
    }
}
