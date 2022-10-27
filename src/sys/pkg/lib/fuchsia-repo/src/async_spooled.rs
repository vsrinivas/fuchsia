// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::file_stream,
    bytes::Bytes,
    futures::{
        future::ready,
        stream::{once, BoxStream},
        AsyncSeekExt as _, AsyncWriteExt as _,
    },
    std::io,
};

/// Helper to buffer writes in memory up to a certain size, then spill over to a temporary file.
pub(crate) struct AsyncSpooledTempFile {
    buf_size: usize,
    kind: Kind,
}

enum Kind {
    Buffer(Vec<u8>),
    File(async_fs::File),
}

impl AsyncSpooledTempFile {
    /// Create a new [AsyncSpooledTempFile] that will buffer `buf_size` bytes in memory before
    /// spilling over to a temporary file.
    pub(crate) fn new(buf_size: usize) -> Self {
        Self { buf_size, kind: Kind::Buffer(vec![]) }
    }

    /// Write the `buf` bytes into the spooled file.
    pub(crate) async fn write_all(&mut self, buf: &[u8]) -> Result<(), io::Error> {
        match &mut self.kind {
            Kind::Buffer(buffer) => {
                if buffer.len().saturating_add(buf.len()) < self.buf_size {
                    let mut file = async_fs::File::from(tempfile::tempfile()?);
                    file.write_all(buffer).await?;
                    file.write_all(buf).await?;
                    self.kind = Kind::File(file);
                } else {
                    buffer.extend(buf);
                }
            }
            Kind::File(file) => {
                file.write_all(buf).await?;
            }
        }
        Ok(())
    }

    /// Return the spooled file as a pair of the file size, and a stream of bytes.
    pub(crate) async fn into_stream(
        self,
    ) -> io::Result<(u64, BoxStream<'static, io::Result<Bytes>>)> {
        match self.kind {
            Kind::Buffer(buffer) => {
                let len = buffer.len() as u64;
                let stream = Box::pin(once(ready(Ok(Bytes::from(buffer)))));
                Ok((len, stream))
            }
            Kind::File(mut file) => {
                file.flush().await?;
                file.seek(io::SeekFrom::Start(0)).await?;

                let len = file.metadata().await?.len();
                let stream = Box::pin(file_stream(len, file));

                Ok((len, stream))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::util::read_stream_to_end};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_zero_buf() {
        let mut tmp = AsyncSpooledTempFile::new(0);
        let mut body = vec![];

        for _ in 0..10 {
            let chunk = (0..std::u8::MAX).cycle().take(100).collect::<Vec<_>>();
            body.extend_from_slice(&chunk);
            tmp.write_all(&chunk).await.unwrap();
        }

        let (len, stream) = tmp.into_stream().await.unwrap();
        assert_eq!(body.len() as u64, len);

        let mut actual = vec![];
        read_stream_to_end(stream, &mut actual).await.unwrap();
        assert_eq!(body, actual);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_large_buf() {
        let mut tmp = AsyncSpooledTempFile::new(1024);
        let mut body = vec![];

        for _ in 0..10 {
            let chunk = (0..std::u8::MAX).cycle().take(100).collect::<Vec<_>>();
            body.extend_from_slice(&chunk);
            tmp.write_all(&chunk).await.unwrap();
        }

        let (len, stream) = tmp.into_stream().await.unwrap();
        assert_eq!(body.len() as u64, len);

        let mut actual = vec![];
        read_stream_to_end(stream, &mut actual).await.unwrap();
        assert_eq!(body, actual);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_single_writes() {
        let buf_size = 100;
        for size in [0, 20, buf_size - 1, buf_size, buf_size + 1, buf_size * 2 + 1] {
            let mut tmp = AsyncSpooledTempFile::new(buf_size);

            let body = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();
            tmp.write_all(&body).await.unwrap();

            let (len, stream) = tmp.into_stream().await.unwrap();
            assert_eq!(body.len() as u64, len);

            let mut actual = vec![];
            read_stream_to_end(stream, &mut actual).await.unwrap();
            assert_eq!(body, actual);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_chunk_writes() {
        let buf_size = 100;
        let mut tmp = AsyncSpooledTempFile::new(buf_size);
        let mut body = vec![];

        for size in [10, 10, 79, 1, buf_size, buf_size + 1, buf_size * 2 + 1] {
            let chunk = (0..std::u8::MAX).cycle().take(size).collect::<Vec<_>>();
            body.extend_from_slice(&chunk);
            tmp.write_all(&chunk).await.unwrap();
        }

        let (len, stream) = tmp.into_stream().await.unwrap();
        assert_eq!(body.len() as u64, len);

        let mut actual = vec![];
        read_stream_to_end(stream, &mut actual).await.unwrap();
        assert_eq!(body, actual);
    }
}
