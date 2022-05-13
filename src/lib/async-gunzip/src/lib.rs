// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Async gzip decompression for a [`Stream`] of bytes.

use bytes::Bytes;
use errors::wrap_error;
use futures::prelude::*;
use futures::stream::FusedStream;

mod asyncbufread_to_stream;
mod errors;

pub use errors::{DecodeError, Error};

/// Size of decompressed chunks (except possibly the last chunk,
/// which may be smaller).
const OUTPUT_CHUNK_SIZE: usize = 32 * 1024;

/// Decode a stream of gzip-compressed data.
///
/// The input stream is a sequence of chunks, each of which may contain
/// any number of bytes.
///
/// Output chunks are typically 32 KiB each, but this behavior should not
/// be relied upon.
///
/// # Errors
///
/// If the input stream yields an error, or we detect that the input is not
/// well-formed gzip, we stop decoding immediately and yield an error.
///
/// In this case, some output chunks may already have been yielded. However,
/// we will _not_ attempt to yield an additional (partial) output chunk before
/// yielding the error.
///
/// No more output chunks or errors are yielded after the first error.
/// Subsequent calls to `.next()` return `None`.
pub fn decode<B, E>(
    compressed_input: impl Stream<Item = Result<B, E>>,
) -> impl FusedStream<Item = Result<Bytes, Error<E>>>
where
    B: AsRef<[u8]>,
    E: std::error::Error + Send + Sync + 'static,
{
    // TODO(kevan): when https://github.com/rust-lang/futures-rs/pull/2599 gets merged,
    // we can remove the Box.
    let compressed_input = Box::pin(compressed_input);

    // Wrap each error in an io::Error, so we can use the AsyncBufRead-based implementation.
    let compressed_input = compressed_input.map_err(wrap_error).into_async_read();

    let output = asyncbufread_to_stream::decode(compressed_input, OUTPUT_CHUNK_SIZE);

    // Unwrap each io::Error that contains an error from the underlying stream.
    output.map_err(Error::unwrap_inner_error)
}

#[cfg(test)]
mod tests {
    use super::*;
    use flate2::bufread::GzEncoder;
    use flate2::Compression;
    use futures::{executor, pin_mut};
    use rand::{Fill, SeedableRng};
    use rand_xorshift::XorShiftRng;
    use std::cmp::min;
    use std::fmt::Debug;
    use std::io::Read;

    #[derive(Debug, thiserror::Error)]
    pub(crate) enum MockError {
        #[error("bad thing happened: {0}")]
        BadThing(String),
    }

    fn mock_input_stream(
        uncompressed: &[u8],
        chunk_size: usize,
    ) -> impl Stream<Item = Result<Vec<u8>, MockError>> {
        let compressed = gzip_compress(&uncompressed);
        let chunks = split_into_chunks(&compressed, chunk_size);
        stream::iter(chunks).map(Ok)
    }

    pub(crate) fn gzip_compress(bytes: &[u8]) -> Vec<u8> {
        let mut out = vec![];
        GzEncoder::new(bytes, Compression::default()).read_to_end(&mut out).unwrap();
        out
    }

    pub(crate) fn split_into_chunks(mut bytes: &[u8], chunk_size: usize) -> Vec<Vec<u8>> {
        let mut chunks = Vec::with_capacity(ceil_div(bytes.len(), chunk_size));

        while !bytes.is_empty() {
            let len = min(bytes.len(), chunk_size);
            chunks.push(Vec::from(&bytes[..len]));
            bytes = &bytes[len..];
        }

        chunks
    }

    fn ceil_div(x: usize, y: usize) -> usize {
        let extra = if x % y != 0 { 1 } else { 0 };
        x / y + extra
    }

    /// Compress some data, then decompress it using [`decode`].
    fn assert_round_trip(uncompressed: &[u8], input_chunk_size: usize) {
        let input_stream = mock_input_stream(uncompressed, input_chunk_size);

        let output_stream = decode(input_stream).map(Result::unwrap);
        pin_mut!(output_stream);
        let decompressed: Vec<u8> = executor::block_on_stream(output_stream).flatten().collect();

        assert_eq!(uncompressed, &decompressed);
    }

    #[test]
    fn test_small_examples() {
        let tests: Vec<&[u8]> = vec![b"Hello world!", b"abc", b"A", b""];

        for uncompressed in tests {
            assert_round_trip(uncompressed, 3);
        }
    }

    /// Deterministically generate a "random-looking" input, which won't
    /// compress much when gzipped.
    ///
    /// NOTE: every time this function is called, you'll get the same bytes
    /// (or a prefix thereof).
    pub(crate) fn random_looking_bytes(num_bytes: usize) -> Vec<u8> {
        let mut fixed_seed_rng = XorShiftRng::seed_from_u64(0);

        let mut buf = vec![0; num_bytes];
        buf.try_fill(&mut fixed_seed_rng).unwrap();
        buf
    }

    #[test]
    fn test_random_input() {
        assert_round_trip(&random_looking_bytes(100), 40);
    }

    /// Test that an error in the input stream is propagated to the output stream.
    #[test]
    fn test_input_stream_error() {
        let input_stream = mock_input_stream(&random_looking_bytes(100), 20);

        // Simulate an error in the input stream.
        let error = MockError::BadThing("oh no!".into());
        let suffix = stream::once(future::ready(Err(error)));
        let failing_stream = input_stream.take(3).chain(suffix);

        let output_stream = decode(failing_stream);
        pin_mut!(output_stream);
        let result: Result<Vec<Bytes>, _> = executor::block_on_stream(output_stream).collect();

        assert!(matches!(result, Err(Error::Input(MockError::BadThing(..)))));
    }

    /// Test that a corrupt gzip payload results in a DEFLATE error.
    #[test]
    fn test_corrupt_gzip_payload() {
        let gzip_blob = gzip_compress(b"");

        // Wrap random garbage in a gzip header and footer.
        let header = &gzip_blob[..10];
        let garbage = &random_looking_bytes(100);
        let footer = &gzip_blob[gzip_blob.len() - 8..];

        let slices = vec![header, garbage, footer].into_iter();
        let corrupted: Vec<u8> = slices.flatten().copied().collect();

        let chunks = split_into_chunks(&corrupted, 20);
        let input_stream = stream::iter(chunks).map(Ok::<_, MockError>);

        let output_stream = decode(input_stream);
        pin_mut!(output_stream);
        let result: Result<Vec<Bytes>, _> = executor::block_on_stream(output_stream).collect();

        assert!(matches!(result, Err(Error::Decode(DecodeError::Deflate(..)))));
    }
}
