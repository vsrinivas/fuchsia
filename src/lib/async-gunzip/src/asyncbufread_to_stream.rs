// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The actual gzip decompression logic.
//!
//! By working with an `AsyncBufRead` input source, we avoid re-implementing standard
//! functionality like `read_exact`.

use crate::asyncbufread_to_stream::flags::Flags;
use crate::{DecodeError, Error};
use async_generator;
use async_generator::Yield;
use bytes::Bytes;
use futures::pin_mut;
use futures::prelude::*;
use futures::stream::FusedStream;
use miniz_oxide::inflate::stream as mz_stream;
use miniz_oxide::inflate::stream::InflateState;
use miniz_oxide::{DataFormat, MZFlush, MZStatus};
use pin_project::pin_project;
use std::pin::Pin;
use std::{io, result};

mod flags;

/// Local alias, just for convenience.
type Result<T> = result::Result<T, Error<io::Error>>;

/// Helper function for `crate::decode`.
pub(crate) fn decode(
    input: impl AsyncBufRead,
    output_chunk_size: usize,
) -> impl FusedStream<Item = Result<Bytes>> {
    async_generator::generate(|co| Decoder::new(input, output_chunk_size).decode(co))
        .into_try_stream()
}

#[pin_project]
struct Decoder<R> {
    /// Remaining uncompressed input.
    #[pin]
    input: R,

    /// Size of decompressed chunks (except possibly the last chunk,
    /// which may be smaller).
    output_chunk_size: usize,
}

impl<R: AsyncBufRead> Decoder<R> {
    fn new(input: R, output_chunk_size: usize) -> Self {
        assert_ne!(output_chunk_size, 0);

        Self { input, output_chunk_size }
    }

    /// Decode the input stream, yielding output chunks to `out`.
    async fn decode(self, out: Yield<Bytes>) -> Result<()> {
        let this = self;
        pin_mut!(this);

        let flags = this.as_mut().read_required_headers().await?;
        this.as_mut().discard_optional_headers(flags).await?;

        this.as_mut().decode_deflate_body(out).await?;

        this.as_mut().read_and_validate_footer().await?;

        if this.project().input.fill_buf().await?.is_empty() {
            Ok(())
        } else {
            // We could add support for gzip multi-streams at some point,
            // but they're almost never used. People prefer to simply `tar`
            // and then `gzip` if they're compressing multiple files.
            let msg = "multiple gzip members present but not supported";
            Err(DecodeError::Footer(msg.into()).into())
        }
    }

    async fn read_required_headers(self: Pin<&mut Self>) -> Result<Flags> {
        let mut this = self.project();

        let mut header = [0; 10];
        this.input.read_exact(&mut header).await?;

        let magic_number = [0x1f, 0x8b];
        let first_2 = &header[..2];
        if first_2 != &magic_number {
            let msg = format!("unrecognized gzip magic; got {first_2:?}");
            return Err(DecodeError::Header(msg).into());
        }

        let method = header[2];
        if method != 8 {
            let reserved = if method < 8 { "reserved value " } else { "" };
            let msg =
                format!("unsupported compression method. expected 8 found {reserved}{method}");
            return Err(DecodeError::Header(msg).into());
        }

        let flags = Flags::new(header[3])?;

        // These aren't very useful (and in particular, the gzip RFC permits us to ignore them).
        let _mtime = &header[4..8]; // The modification time of the original uncompressed file.
        let _xflags = header[8]; // May be used to indicate the level of compression performed.
        let _os = header[9]; // The operating system / file system on which the compression took place.

        Ok(flags)
    }

    async fn discard_optional_headers(mut self: Pin<&mut Self>, flags: Flags) -> Result<()> {
        if flags.contains(Flags::EXTRA) {
            let mut buf = vec![0; self.as_mut().read_u16_le().await? as usize];
            self.as_mut().project().input.read_exact(&mut buf).await?;
        }

        // For now, we just discard the "original file name" field, if present.
        // In the future, we might want to provide an API for the user to get this value.
        if flags.contains(Flags::NAME) {
            let mut buf = vec![];
            self.as_mut().project().input.read_until(0, &mut buf).await?;
        }

        if flags.contains(Flags::COMMENT) {
            let mut buf = vec![];
            self.as_mut().project().input.read_until(0, &mut buf).await?;
        }

        if flags.contains(Flags::HCRC) {
            let _header_crc = self.read_u16_le().await?;
        }

        // We ignore this flag, as permitted by the RFC.
        // We're producing a stream of bytes anyways, so it doesn't matter if
        // it's hinted that the contents is probably text.
        let _is_text = flags.contains(Flags::TEXT);

        Ok(())
    }

    // TODO(https://fxbug.dev/96236): The gzip spec permits ignoring the CRC,
    // but we may like to implement it as an optional check in a future CL.
    // (Same goes for the optional header CRC.)
    async fn read_and_validate_footer(mut self: Pin<&mut Self>) -> Result<()> {
        let _crc = self.as_mut().read_u32_le().await?;
        let _uncompressed_size_mod_32 = self.read_u32_le().await?;
        Ok(())
    }

    /// Yield output chunks to `out`.
    //
    // TODO(https://fxbug.dev/96237): This implementation blocks on output
    // until it has consumed enough input to produce a full output buffer. Some API
    // consumers may like to have the output buffer flushed whenever reading from input
    // would block.
    async fn decode_deflate_body(self: Pin<&mut Self>, mut out: Yield<Bytes>) -> Result<()> {
        let mut this = self.project();

        let mut mz_state = InflateState::new_boxed(DataFormat::Raw);

        let mut output_buf = vec![0; *this.output_chunk_size];
        let mut output_len = 0; // How much of the output buffer is currently filled.

        loop {
            // Ensure more input is available. Note the deflate body is followed by a gzip
            // footer, so the stream should never dry up at this stage.
            let input_buf = this.input.fill_buf().await?;

            let info = mz_stream::inflate(
                &mut mz_state,
                &input_buf,
                &mut output_buf[output_len..],
                MZFlush::None,
            );

            let status = info.status.map_err(DecodeError::from)?;
            this.input.consume_unpin(info.bytes_consumed);
            output_len += info.bytes_written;

            // If we have a full output chunk, yield it.
            if output_len == output_buf.len() {
                let output_chunk = Bytes::copy_from_slice(&output_buf);
                out.yield_(output_chunk).await;
                output_len = 0;
            } else if output_len > output_buf.len() {
                panic!("logic error: over-full buffer");
            }

            match status {
                MZStatus::Ok => (),
                MZStatus::StreamEnd => {
                    // Return a partial chunk with the rest of the output data.
                    if output_len != 0 {
                        let output_chunk = Bytes::copy_from_slice(&output_buf[..output_len]);
                        out.yield_(output_chunk).await;
                    }

                    return Ok(());
                }
                // gzip doesn't support preset dictionaries, so this status will never be returned.
                MZStatus::NeedDict => unreachable!("miniz_oxide never returns NeedDict"),
            }
        }
    }

    async fn read_u16_le(self: Pin<&mut Self>) -> io::Result<u16> {
        let mut this = self.project();

        let mut buf = [0; 2];
        this.input.read_exact(&mut buf).await?;
        Ok(u16::from_le_bytes(buf))
    }

    async fn read_u32_le(self: Pin<&mut Self>) -> io::Result<u32> {
        let mut this = self.project();

        let mut buf = [0; 4];
        this.input.read_exact(&mut buf).await?;
        Ok(u32::from_le_bytes(buf))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tests::{gzip_compress, random_looking_bytes, split_into_chunks};
    use futures::channel::mpsc as futures_mpsc;
    use futures::channel::mpsc::TryRecvError;
    use futures::executor;
    use futures::executor::LocalPool;
    use futures::task::SpawnExt;

    /// Test chunking behavior of the decoder:
    /// * Push input chunks one-at-a-time by hand.
    /// * Verify that output chunks are yielded when expected,
    ///   and have the expected size.
    #[test]
    fn test_chunking() {
        const UNCOMPRESSED_LEN: usize = 100;
        const INPUT_CHUNK_SIZE: usize = 10;
        const OUTPUT_CHUNK_SIZE: usize = 32;

        let uncompressed = random_looking_bytes(UNCOMPRESSED_LEN);
        let compressed = gzip_compress(&uncompressed);
        let input_chunks = split_into_chunks(&compressed, INPUT_CHUNK_SIZE);

        // Run the decoder in a separate task, passing input/output via channels.
        let mut decoder_pool = LocalPool::new();
        let (in_tx, in_rx) = futures_mpsc::unbounded();
        let (mut out_tx, mut out_rx) = futures_mpsc::unbounded();
        decoder_pool
            .spawner()
            .spawn(async move {
                let out_stream = decode(in_rx.into_async_read(), OUTPUT_CHUNK_SIZE);
                pin_mut!(out_stream);
                while let Some(out_chunk) = out_stream.try_next().await.unwrap() {
                    out_tx.send(out_chunk).await.unwrap();
                }
            })
            .unwrap();

        let mut output_chunks = vec![];
        let mut output_events = vec![];

        // Push chunks one-at-a-time to the input stream.
        for in_chunk in input_chunks {
            in_tx.unbounded_send(Ok(in_chunk)).unwrap();

            // Run the decoder until it would block.
            decoder_pool.run_until_stalled();

            // Did it produce an output chunk?
            let out_chunk = match out_rx.try_next() {
                Ok(None) => panic!("no more output"),
                Ok(Some(chunk)) => Some(chunk),
                Err(TryRecvError { .. }) => None,
            };

            let len = out_chunk.as_ref().map(|chunk| chunk.len());
            output_events.push(len);

            if let Some(chunk) = out_chunk {
                output_chunks.push(chunk);
            }
        }

        // Close the input stream.
        drop(in_tx);

        // The output stream should close and `out_tx` should be dropped.
        decoder_pool.run_until_stalled();
        assert!(matches!(out_rx.try_next(), Ok(None)));

        // Hard-coded consistency check: chunking behavior seems reasonable.
        let expected = [
            None,
            None,
            None,
            None,
            Some(32),
            None,
            None,
            Some(32),
            None,
            None,
            None,
            Some(32),
            Some(4),
        ];
        assert_eq!(output_events, expected);

        // Are the output bytes correct?
        let decompressed: Vec<u8> = output_chunks.into_iter().flatten().collect();
        assert_eq!(uncompressed, decompressed);
    }

    /// Test that multiple gzip members result in an error, since
    /// support for them is not yet implemented (and may never be).
    #[test]
    fn test_multiple_members_error() {
        const UNCOMPRESSED_LEN: usize = 100;
        const CHUNK_SIZE: usize = 40;

        let uncompressed = random_looking_bytes(UNCOMPRESSED_LEN);
        let compressed = gzip_compress(&uncompressed);
        let duplicated = compressed.repeat(2); // Two identical gzip members.
        let chunks = split_into_chunks(&duplicated, CHUNK_SIZE);
        let input_stream = stream::iter(chunks).map(Ok);

        let output_stream = decode(input_stream.into_async_read(), CHUNK_SIZE);
        pin_mut!(output_stream);
        let result: Result<Vec<Bytes>> = executor::block_on_stream(output_stream).collect();

        assert!(matches!(result, Err(Error::Decode(DecodeError::Footer(..)))));
    }
}
