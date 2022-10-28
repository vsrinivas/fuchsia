// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bytes::{Bytes, BytesMut},
    futures::{stream, AsyncRead, Stream, TryStreamExt as _},
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
    expected_len: u64,
    mut file: impl AsyncRead + Unpin,
) -> impl Stream<Item = io::Result<Bytes>> {
    let mut buf = BytesMut::new();
    let mut remaining_len = expected_len;

    stream::poll_fn(move |cx| {
        if remaining_len == 0 {
            return Poll::Ready(None);
        }

        buf.resize(min(CHUNK_SIZE, remaining_len as usize), 0);

        // Read a chunk from the file.
        let n = match futures::ready!(Pin::new(&mut file).poll_read(cx, &mut buf)) {
            Ok(n) => n as u64,
            Err(err) => {
                return Poll::Ready(Some(Err(err)));
            }
        };

        // If we read zero bytes, then the file changed size while we were streaming it.
        if n == 0 {
            let msg = format!(
                "file truncated: only read {} out of {} bytes",
                expected_len - remaining_len,
                expected_len,
            );
            // Clear out the remaining_len so we'll return None next time we're polled.
            remaining_len = 0;
            return Poll::Ready(Some(Err(io::Error::new(io::ErrorKind::Other, msg))));
        }

        // Return the chunk read from the file. The file may have changed size during streaming, so
        // it's possible we could have read more than expected. If so, truncate the result to the
        // limited size.
        let mut chunk = buf.split_to(n as usize).freeze();
        if n > remaining_len {
            chunk = chunk.split_to(remaining_len as usize);
            remaining_len = 0;
        } else {
            remaining_len -= n;
        }

        Poll::Ready(Some(Ok(chunk)))
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        proptest::prelude::*,
        std::{
            io,
            task::{Context, Poll},
        },
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_file_stream() {
        for size in [0, CHUNK_SIZE - 1, CHUNK_SIZE, CHUNK_SIZE + 1, CHUNK_SIZE * 2 + 1] {
            let expected = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();
            let stream = file_stream(size as u64, &*expected);

            let mut actual = vec![];
            read_stream_to_end(stream, &mut actual).await.unwrap();
            assert_eq!(actual, expected);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_file_stream_chunks() {
        let size = CHUNK_SIZE * 3 + 10;

        let expected = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();
        let mut stream = file_stream(size as u64, &*expected);

        let mut expected_chunks = expected.chunks(CHUNK_SIZE).map(Bytes::copy_from_slice);

        assert_eq!(stream.try_next().await.unwrap(), expected_chunks.next());
        assert_eq!(stream.try_next().await.unwrap(), expected_chunks.next());
        assert_eq!(stream.try_next().await.unwrap(), expected_chunks.next());
        assert_eq!(stream.try_next().await.unwrap(), expected_chunks.next());
        assert_eq!(stream.try_next().await.unwrap(), None);
        assert_eq!(expected_chunks.next(), None);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_file_stream_file_truncated() {
        let len = CHUNK_SIZE * 2;
        let long_len = CHUNK_SIZE * 3;

        let truncated_buf = vec![0; len];
        let stream = file_stream(long_len as u64, truncated_buf.as_slice());

        let mut actual = vec![];
        assert_eq!(
            read_stream_to_end(stream, &mut actual).await.unwrap_err().to_string(),
            format!("file truncated: only read {} out of {} bytes", len, long_len)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_file_stream_file_extended() {
        let len = CHUNK_SIZE * 3;
        let short_len = CHUNK_SIZE * 2;

        let buf = (0..std::u8::MAX).cycle().take(len).collect::<Vec<_>>();
        let stream = file_stream(short_len as u64, buf.as_slice());

        let mut actual = vec![];
        read_stream_to_end(stream, &mut actual).await.unwrap();
        assert_eq!(actual, &buf[..short_len]);
    }

    struct TestReader<'a> {
        bytes: &'a [u8],
    }

    impl<'a> AsyncRead for TestReader<'a> {
        fn poll_read(
            mut self: Pin<&mut Self>,
            _cx: &mut Context<'_>,
            buf: &mut [u8],
        ) -> Poll<Result<usize, io::Error>> {
            let mut this = self.as_mut();
            let mut n = min(buf.len(), this.bytes.len());
            if n > 10 {
                n -= 5;
            }

            let (lhs, rhs) = this.bytes.split_at(n);
            this.bytes = rhs;

            buf[..n].copy_from_slice(&lhs[..n]);

            Poll::Ready(Ok(n))
        }
    }

    proptest! {
        #[test]
        fn test_file_stream_proptest(len in 0usize..CHUNK_SIZE * 100) {
            let mut executor = fuchsia_async::TestExecutor::new().unwrap();
            let () = executor.run_singlethreaded(async move {
                let expected = (0..std::u8::MAX).cycle().take(len).collect::<Vec<_>>();
                let reader = TestReader {
                    bytes: expected.as_slice(),
                };
                let stream = file_stream(expected.len() as u64, reader);

                let mut actual = vec![];
                read_stream_to_end(stream, &mut actual).await.unwrap();

                assert_eq!(expected, actual);
            });
        }
    }
}
