// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::output::{
        ArtifactType, DirectoryArtifactType, DynArtifact, DynDirectoryArtifact, EntityId,
        EntityInfo, ReportedOutcome, Reporter, Timestamp,
    },
    async_trait::async_trait,
    std::io::{Error, Write},
    vte::{Parser, Perform},
};

/// A reporter that composes an inner reporter. Filters out ANSI in artifact output.
pub(crate) struct AnsiFilterReporter<R: Reporter> {
    inner: R,
}

impl<R: Reporter> AnsiFilterReporter<R> {
    pub(crate) fn new(inner: R) -> Self {
        Self { inner }
    }
}

#[async_trait]
impl<R: Reporter> Reporter for AnsiFilterReporter<R> {
    async fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
        self.inner.new_entity(entity, name).await
    }

    async fn set_entity_info(&self, entity: &EntityId, info: &EntityInfo) {
        self.inner.set_entity_info(entity, info).await
    }

    async fn entity_started(&self, entity: &EntityId, timestamp: Timestamp) -> Result<(), Error> {
        self.inner.entity_started(entity, timestamp).await
    }

    async fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        timestamp: Timestamp,
    ) -> Result<(), Error> {
        self.inner.entity_stopped(entity, outcome, timestamp).await
    }

    async fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        self.inner.entity_finished(entity).await
    }

    async fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        let inner_artifact = self.inner.new_artifact(entity, artifact_type).await?;
        match artifact_type {
            // All the artifact types are enumerated here as we expect future artifacts
            // should not be filtered.
            ArtifactType::Stdout
            | ArtifactType::Stderr
            | ArtifactType::Syslog
            | ArtifactType::RestrictedLog => {
                Ok(Box::new(AnsiFilterWriter::new(inner_artifact)) as Box<DynArtifact>)
            }
        }
    }

    async fn new_directory_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &DirectoryArtifactType,
        component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        self.inner.new_directory_artifact(entity, artifact_type, component_moniker).await
    }
}

/// A wrapper around a `Write` that filters out ANSI escape sequences before writing to the
/// wrapped object.
/// AnsiFilterWriter assumes the bytes are valid UTF8, and clears its state on newline in an
/// attempt to recover from malformed inputs.
struct AnsiFilterWriter<W: Write> {
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
            if let &FoundChars::PrintableChars('\n') = &found {
                self.parser = Parser::new();
            }

            match found {
                FoundChars::Nothing => (),
                FoundChars::PrintableChars(char::REPLACEMENT_CHARACTER) => {
                    // replacement character needs to be handled specially since the invalid
                    // bytes could be a different length than REPLACEMENT_CHARACTER.
                    match printable_range {
                        None => {
                            self.inner.write_all("�".as_bytes())?;
                            return Ok(idx + 1);
                        }
                        Some(range) => {
                            // in this case, we don't know where the last "good" processed
                            // byte after the writable range is, so write the known good range.
                            // Return the index after the good range so that anything after is
                            // reprocessed and hits the None case on subsequent write.
                            self.inner.write_all(&bytes[range.0..range.1])?;
                            return Ok(range.1);
                        }
                    }
                }
                FoundChars::PrintableChars(character) => {
                    let character_len = character.len_utf8();
                    match printable_range.as_mut() {
                        // Character length could exceed the number of bytes we've processed in
                        // this write if part of a multibyte UTF8 character was processed in a
                        // previous call to write. In this case, we have to regenerate the
                        // chacter in memory so write only it and return immediately.
                        None if character_len > idx + 1 => {
                            let mut buf = [0u8; 4];
                            character.encode_utf8(&mut buf);
                            self.inner.write_all(&buf[..character_len])?;
                            return Ok(idx + 1);
                        }
                        None => {
                            printable_range = Some((idx + 1 - character_len, idx + 1));
                        }
                        Some(mut range) if range.1 == idx + 1 - character_len => {
                            range.1 = idx + 1;
                        }
                        // We've passed over a section of non-printable characters and found a new
                        // section of printable characters. We'll write the first printable range,
                        // and return the number of bytes until just before the new set of
                        // printable characters. The next write will essentially process the new
                        // printable character again. Since we already know the new printable
                        // character is a valid UTF8 character, reprocessing it should be fine.
                        Some(range) => {
                            self.inner.write_all(&bytes[range.0..range.1])?;
                            return Ok(idx + 1 - character_len);
                        }
                    }
                }
            }
        }
        if let Some(range) = printable_range {
            self.inner.write_all(&bytes[range.0..range.1])?;
        }
        // If we reach this far, we have processed all the bytes.
        Ok(bytes.len())
    }

    fn flush(&mut self) -> Result<(), Error> {
        self.inner.flush()
    }
}

/// An implementation of |Perform| that reports the characters found by the parser.
enum FoundChars {
    Nothing,
    PrintableChars(char),
}

const PRINTABLE_COMMAND_CHARS: [u8; 3] = ['\r' as u8, '\t' as u8, '\n' as u8];

impl Perform for FoundChars {
    fn print(&mut self, c: char) {
        *self = Self::PrintableChars(c);
    }

    fn execute(&mut self, code: u8) {
        if PRINTABLE_COMMAND_CHARS.contains(&code) {
            *self = Self::PrintableChars(code.into());
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
            "\twhitespace\ncase\r",
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
    fn ansi_partial_utf8_write() {
        // Verify that if a multibyte UTF8 character gets split across two writes, the character
        // is passed through.
        let cases = vec![
            "ß", // 2 byte character
            "beforeßafter",
            "ßafter",
            "beforeßafter",
            "￥", // 3 byte character
            "before￥after",
            "￥after",
            "before￥",
            "💝", // 4 byte character
            "before💝after",
            "💝after",
            "before💝",
        ];

        for case in cases {
            let bytes = case.as_bytes();
            for split_point in 1..bytes.len() {
                let mut output: Vec<u8> = vec![];
                let mut filter_writer = AnsiFilterWriter::new(&mut output);

                filter_writer.write_all(&bytes[..split_point]).expect("write slice");
                filter_writer.write_all(&bytes[split_point..]).expect("write slice");
                assert_eq!(
                    output, bytes,
                    "Failed on case {} split on byte {:?}",
                    case, split_point
                );
            }
        }
    }

    #[test]
    fn ansi_handle_invalid_utf8() {
        // invalid bytes constructed according to rules in
        // https://en.wikipedia.org/wiki/UTF-8#Encoding
        const TWO_BYTE: [u8; 2] = [0xC2u8, 0xC2u8];
        const THREE_BYTE: [u8; 3] = [0xE0u8, 0xA0u8, 0xC2u8];
        const FOUR_BYTE: [u8; 4] = [0xF0u8, 0xA0u8, 0x82u8, 0xC2u8];

        let cases = vec![
            ([b"string".as_slice(), TWO_BYTE.as_slice()].concat(), "string�"),
            ([b"string".as_slice(), THREE_BYTE.as_slice()].concat(), "string�"),
            ([b"string".as_slice(), FOUR_BYTE.as_slice()].concat(), "string�"),
            ([TWO_BYTE.as_slice(), b"string".as_slice()].concat(), "�string"),
            ([THREE_BYTE.as_slice(), b"string".as_slice()].concat(), "�string"),
            ([FOUR_BYTE.as_slice(), b"string".as_slice()].concat(), "�string"),
        ];

        for (bytes, expected) in cases {
            let mut output: Vec<u8> = vec![];
            let mut filter_writer = AnsiFilterWriter::new(&mut output);

            filter_writer.write_all(&bytes).expect("write slice");
            assert_eq!(
                output,
                expected.as_bytes(),
                "Failed on case {:?}, expected string {}",
                bytes,
                expected
            );
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
