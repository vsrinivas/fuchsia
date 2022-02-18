// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::client::QueryResponseFut,
    fidl_fuchsia_io::{FileProxy, NodeAttributes},
    fuchsia_zircon_status as zx_status,
    futures::{
        future::Future,
        io::{AsyncRead, AsyncSeek, SeekFrom},
    },
    std::{
        cmp::min,
        convert::TryInto as _,
        io,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// Trait for reading at a given offset asynchronously.
/// This is basically `futures::io::AsyncRead` with an extra offset.
pub trait AsyncReadAt {
    /// Attempt to read at most `buf.len()` bytes starting at `offset` into `buf`. On success
    /// returns the number of bytes read.
    /// Contents of `buf` are only altered on success.
    /// Reads of more than zero but fewer than `buf.len()` bytes do NOT indicate EOF.
    /// Reads of zero bytes only occur if `buf.len() == 0` or EOF.
    fn poll_read_at(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        offset: u64,
        buf: &mut [u8],
    ) -> Poll<io::Result<usize>>;
}

/// Trait for getting the size of the file asynchronously.
pub trait AsyncGetSize {
    /// Attempt to get the size of the file, on success returns the file size.
    fn poll_get_size(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<io::Result<u64>>;
}

/// An extension trait which adds utility methods to AsyncGetSize.
pub trait AsyncGetSizeExt: AsyncGetSize {
    /// Returns a future that will return the file size on success.
    fn get_size<'a>(&'a mut self) -> GetSize<'a, Self>
    where
        Self: Unpin,
    {
        GetSize { size_getter: self }
    }
}

impl<T: AsyncGetSize + ?Sized> AsyncGetSizeExt for T {}

/// Future for the [`get_size`](AsyncGetSizeExt::get_size) method.
#[derive(Debug)]
#[must_use = "futures do nothing unless you `.await` or poll them"]
pub struct GetSize<'a, R: ?Sized> {
    size_getter: &'a mut R,
}

impl<R: ?Sized + Unpin> Unpin for GetSize<'_, R> {}

impl<R: AsyncGetSize + ?Sized + Unpin> Future for GetSize<'_, R> {
    type Output = io::Result<u64>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = &mut *self;
        Pin::new(&mut *this.size_getter).poll_get_size(cx)
    }
}

/// Wraps a `fidl_fuchsia_io::FileProxy` and implements `AsyncReadAt` and `AsyncGetSize`, which
/// allows one to perform asynchronous file reads that don't block the current thread while waiting
/// for data.
/// Unlike `AsyncReader`, this struct does not require exclusive ownership, because `read_at` does
/// not rely on the file offset state in the connection. This is useful if one wants to efficiently
/// read different parts of the file at the same time.
#[derive(Debug)]
pub struct AsyncFile {
    file: FileProxy,
    read_at_state: ReadAtState,
    get_attr_fut: Option<QueryResponseFut<(i32, NodeAttributes)>>,
}

#[derive(Debug)]
enum ReadAtState {
    Empty,
    Forwarding {
        fut: QueryResponseFut<Result<Vec<u8>, i32>>,
        file_offset: u64,
        zero_byte_request: bool,
    },
    Bytes {
        bytes: Vec<u8>,
        file_offset: u64,
    },
}

impl AsyncFile {
    pub fn from_proxy(file: FileProxy) -> Self {
        Self { file, read_at_state: ReadAtState::Empty, get_attr_fut: None }
    }
}

impl AsyncReadAt for AsyncFile {
    fn poll_read_at(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        offset: u64,
        buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        loop {
            match self.read_at_state {
                ReadAtState::Empty => {
                    let len = if let Ok(len) = buf.len().try_into() {
                        min(len, fidl_fuchsia_io::MAX_BUF)
                    } else {
                        fidl_fuchsia_io::MAX_BUF
                    };
                    self.read_at_state = ReadAtState::Forwarding {
                        fut: self.file.read_at(len, offset),
                        file_offset: offset,
                        zero_byte_request: len == 0,
                    };
                }
                ReadAtState::Forwarding { ref mut fut, file_offset, zero_byte_request } => {
                    match futures::ready!(Pin::new(fut).poll(cx)) {
                        Ok(result) => {
                            match result {
                                Err(s) => {
                                    self.read_at_state = ReadAtState::Empty;
                                    return Poll::Ready(Err(
                                        zx_status::Status::from_raw(s).into_io_error()
                                    ));
                                }
                                Ok(bytes) => {
                                    // If the File.ReadAt request was for zero bytes, but the current
                                    // poll_read_at is not (because the File.ReadAt request was made by an
                                    // earlier call to poll_read_at with a zero length buffer) then we
                                    // should not advance to ReadAtState::Bytes because that would return
                                    // Ready(Ok(0)), which would indicate EOF to the client.
                                    // This handling is done here instead of short-circuiting at the
                                    // beginning of the function so that zero-length poll_read_ats still
                                    // trigger the validation performed by File.ReadAt.
                                    if zero_byte_request && buf.len() != 0 {
                                        self.read_at_state = ReadAtState::Empty;
                                    } else {
                                        self.read_at_state =
                                            ReadAtState::Bytes { bytes, file_offset };
                                    }
                                }
                            }
                        }
                        Err(e) => {
                            self.read_at_state = ReadAtState::Empty;
                            return Poll::Ready(Err(std::io::Error::new(
                                std::io::ErrorKind::Other,
                                e,
                            )));
                        }
                    }
                }
                ReadAtState::Bytes { ref bytes, file_offset } => {
                    if offset < file_offset {
                        self.read_at_state = ReadAtState::Empty;
                        continue;
                    }
                    let bytes_offset = match (offset - file_offset).try_into() {
                        Ok(offset) => offset,
                        Err(_) => {
                            self.read_at_state = ReadAtState::Empty;
                            continue;
                        }
                    };
                    if bytes_offset != 0 && bytes_offset >= bytes.len() {
                        self.read_at_state = ReadAtState::Empty;
                        continue;
                    }
                    let n = min(buf.len(), bytes.len() - bytes_offset);
                    let () = buf[..n].copy_from_slice(&bytes[bytes_offset..bytes_offset + n]);
                    return Poll::Ready(Ok(n));
                }
            }
        }
    }
}

impl AsyncGetSize for AsyncFile {
    fn poll_get_size(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<std::io::Result<u64>> {
        if self.get_attr_fut.is_none() {
            self.get_attr_fut = Some(self.file.get_attr());
        }
        let fut = self.get_attr_fut.as_mut().unwrap();
        let result = futures::ready!(Pin::new(fut).poll(cx));
        self.get_attr_fut = None;
        match result {
            Ok((status, attr)) => {
                if let Err(e) = zx_status::Status::ok(status) {
                    return Poll::Ready(Err(e.into_io_error()));
                }

                return Poll::Ready(Ok(attr.content_size));
            }
            Err(e) => {
                return Poll::Ready(Err(std::io::Error::new(std::io::ErrorKind::Other, e)));
            }
        }
    }
}

/// Adapter to implement AsyncReadAt + AsyncGetSize for AsyncRead + AsyncSeek.
#[derive(Debug)]
pub struct Adapter<T> {
    inner: T,
}

impl<T> Adapter<T> {
    pub fn new(inner: T) -> Adapter<T> {
        Self { inner }
    }
}

impl<T: AsyncRead + AsyncSeek + Unpin> AsyncReadAt for Adapter<T> {
    fn poll_read_at(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        offset: u64,
        buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        futures::ready!(Pin::new(&mut self.inner).poll_seek(cx, SeekFrom::Start(offset)))?;
        Pin::new(&mut self.inner).poll_read(cx, buf)
    }
}

impl<T: AsyncSeek + Unpin> AsyncGetSize for Adapter<T> {
    fn poll_get_size(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<std::io::Result<u64>> {
        Pin::new(&mut self.inner).poll_seek(cx, SeekFrom::End(0))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::file::{self, AsyncReadAtExt},
        assert_matches::assert_matches,
        fidl::endpoints,
        fidl_fuchsia_io::{FileMarker, FileRequest},
        fuchsia_async as fasync,
        futures::{
            future::{self, poll_fn},
            StreamExt as _, TryStreamExt as _,
        },
        std::io::Write,
        tempfile::{NamedTempFile, TempDir},
    };

    async fn poll_read_at_with_specific_buf_size(
        poll_read_size: u64,
        expected_file_read_size: u64,
    ) {
        let (proxy, mut stream) = endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        let () = poll_fn(|cx| {
            let mut buf = vec![0u8; poll_read_size.try_into().unwrap()];
            assert_matches!(
                Pin::new(&mut reader).poll_read_at(cx, 0, buf.as_mut_slice()),
                Poll::Pending
            );
            Poll::Ready(())
        })
        .await;

        match stream.next().await.unwrap().unwrap() {
            FileRequest::ReadAt { count, .. } => {
                assert_eq!(count, expected_file_read_size);
            }
            req => panic!("unhandled request {:?}", req),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_at_empty_buf() {
        poll_read_at_with_specific_buf_size(0, 0).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_at_caps_buf_size() {
        poll_read_at_with_specific_buf_size(fidl_fuchsia_io::MAX_BUF * 2, fidl_fuchsia_io::MAX_BUF)
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_at_pending_saves_future() {
        let (proxy, mut stream) = endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        // This poll_read_at call will create a File.ReadAt future and poll it. The poll of the
        // File.ReadAt future will return Pending because nothing is handling the FileRequestStream
        // yet. The reader should save this File.ReadAt future for handling subsequent poll_read_at
        // calls.
        let () = poll_fn(|cx| {
            assert_matches!(
                Pin::new(&mut reader).poll_read_at(cx, 2, &mut [0u8; 1]),
                Poll::Pending
            );
            Poll::Ready(())
        })
        .await;

        // Call poll_read_at until we get a byte out. This byte should be from the first and only
        // File.ReadAt request.
        let poll_read_at = async move {
            let mut buf = [0u8; 1];
            assert_eq!(reader.read_at(2, &mut buf).await.unwrap(), buf.len());
            assert_eq!(&buf, &[1]);
        };

        let mut file_read_requests = 0u8;
        let handle_file_stream = async {
            while let Some(req) = stream.try_next().await.unwrap() {
                file_read_requests += 1;
                match req {
                    FileRequest::ReadAt { count, offset, responder } => {
                        assert_eq!(count, 1);
                        assert_eq!(offset, 2);
                        responder.send(&mut Ok(vec![file_read_requests])).unwrap();
                    }
                    req => panic!("unhandled request {:?}", req),
                }
            }
        };

        let ((), ()) = future::join(poll_read_at, handle_file_stream).await;
        assert_eq!(file_read_requests, 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_at_with_smaller_buf_after_pending() {
        let (proxy, mut stream) = endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        // Call poll_read_at with a buf of length 3. This is the first poll_read_at call, so the
        // reader will create a File.ReadAt future for 3 bytes. poll_read_at will return Pending
        // because nothing is handling the FileRequestStream yet.
        let () = poll_fn(|cx| {
            assert_matches!(
                Pin::new(&mut reader).poll_read_at(cx, 0, &mut [0u8; 3]),
                Poll::Pending
            );
            Poll::Ready(())
        })
        .await;

        // Respond to the three byte File.ReadAt request.
        let () = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::ReadAt { count, offset, responder } => {
                    assert_eq!(count, 3);
                    assert_eq!(offset, 0);
                    responder.send(&mut Ok(b"012".to_vec())).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        }
        .await;

        // Call poll_read_at with a buf of length 1. This should resolve the previously created 3
        // byte File.ReadAt future and return the first byte from it while saving the remaining two
        // bytes.
        let mut buf = [0u8; 1];
        assert_eq!(reader.read_at(0, &mut buf).await.unwrap(), buf.len());
        assert_eq!(&buf, b"0");

        // Call poll_read_at with a buf of len 1. This should return the first saved byte, which
        // should be the second byte from the original File.ReadAt request.
        let mut buf = [0u8; 1];
        assert_eq!(reader.read_at(1, &mut buf).await.unwrap(), buf.len());
        assert_eq!(&buf, b"1");

        // Call poll_read_at with a buf of len 2. There should only be one remaining saved byte
        // from the original File.ReadAt request, so poll_read_at should only return one byte.
        let mut buf = [0u8; 2];
        assert_eq!(reader.read_at(2, &mut buf).await.unwrap(), 1);
        assert_eq!(&buf[..1], b"2");

        // There should be no saved bytes remaining, so a poll_read_at of four bytes should cause a
        // new File.ReadAt request.
        let mut buf = [0u8; 4];
        let poll_read_at = reader.read_at(3, &mut buf);

        let handle_second_file_request = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::ReadAt { count, offset, responder } => {
                    assert_eq!(count, 4);
                    assert_eq!(offset, 3);
                    responder.send(&mut Ok(b"3456".to_vec())).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (read_res, ()) = future::join(poll_read_at, handle_second_file_request).await;
        assert_eq!(read_res.unwrap(), 4);
        assert_eq!(&buf, b"3456");
    }

    #[fasync::run_singlethreaded(test)]
    async fn transition_to_empty_on_fidl_error() {
        let (proxy, _) = endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        // poll_read_at will fail because the channel is closed because the server end was dropped.
        let () = poll_fn(|cx| {
            assert_matches!(
                Pin::new(&mut reader).poll_read_at(cx, 0, &mut [0u8; 1]),
                Poll::Ready(Err(_))
            );
            Poll::Ready(())
        })
        .await;

        // This test is accessing internal state because the only fidl error that is easy to inject
        // is ZX_ERR_PEER_CLOSED (by closing the channel). Once the channel is closed, all new
        // futures created by the AsyncFile will fail, but, if poll'ed, the old future would also
        // continue to fail (not panic) because it is Fused.
        assert_matches!(reader.read_at_state, ReadAtState::Empty);
    }

    #[fasync::run_singlethreaded(test)]
    async fn recover_from_file_read_error() {
        let (proxy, mut stream) = endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        // Call poll_read_at until failure.
        let mut buf = [0u8; 1];
        let poll_read_at = reader.read_at(0, &mut buf);

        let failing_file_response = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::ReadAt { count, offset, responder } => {
                    assert_eq!(count, 1);
                    assert_eq!(offset, 0);
                    responder.send(&mut Err(zx_status::Status::NO_MEMORY.into_raw())).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (read_res, ()) = future::join(poll_read_at, failing_file_response).await;
        assert_matches!(read_res, Err(_));

        // Calling poll_read_at again should create a new File.ReadAt request instead of reusing
        // the old future.
        let mut buf = [0u8; 1];
        let poll_read_at = reader.read_at(0, &mut buf);

        let succeeding_file_response = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::ReadAt { count, offset, responder } => {
                    assert_eq!(count, 1);
                    assert_eq!(offset, 0);
                    responder.send(&mut Ok(b"0".to_vec())).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (read_res, ()) = future::join(poll_read_at, succeeding_file_response).await;
        assert_eq!(read_res.unwrap(), 1);
        assert_eq!(&buf, b"0");
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_at_zero_then_read_nonzero() {
        let (proxy, mut stream) = endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        // Call poll_read_at with a zero-length buffer.
        let () = poll_fn(|cx| {
            assert_matches!(Pin::new(&mut reader).poll_read_at(cx, 0, &mut []), Poll::Pending);
            Poll::Ready(())
        })
        .await;

        // Handle the zero-length File.ReadAt request.
        match stream.next().await.unwrap().unwrap() {
            FileRequest::ReadAt { count, offset, responder } => {
                assert_eq!(count, 0);
                assert_eq!(offset, 0);
                responder.send(&mut Ok(vec![])).unwrap();
            }
            req => panic!("unhandled request {:?}", req),
        }

        // Call poll_read_at with a length 1 buffer until Ready is returned;
        let mut buf = vec![0u8; 1];
        let poll_read_at = reader.read_at(0, &mut buf);

        // The AsyncFile will discard the File.ReadAt response from the first poll_read, and create
        // another request, this handles that second request. The AsyncFile discards the first
        // response because the first poll_read_at was for zero bytes, but the current poll_read_at
        // is not.
        let handle_file_request = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::ReadAt { count, offset, responder } => {
                    assert_eq!(count, 1);
                    assert_eq!(offset, 0);
                    responder.send(&mut Ok(vec![1])).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (poll_read, ()) = future::join(poll_read_at, handle_file_request).await;

        // poll_read_at should read 1 byte, even though the first poll_read_at request was for zero
        // bytes and returned Pending.
        assert_eq!(poll_read.unwrap(), 1);
        assert_eq!(&buf[..], &[1]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn different_poll_read_at_and_file_sizes() {
        for first_poll_read_len in 0..5 {
            for file_size in 0..5 {
                for second_poll_offset in 0..file_size {
                    for second_poll_read_len in 0..5 {
                        let (proxy, mut stream) =
                            endpoints::create_proxy_and_stream::<FileMarker>().unwrap();

                        let mut reader = AsyncFile::from_proxy(proxy);

                        // poll_read_at causes the AsyncFile to create a File.ReadAt request.
                        let () = poll_fn(|cx| {
                            let mut buf = vec![0u8; first_poll_read_len];
                            assert_matches!(
                                Pin::new(&mut reader).poll_read_at(cx, 0, &mut buf),
                                Poll::Pending
                            );
                            Poll::Ready(())
                        })
                        .await;

                        // Respond to the File.ReadAt request with at most as many bytes as the
                        // poll_read_at requested.
                        match stream.next().await.unwrap().unwrap() {
                            FileRequest::ReadAt { count, offset, responder } => {
                                assert_eq!(count, first_poll_read_len.try_into().unwrap());
                                assert_eq!(offset, 0);
                                let resp = vec![7u8; min(file_size, first_poll_read_len)];
                                responder.send(&mut Ok(resp)).unwrap();
                            }
                            req => panic!("unhandled request {:?}", req),
                        }

                        // Call poll_read_at until it returns Ready. If the first poll_read_at was
                        // for zero bytes and this poll_read_at is not or this poll_read_at offset
                        // is outside the buffer, the AsyncFile will make another File.ReadAt
                        // request.
                        let mut buf = vec![0u8; second_poll_read_len];
                        let poll_read_at = reader.read_at(second_poll_offset as u64, &mut buf);

                        let second_request = first_poll_read_len == 0 && second_poll_read_len != 0
                            || second_poll_offset != 0 && second_poll_offset >= first_poll_read_len;
                        let handle_conditional_file_request = async {
                            if second_request {
                                match stream.next().await.unwrap().unwrap() {
                                    FileRequest::ReadAt { count, offset, responder } => {
                                        assert_eq!(count, second_poll_read_len.try_into().unwrap());
                                        assert_eq!(offset, second_poll_offset.try_into().unwrap());
                                        let resp = vec![
                                            7u8;
                                            min(
                                                file_size - second_poll_offset,
                                                second_poll_read_len
                                            )
                                        ];
                                        responder.send(&mut Ok(resp)).unwrap();
                                    }
                                    req => panic!("unhandled request {:?}", req),
                                }
                            }
                        };

                        let (read_res, ()) =
                            future::join(poll_read_at, handle_conditional_file_request).await;

                        let expected_len = if second_request {
                            min(file_size - second_poll_offset, second_poll_read_len)
                        } else {
                            min(
                                min(file_size, first_poll_read_len) - second_poll_offset,
                                second_poll_read_len,
                            )
                        };
                        let expected = vec![7u8; expected_len];
                        assert_eq!(read_res.unwrap(), expected_len);
                        assert_eq!(&buf[..expected_len], &expected[..]);
                    }
                }
            }
        }
    }

    async fn get_size_file_with_contents(contents: &[u8]) {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("get_size_file_with_contents").to_str().unwrap().to_owned();
        let () = file::write_in_namespace(&path, contents).await.unwrap();
        let file = file::open_in_namespace(&path, fidl_fuchsia_io::OPEN_RIGHT_READABLE).unwrap();

        let mut reader = AsyncFile::from_proxy(file);

        assert_eq!(reader.get_size().await.unwrap(), contents.len() as u64);
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_size_empty() {
        get_size_file_with_contents(&[]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_size_large() {
        let expected_contents = vec![7u8; (fidl_fuchsia_io::MAX_BUF * 3).try_into().unwrap()];
        get_size_file_with_contents(&expected_contents[..]).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn get_size_changing_size() {
        let (mut file, path) = NamedTempFile::new().unwrap().into_parts();
        let proxy =
            file::open_in_namespace(path.to_str().unwrap(), fidl_fuchsia_io::OPEN_RIGHT_READABLE)
                .unwrap();

        let mut reader = AsyncFile::from_proxy(proxy);

        assert_eq!(reader.get_size().await.unwrap(), 0);
        file.write_all(&[1; 3][..]).unwrap();
        assert_eq!(reader.get_size().await.unwrap(), 3);
        file.write_all(&[2; 5][..]).unwrap();
        assert_eq!(reader.get_size().await.unwrap(), 8);
    }

    #[fasync::run_singlethreaded(test)]
    async fn adapter_for_cursor() {
        let data = (0..1000).map(|i| (i % 256) as u8).collect::<Vec<_>>();
        let cursor = futures::io::Cursor::new(data.clone());
        let mut adapter = Adapter::new(cursor);

        assert_eq!(adapter.get_size().await.unwrap(), 1000);

        let mut buffer = vec![];
        adapter.read_to_end(&mut buffer).await.unwrap();
        assert_eq!(buffer, data);

        let mut buffer = vec![0; 100];
        adapter.read_at_exact(333, &mut buffer).await.unwrap();
        assert_eq!(buffer, &data[333..433]);
    }
}
