// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bytes::{Bytes, BytesMut},
    futures::{stream, AsyncRead as _, Stream, TryStreamExt as _},
    std::{cmp::min, io, pin::Pin, task::Poll},
};

/// Read files in chunks of this size off the local storage.
// Note: this is internally public to allow repository tests to check they work across chunks.
pub(crate) const CHUNK_SIZE: usize = 8_192;

/// Helper to read to the end of a [Bytes] stream.
pub(crate) async fn read_stream_to_end<S>(mut stream: S, buf: &mut Vec<u8>) -> io::Result<()>
where
    S: Stream<Item = io::Result<Bytes>> + Unpin,
{
    while let Some(chunk) = stream.try_next().await? {
        buf.extend_from_slice(&chunk);
    }
    Ok(())
}

/// Read a file up to `len` bytes in batches of [CHUNK_SIZE], and return a stream of [Bytes].
///
/// This will return an error if the file changed size during streaming.
pub(super) fn file_stream(
    mut expected_len: u64,
    mut file: async_fs::File,
) -> impl Stream<Item = io::Result<Bytes>> {
    let mut buf = BytesMut::new();

    stream::poll_fn(move |cx| {
        if expected_len == 0 {
            return Poll::Ready(None);
        }

        buf.resize(min(CHUNK_SIZE, expected_len as usize), 0);

        // Read a chunk from the file.
        let n = match futures::ready!(Pin::new(&mut file).poll_read(cx, &mut buf)) {
            Ok(n) => n as u64,
            Err(err) => {
                return Poll::Ready(Some(Err(err)));
            }
        };

        // If we read zero bytes, then the file changed size while we were streaming it.
        if n == 0 {
            expected_len = 0;
            return Poll::Ready(Some(Err(io::Error::new(io::ErrorKind::Other, "file truncated"))));
        }

        // Return the chunk read from the file. The file may have changed size during streaming, so
        // it's possible we could have read more than expected. If so, truncate the result to the
        // limited size.
        let mut chunk = buf.split_to(n as usize).freeze();
        if n > expected_len {
            chunk = chunk.split_to(expected_len as usize);
            expected_len = 0;
        } else {
            expected_len -= n;
        }

        Poll::Ready(Some(Ok(chunk)))
    })
}
