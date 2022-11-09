// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod u64_arithmetic;

use {
    anyhow::{anyhow, Context, Result},
    std::{
        fmt::Debug,
        fs::File,
        io::{BufReader, Read, Seek, SeekFrom},
    },
    thiserror::Error,
    u64_arithmetic::{abs, named_u64, u64_add, u64_sub, U64Eval},
};

/// A fallible `Clone` trait.
pub trait TryClone: Sized {
    fn try_clone(&self) -> Result<Self>;
}

impl<T: Clone> TryClone for T {
    fn try_clone(&self) -> Result<Self> {
        Ok(self.clone())
    }
}

/// A wrapper type that implements `TryClone` for `BufReader<File>`.
pub struct TryClonableBufReaderFile(BufReader<File>);

impl From<BufReader<File>> for TryClonableBufReaderFile {
    fn from(buf_reader_file: BufReader<File>) -> Self {
        Self(buf_reader_file)
    }
}

impl TryClone for TryClonableBufReaderFile {
    fn try_clone(&self) -> Result<Self> {
        Ok(Self(
            self.0
                .get_ref()
                .try_clone()
                .map(BufReader::new)
                .map_err(|err| anyhow!("Failed to reopen file for clone: {}", err))?,
        ))
    }
}

impl Read for TryClonableBufReaderFile {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.0.read(buf)
    }
}

impl Seek for TryClonableBufReaderFile {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        self.0.seek(pos)
    }
}

/// A trait for use in contexts that require `<std::io::Read + std::io::Seek>`, which, as written,
/// is not allowed.
pub trait ReadSeek: Read + Seek {}

impl<T: Read + Seek> ReadSeek for T {}

/// Convert an `anyhow::Error` to a `std::io::Error`.
fn anyhow_to_io(error: anyhow::Error) -> std::io::Error {
    std::io::Error::new(std::io::ErrorKind::Other, error)
}

/// Modeled custom errors that can be encountered performing read and seek operations on a
/// `WrappedReaderSeeker`. Note that standard `std::io::Error` values may also be returned from the
/// underlying `std::io::Read + std::io::Seek`.
#[derive(Debug, Error)]
#[error("Reader-seeker position {position} out of bounds [{lower_bound}, {upper_bound}]")]
pub struct WrappedReaderSeekerError {
    position: u64,
    lower_bound: u64,
    upper_bound: u64,
}

/// A builder pattern for `WrappedReaderSeeker`, which wraps a `std::io::Read + std::io::Seek` to
/// form a restricted `[offset, length)` window.
pub struct WrappedReaderSeekerBuilder<RS: Read + Seek> {
    reader_seeker: Option<RS>,
    offset: Option<u64>,
    length: Option<u64>,
}

impl<RS: Read + Seek> WrappedReaderSeekerBuilder<RS> {
    /// Construct an uninitialized builder.
    pub fn new() -> Self {
        Self { reader_seeker: None, offset: None, length: None }
    }

    /// Set the `std::io::Read + std::io::Seek` to wrap.
    pub fn reader_seeker(mut self, reader_seeker: RS) -> Self {
        self.reader_seeker = Some(reader_seeker);
        self
    }

    /// Set the start `offset` in the restricted `[offset, length)` window.
    pub fn offset(mut self, offset: u64) -> Self {
        self.offset = Some(offset);
        self
    }

    /// Set the total `length` in the restricted `[offset, length)` window.
    pub fn length(mut self, length: u64) -> Self {
        self.length = Some(length);
        self
    }

    /// Build a `WrappedReaderSeeker`. This operation seeks the underlying
    /// `std::io::Read + std::io::Seek` to `self.offset.unwrap()`. This computation may fail if:
    /// 1. `reader_seeker`, `offset`, or `length` are unset, or
    /// 2. `offset + length` exceeds `u64::MAX`, or
    /// 3. `self.reader_seeker.seek(std::io::SeekFrom(self.offset.unwrap()))` fails.
    pub fn build(self) -> Result<WrappedReaderSeeker<RS>> {
        let mut reader_seeker = self.reader_seeker.ok_or_else(|| {
            anyhow!("Attempt to build wrapped reader-seeker with no underlying reader-seeker")
        })?;
        let offset = self
            .offset
            .ok_or_else(|| anyhow!("Attempt to build wrapped reader-seeker with no offset"))?;
        let length = self
            .length
            .ok_or_else(|| anyhow!("Attempt to build wrapped reader-seeker with no length"))?;

        let offset_expression = named_u64("offset", offset);
        let length_expression = named_u64("length", length);
        let total_expression = u64_add(&offset_expression, &length_expression);
        total_expression
            .eval()
            .context("attempt to build wrapped reader-seeker with impossible size")?;

        reader_seeker.seek(SeekFrom::Start(offset))?;
        Ok(WrappedReaderSeeker { reader_seeker, offset: offset, length })
    }
}

/// A `Clone + std::io::Read + std::io::Seek` that wraps another
/// `Clone + std::io::Read + std::io::Seek`, and applies a restricted `[self.offset, self.length)`
/// window to read and seek operations.
#[derive(Clone)]
pub struct WrappedReaderSeeker<RS: Read + Seek> {
    reader_seeker: RS,
    offset: u64,
    length: u64,
}

impl<RS: Read + Seek> WrappedReaderSeeker<RS> {
    /// Construct the default builder for `WrappedReaderSeeker`.
    pub fn builder() -> WrappedReaderSeekerBuilder<RS> {
        WrappedReaderSeekerBuilder::new()
    }

    /// Consume this wrapper and return the underlying `Clone + std::io::Read + std::io::Seek`.
    pub fn into_inner(self) -> RS {
        self.reader_seeker
    }
}

impl<RS: Read + Seek> Read for WrappedReaderSeeker<RS> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        let position = named_u64("position", self.reader_seeker.seek(SeekFrom::Current(0))?);
        let offset = named_u64("offset", self.offset);
        let length = named_u64("length", self.length);
        let absolute_start = offset.clone();
        let absolute_end = u64_add(&offset, &length);
        {
            let position = position.eval().map_err(anyhow_to_io)?;
            let absolute_start = absolute_start.eval().map_err(anyhow_to_io)?;
            let absolute_end = absolute_end.eval().map_err(anyhow_to_io)?;
            if position < absolute_start || position > absolute_end {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::Other,
                    WrappedReaderSeekerError {
                        position,
                        lower_bound: absolute_start,
                        upper_bound: absolute_end,
                    },
                ));
            }
        }

        let buffer_size = named_u64("buffer_size", buf.len() as u64);
        let full_buffer_absolute_end = u64_add(&position, &buffer_size);
        match full_buffer_absolute_end.eval() {
            Ok(full_buffer_absolute_end) => {
                if full_buffer_absolute_end > absolute_end.eval().map_err(anyhow_to_io)? {
                    // Clip read length by size of WrappedReaderSeeker window.
                    let read_length = u64_sub(&absolute_end, &position);
                    self.reader_seeker
                        .read(&mut buf[..read_length.eval().map_err(anyhow_to_io)? as usize])
                } else {
                    // Buffer fits inside WrappedReaderSeeker window; read into whole buffer.
                    self.reader_seeker.read(buf)
                }
            }
            Err(_) => {
                // Calculating new position after complete buffer read would overflow; clip read
                // length by size of WrappedReaderSeeker window.
                let read_length = u64_sub(&absolute_end, &position);
                self.reader_seeker
                    .read(&mut buf[..read_length.eval().map_err(anyhow_to_io)? as usize])
            }
        }
    }
}

// fn seek_new_position(
//     pos: SeekFrom,
//     position: &NamedU64,
//     offset: &NamedU64,
//     absolute_end: &U64Expression<'_, '_, NamedU64, NamedU64>,
// ) -> impl U64Eval {
//     match pos {
//         SeekFrom::Current(pos) => {
//             if pos >= 0 {
//                 let pos = named_u64("offset_from_current", pos as u64);
//                 let new_position = u64_add(position, &pos);
//                 new_position
//             } else {
//                 let negative_pos = named_u64("-offset_from_current", abs(pos));
//                 let new_position = u64_sub(position, &negative_pos);
//                 new_position
//             }
//         }
//         SeekFrom::End(pos) => {
//             if pos >= 0 {
//                 let pos = named_u64("offset_from_end", pos as u64);
//                 let new_position = u64_add(absolute_end, &pos);
//                 new_position
//             } else {
//                 let negative_pos = named_u64("-offset_from_end", u64_arithmetic::abs(pos));
//                 let new_position = u64_sub(absolute_end, &negative_pos);
//                 new_position
//             }
//         }
//         SeekFrom::Start(pos) => {
//             let pos = named_u64("offset_from_start", pos);
//             let new_position = u64_add(offset, &pos);
//             new_position
//         }
//     }
// }

// fn lift_u64_eval<T: U64Eval>(value: T) -> impl U64Eval {
//     value
// }

impl<RS: Read + Seek> Seek for WrappedReaderSeeker<RS> {
    fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
        let position = named_u64("position", self.reader_seeker.seek(SeekFrom::Current(0))?);
        let offset = named_u64("offset", self.offset);
        let length = named_u64("length", self.length);
        // let absolute_start = offset.clone();
        let absolute_end = u64_add(&offset, &length);

        // let new_position = seek_new_position(pos, &position, &offset, &absolute_end);
        let new_position = match pos {
            SeekFrom::Current(pos) => {
                if pos >= 0 {
                    let pos = named_u64("offset_from_current", pos as u64);
                    {
                        let new_position = u64_add(&position, &pos);
                        new_position.eval()
                    }
                } else {
                    let negative_pos = named_u64("-offset_from_current", abs(pos));
                    let new_position = u64_sub(&position, &negative_pos);
                    new_position.eval()
                }
            }
            SeekFrom::End(pos) => {
                if pos >= 0 {
                    let pos = named_u64("offset_from_end", pos as u64);
                    let new_position = u64_add(&absolute_end, &pos);
                    new_position.eval()
                } else {
                    let negative_pos = named_u64("-offset_from_end", u64_arithmetic::abs(pos));
                    let new_position = u64_sub(&absolute_end, &negative_pos);
                    new_position.eval()
                }
            }
            SeekFrom::Start(pos) => {
                let pos = named_u64("offset_from_start", pos);
                let new_position = u64_add(&offset, &pos);
                new_position.eval()
            }
        }
        .map_err(anyhow_to_io)?;

        let absolute_start = offset.eval().map_err(anyhow_to_io)?;
        let absolute_end = absolute_end.eval().map_err(anyhow_to_io)?;
        if new_position < absolute_start || new_position > absolute_end {
            return Err(std::io::Error::new(
                std::io::ErrorKind::Other,
                WrappedReaderSeekerError {
                    position: new_position,
                    lower_bound: absolute_start,
                    upper_bound: absolute_end,
                },
            ));
        }

        self.reader_seeker.seek(SeekFrom::Start(new_position))?;
        Ok(new_position - self.offset)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            u64_arithmetic::{
                named_u64, u64_add, u64_sub, U64ArithmeticError, U64Operation,
                NEGATIVE_I64_MIN_AS_U64,
            },
            WrappedReaderSeeker, WrappedReaderSeekerError,
        },
        anyhow::Context,
        std::io::{Read, Seek, SeekFrom},
    };

    /// A `Clone + std::io::Read + std::io::Seek` that reads `b'\0'` bytes within the range
    /// `[0, self.length)`. Seeking is unrestricted, and uses naive unchecked arithmetic.
    #[derive(Clone)]
    struct FakeReaderSeeker {
        position: u64,
        length: u64,
    }

    impl FakeReaderSeeker {
        fn new(position: u64, length: u64) -> Self {
            Self { position, length }
        }
    }

    impl Read for FakeReaderSeeker {
        /// Read `b'\0'` bytes within the ranage `[0, self.length)`.
        fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
            let read_length = std::cmp::min(
                std::cmp::min(u64::MAX as usize, buf.len()),
                (self.length - self.position) as usize,
            );
            let mut idx = 0;
            loop {
                if idx >= read_length {
                    break;
                }

                buf[idx] = b'\0';
                idx += 1;
            }
            let read_length = read_length as u64;
            self.position += read_length;
            Ok(read_length as usize)
        }
    }

    impl Seek for FakeReaderSeeker {
        /// Seek unrestricted using naive unchecked arithmetic.
        fn seek(&mut self, pos: SeekFrom) -> std::io::Result<u64> {
            match pos {
                SeekFrom::Current(pos) => {
                    let current_position = self.position as i64;
                    let position = current_position + pos;
                    self.position = position as u64;
                }
                SeekFrom::End(pos) => {
                    let length = self.length as i64;
                    self.position = (length + pos) as u64;
                }
                SeekFrom::Start(pos) => {
                    self.position = pos as u64;
                }
            }
            Ok(self.position as u64)
        }
    }

    #[fuchsia::test]
    fn full_buffer_read() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();
        let mut buffer: [u8; 5] = [0; 5];
        let read_size = reader_seeker.read(&mut buffer).unwrap();
        assert_eq!(read_size, 5);
    }

    #[fuchsia::test]
    fn undersized_read() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();
        let mut buffer: [u8; 20] = [0; 20];
        let read_size = reader_seeker.read(&mut buffer).unwrap();
        assert_eq!(read_size, 10);
    }

    #[fuchsia::test]
    fn undersized_read_oversized_wrapper() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(10)
            .length(20)
            .build()
            .unwrap();
        let mut buffer: [u8; 20] = [0; 20];
        let read_size = reader_seeker.read(&mut buffer).unwrap();
        assert_eq!(read_size, 10);
    }

    #[fuchsia::test]
    fn undersized_empty_read() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(10)
            .length(0)
            .build()
            .unwrap();
        let mut buffer: [u8; 20] = [0; 20];
        let read_size = reader_seeker.read(&mut buffer).unwrap();
        assert_eq!(read_size, 0);
    }

    #[fuchsia::test]
    fn internal_window_overflow() {
        assert!(WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(1)
            .length(u64::MAX)
            .build()
            .is_err());
    }

    #[fuchsia::test]
    fn read_new_offset_would_overflow() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(u64::MAX - 1, u64::MAX))
            .offset(u64::MAX - 1)
            .length(1)
            .build()
            .unwrap();

        let mut buffer: [u8; 2] = [1; 2];
        let read_size = reader_seeker.read(&mut buffer).unwrap();
        assert_eq!(read_size, 1);
        assert_eq!(buffer[0], 0);
        assert_eq!(buffer[1], 1);
    }

    #[fuchsia::test]
    fn seek_read_empty() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let offset = reader_seeker.seek(SeekFrom::Current(10)).unwrap();
        assert_eq!(offset, 10);

        let mut buffer: [u8; 20] = [0; 20];
        let read_size = reader_seeker.read(&mut buffer).unwrap();
        assert_eq!(read_size, 0);

        let mut inner = reader_seeker.into_inner();
        let offset = inner.seek(SeekFrom::Current(0)).unwrap();
        assert_eq!(offset, 15);
    }

    #[fuchsia::test]
    fn seek_forward_back() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let offset = reader_seeker.seek(SeekFrom::Current(5)).unwrap();
        assert_eq!(offset, 5);

        let offset = reader_seeker.seek(SeekFrom::Current(-3)).unwrap();
        assert_eq!(offset, 2);

        let mut inner = reader_seeker.into_inner();
        let offset = inner.seek(SeekFrom::Current(0)).unwrap();
        assert_eq!(offset, 7);
    }

    #[fuchsia::test]
    fn seek_from_start() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let offset = reader_seeker.seek(SeekFrom::Start(5)).unwrap();
        assert_eq!(offset, 5);

        let mut inner = reader_seeker.into_inner();
        let offset = inner.seek(SeekFrom::Current(0)).unwrap();
        assert_eq!(offset, 10);
    }

    #[fuchsia::test]
    fn seek_from_end() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let offset = reader_seeker.seek(SeekFrom::End(-2)).unwrap();
        assert_eq!(offset, 8);

        let mut inner = reader_seeker.into_inner();
        let offset = inner.seek(SeekFrom::Current(0)).unwrap();
        assert_eq!(offset, 13);
    }

    fn assert_io_error(actual: std::io::Error, expected: &(dyn std::error::Error + Send + Sync)) {
        let actual_debug = format!("{:?}", actual.get_ref().unwrap());
        let expected_debug = format!("{:?}", expected);
        assert_eq!(actual_debug, expected_debug);
        let actual_display = format!("{}", actual.get_ref().unwrap());
        let expected_display = format!("{}", expected);
        assert_eq!(actual_display, expected_display);
    }

    #[fuchsia::test]
    fn seek_before_start() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let expected_error =
            WrappedReaderSeekerError { position: 4, lower_bound: 5, upper_bound: 15 };
        let actual_io_error = reader_seeker.seek(SeekFrom::Current(-1)).err().unwrap();
        assert_io_error(actual_io_error, &expected_error);
    }

    #[fuchsia::test]
    fn seek_before_start_from_end() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let expected_error =
            WrappedReaderSeekerError { position: 4, lower_bound: 5, upper_bound: 15 };
        let actual_io_error = reader_seeker.seek(SeekFrom::End(-11)).err().unwrap();
        assert_io_error(actual_io_error, &expected_error);
    }

    #[fuchsia::test]
    fn seek_after_end() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let expected_error =
            WrappedReaderSeekerError { position: 16, lower_bound: 5, upper_bound: 15 };
        let actual_io_error = reader_seeker.seek(SeekFrom::Start(11)).err().unwrap();
        assert_io_error(actual_io_error, &expected_error);
    }

    #[fuchsia::test]
    fn seek_after_end_from_current() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let expected_error =
            WrappedReaderSeekerError { position: 16, lower_bound: 5, upper_bound: 15 };
        let actual_io_error = reader_seeker.seek(SeekFrom::Current(11)).err().unwrap();
        assert_io_error(actual_io_error, &expected_error);
    }

    #[fuchsia::test]
    fn seek_after_end_from_end() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, 20))
            .offset(5)
            .length(10)
            .build()
            .unwrap();

        let expected_error =
            WrappedReaderSeekerError { position: 16, lower_bound: 5, upper_bound: 15 };
        let actual_io_error = reader_seeker.seek(SeekFrom::End(1)).err().unwrap();
        assert_io_error(actual_io_error, &expected_error);
    }

    #[fuchsia::test]
    fn seek_overflow_from_start() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, u64::MAX))
            .offset(1)
            .length(u64::MAX - 1)
            .build()
            .unwrap();

        // Reconstruct failing expression to appropriately contextualize error.
        let offset = named_u64("offset", 1);
        let offset_from_start = named_u64("offset_from_start", u64::MAX);
        let new_offset = u64_add(&offset, &offset_from_start);
        let error_expression = new_offset;

        // Contextualize expected error.
        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(1, U64Operation::Add, u64::MAX));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_contextualized_error = expected_contextualized_result.err().unwrap();
        let expected_error: Box<dyn std::error::Error + Send + Sync> =
            expected_contextualized_error.into();

        let actual_io_error = reader_seeker.seek(SeekFrom::Start(u64::MAX)).err().unwrap();
        assert_io_error(actual_io_error, expected_error.as_ref());
    }

    #[fuchsia::test]
    fn seek_overflow_from_current() {
        let i64_max = i64::MAX as u64;
        let offset = u64::MAX - i64_max + 1;
        let length = u64::MAX - offset;
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, u64::MAX))
            .offset(offset)
            .length(length)
            .build()
            .unwrap();

        // Reconstruct failing expression to appropriately contextualize error.
        let position = named_u64("position", offset);
        let offset_from_current = named_u64("offset_from_current", i64_max);
        let new_offset = u64_add(&position, &offset_from_current);
        let error_expression = new_offset;

        // Contextualize expected error.
        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(offset, U64Operation::Add, i64_max));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_contextualized_error = expected_contextualized_result.err().unwrap();
        let expected_error: Box<dyn std::error::Error + Send + Sync> =
            expected_contextualized_error.into();

        let actual_io_error = reader_seeker.seek(SeekFrom::Current(i64::MAX)).err().unwrap();
        assert_io_error(actual_io_error, expected_error.as_ref());
    }

    #[fuchsia::test]
    fn seek_overflow_from_end() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, u64::MAX))
            .offset(1)
            .length(u64::MAX - 1)
            .build()
            .unwrap();

        // Reconstruct failing expression to appropriately contextualize error.
        let offset = named_u64("offset", 1);
        let length = named_u64("length", u64::MAX - 1);
        let offset_from_end = named_u64("offset_from_end", 1);
        let absolute_end = u64_add(&offset, &length);
        let new_offset = u64_add(&absolute_end, &offset_from_end);
        let error_expression = new_offset;

        // Contextualize expected error.
        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(u64::MAX, U64Operation::Add, 1));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_contextualized_error = expected_contextualized_result.err().unwrap();
        let expected_error: Box<dyn std::error::Error + Send + Sync> =
            expected_contextualized_error.into();

        let actual_io_error = reader_seeker.seek(SeekFrom::End(1)).err().unwrap();
        assert_io_error(actual_io_error, expected_error.as_ref());
    }

    #[fuchsia::test]
    fn seek_underflow_from_current() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, u64::MAX))
            .offset(0)
            .length(u64::MAX)
            .build()
            .unwrap();

        // Reconstruct failing expression to appropriately contextualize error.
        let position = named_u64("position", 0);
        let negative_offset_from_current = named_u64("-offset_from_current", 1);
        let new_offset = u64_sub(&position, &negative_offset_from_current);
        let error_expression = new_offset;

        // Contextualize expected error.
        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(0, U64Operation::Subtract, 1));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_contextualized_error = expected_contextualized_result.err().unwrap();
        let expected_error: Box<dyn std::error::Error + Send + Sync> =
            expected_contextualized_error.into();

        let actual_io_error = reader_seeker.seek(SeekFrom::Current(-1)).err().unwrap();
        assert_io_error(actual_io_error, expected_error.as_ref());
    }

    #[fuchsia::test]
    fn seek_underflow_from_end() {
        let mut reader_seeker = WrappedReaderSeeker::builder()
            .reader_seeker(FakeReaderSeeker::new(0, u64::MAX))
            .offset(0)
            .length(NEGATIVE_I64_MIN_AS_U64 - 1)
            .build()
            .unwrap();

        // Reconstruct failing expression to appropriately contextualize error.
        let offset = named_u64("offset", 0);
        let length = named_u64("length", NEGATIVE_I64_MIN_AS_U64 - 1);
        let negative_offset_from_end = named_u64("-offset_from_end", NEGATIVE_I64_MIN_AS_U64);
        let absolute_end = u64_add(&offset, &length);
        let new_offset = u64_sub(&absolute_end, &negative_offset_from_end);
        let error_expression = new_offset;

        // Contextualize expected error.
        let expected_result: anyhow::Result<usize, U64ArithmeticError> =
            Err(U64ArithmeticError::new(
                NEGATIVE_I64_MIN_AS_U64 - 1,
                U64Operation::Subtract,
                NEGATIVE_I64_MIN_AS_U64,
            ));
        let expected_contextualized_result = expected_result.context(error_expression.context());
        let expected_contextualized_error = expected_contextualized_result.err().unwrap();
        let expected_error: Box<dyn std::error::Error + Send + Sync> =
            expected_contextualized_error.into();

        let actual_io_error = reader_seeker.seek(SeekFrom::End(i64::MIN)).err().unwrap();
        assert_io_error(actual_io_error, expected_error.as_ref());
    }
}
