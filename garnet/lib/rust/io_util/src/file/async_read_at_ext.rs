// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::file::AsyncReadAt,
    futures::{future::Future, ready},
    std::{
        convert::{TryFrom as _, TryInto as _},
        io,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// An extension trait which adds utility methods to AsyncReadAt.
pub trait AsyncReadAtExt: AsyncReadAt {
    /// Returns a future that reads at `offset`, and fill `buf`, on success the number of bytes
    /// read is returned.
    fn read_at<'a>(&'a mut self, offset: u64, buf: &'a mut [u8]) -> ReadAt<'a, Self>
    where
        Self: Unpin,
    {
        ReadAt { reader: self, offset, buf }
    }

    /// Returns a future that reads at `offset`, and fill `buf` exactly.
    fn read_at_exact<'a>(&'a mut self, offset: u64, buf: &'a mut [u8]) -> ReadAtExact<'a, Self>
    where
        Self: Unpin,
    {
        ReadAtExact { reader: self, offset, buf }
    }

    /// Returns a future that appends all data to `buf`, on success the number of bytes
    /// read is returned.
    fn read_to_end<'a>(&'a mut self, buf: &'a mut Vec<u8>) -> ReadToEnd<'a, Self>
    where
        Self: Unpin,
    {
        ReadToEnd { reader: self, buf }
    }
}

impl<T: AsyncReadAt + ?Sized> AsyncReadAtExt for T {}

/// Future for the [`read_at`](AsyncReadAtExt::read_at) method.
#[derive(Debug)]
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct ReadAt<'a, R: ?Sized> {
    reader: &'a mut R,
    offset: u64,
    buf: &'a mut [u8],
}

impl<R: ?Sized + Unpin> Unpin for ReadAt<'_, R> {}

impl<R: AsyncReadAt + ?Sized + Unpin> Future for ReadAt<'_, R> {
    type Output = io::Result<usize>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        Pin::new(&mut *this.reader).poll_read_at(cx, this.offset, this.buf)
    }
}

/// Future for the [`read_at_exact`](AsyncReadAtExt::read_at_exact) method.
#[derive(Debug)]
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct ReadAtExact<'a, R: ?Sized> {
    reader: &'a mut R,
    offset: u64,
    buf: &'a mut [u8],
}

impl<R: ?Sized + Unpin> Unpin for ReadAtExact<'_, R> {}

impl<R: AsyncReadAt + ?Sized + Unpin> Future for ReadAtExact<'_, R> {
    type Output = io::Result<()>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        loop {
            let n = ready!(Pin::new(&mut *this.reader).poll_read_at(cx, this.offset, this.buf))?;
            if n == this.buf.len() {
                return Poll::Ready(Ok(()));
            }
            if n == 0 {
                return Poll::Ready(Err(io::ErrorKind::UnexpectedEof.into()));
            }
            match u64::try_from(n) {
                Ok(n) => this.offset += n,
                Err(e) => return Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, e))),
            };
            this.buf = &mut std::mem::replace(&mut this.buf, &mut [])[n..];
        }
    }
}

/// Future for the [`read_to_end`](AsyncReadAtExt::read_to_end) method.
#[derive(Debug)]
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct ReadToEnd<'a, R: ?Sized> {
    reader: &'a mut R,
    buf: &'a mut Vec<u8>,
}

impl<R: ?Sized + Unpin> Unpin for ReadToEnd<'_, R> {}

impl<R: AsyncReadAt + ?Sized + Unpin> Future for ReadToEnd<'_, R> {
    type Output = io::Result<usize>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        let mut g = Guard { len: this.buf.len(), buf: this.buf };
        loop {
            if g.len == g.buf.len() {
                g.buf.reserve(32);
                let capacity = g.buf.capacity();
                g.buf.resize(capacity, 0);
            }

            let offset = match g.len.try_into() {
                Ok(len) => len,
                Err(e) => return Poll::Ready(Err(io::Error::new(io::ErrorKind::Other, e))),
            };
            let buf = &mut g.buf[g.len..];
            match ready!(Pin::new(&mut *this.reader).poll_read_at(cx, offset, buf)) {
                Ok(0) => {
                    return Poll::Ready(Ok(g.len));
                }
                Ok(n) => {
                    g.len += n;
                }
                Err(e) => {
                    return Poll::Ready(Err(e));
                }
            }
        }
    }
}

struct Guard<'a> {
    buf: &'a mut Vec<u8>,
    len: usize,
}

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.buf.truncate(self.len);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::file::{self, AsyncFile},
        fidl::endpoints,
        fidl_fuchsia_io::{FileMarker, FileRequest},
        fuchsia_async as fasync, fuchsia_zircon_status as zx_status,
        futures::{
            future::{self},
            StreamExt as _,
        },
        tempfile::TempDir,
    };

    async fn read_to_end_file_with_expected_contents(expected_contents: &[u8]) {
        let dir = TempDir::new().unwrap();
        let path =
            dir.path().join("read_to_end_with_expected_contents").to_str().unwrap().to_owned();
        let () = file::write_in_namespace(&path, expected_contents).await.unwrap();
        let file = file::open_in_namespace(&path, fidl_fuchsia_io::OPEN_RIGHT_READABLE).unwrap();

        let mut reader = AsyncFile::from_proxy(file);
        let mut actual_contents = vec![];
        reader.read_to_end(&mut actual_contents).await.unwrap();

        assert_eq!(actual_contents, expected_contents);
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_to_end_empty() {
        read_to_end_file_with_expected_contents(&[]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_to_end_large() {
        let expected_contents = vec![7u8; (fidl_fuchsia_io::MAX_BUF * 3).try_into().unwrap()];
        read_to_end_file_with_expected_contents(&expected_contents[..]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_at_different_offsets() {
        let file_contents = (0..1000).map(|i| (i % 256) as u8).collect::<Vec<_>>();
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("read_at_different_offsets").to_str().unwrap().to_owned();
        let () = file::write_in_namespace(&path, &file_contents).await.unwrap();
        let file = file::open_in_namespace(&path, fidl_fuchsia_io::OPEN_RIGHT_READABLE).unwrap();

        let mut reader = AsyncFile::from_proxy(file);
        for &(offset, length) in &[(0, 100), (100, 200), (50, 10), (500, 300)] {
            let mut buffer = vec![0; length];
            reader.read_at(offset as u64, &mut buffer).await.unwrap();

            assert_eq!(buffer, &file_contents[offset..offset + length]);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_at_exact() {
        let (proxy, mut stream) = endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        let contents = (0..50).collect::<Vec<_>>();

        let read_at_exact = async {
            let mut buffer = vec![0; 50];
            reader.read_at_exact(20, &mut buffer[..]).await.unwrap();
            assert_eq!(buffer, contents);
        };
        let handle_requests = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::ReadAtDeprecated { count, offset, responder } => {
                    assert_eq!(count, 50);
                    assert_eq!(offset, 20);
                    responder.send(zx_status::Status::OK.into_raw(), &contents[..20]).unwrap();
                }
                FileRequest::ReadAt { count, offset, responder } => {
                    assert_eq!(count, 50);
                    assert_eq!(offset, 20);
                    responder.send(&mut Ok(contents[..20].to_vec())).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
            match stream.next().await.unwrap().unwrap() {
                FileRequest::ReadAtDeprecated { count, offset, responder } => {
                    assert_eq!(count, 30);
                    assert_eq!(offset, 40);
                    responder.send(zx_status::Status::OK.into_raw(), &contents[20..]).unwrap();
                }
                FileRequest::ReadAt { count, offset, responder } => {
                    assert_eq!(count, 30);
                    assert_eq!(offset, 40);
                    responder.send(&mut Ok(contents[20..].to_vec())).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };
        future::join(read_at_exact, handle_requests).await;
    }
}
