// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{client::QueryResponseFut, endpoints::ClientEnd},
    fidl_fuchsia_io::{FileMarker, FileProxy},
    fuchsia_zircon::Status,
    futures::io::AsyncRead,
    std::{
        cmp::min,
        convert::TryInto as _,
        future::Future as _,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// Wraps a `fidl_fuchsia_io::FileProxy` and implements `futures::io::AsyncRead`, which allows one
/// to perform asynchronous file reads that don't block the current thread while waiting for data.
#[derive(Debug)]
pub struct AsyncReader {
    file: FileProxy,
    state: State,
}

#[derive(Debug)]
enum State {
    Empty,
    Forwarding { fut: QueryResponseFut<(i32, Vec<u8>)>, zero_byte_request: bool },
    Bytes { bytes: Vec<u8>, offset: usize },
}

impl AsyncReader {
    /// Errors if the provided `ClientEnd` fails to convert to a `FileProxy`.
    ///
    /// This function takes a `ClientEnd` instead of a `FileProxy` to guarantee that the channel is
    /// exclusively owned. Exclusive ownership avoids surprising behavior arising from the mismatch
    /// between the semantics for `AsyncRead` and `fuchsia.io/File.Read`. On e.g. Linux, if two
    /// `AsyncRead` objects were wrapping the same file descriptor and a call to `poll_read` on one
    /// of the `AsyncRead` objects returned `Pending`, a client would generally not expect the
    /// offset of the underlying file descriptor to advance. Meaning that a client could then call
    /// `poll_read` on the other `AsyncRead` object and expect not to miss any file
    /// contents. However, with an `AsyncRead` implementation that wraps `fuchsia.io/File.Read`, a
    /// `poll_read` call that returns `Pending` would advance the file offset, meaning that
    /// interleaving usage of `AsyncRead` objects that share a channel would return file contents in
    /// surprising order.
    pub fn from_client_end(file: ClientEnd<FileMarker>) -> Result<Self, AsyncReaderError> {
        Ok(Self {
            file: file.into_proxy().map_err(AsyncReaderError::ConvertClientEndToProxy)?,
            state: State::Empty,
        })
    }
}

impl AsyncRead for AsyncReader {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        loop {
            match self.state {
                State::Empty => {
                    let len = if let Ok(len) = buf.len().try_into() {
                        min(len, fidl_fuchsia_io::MAX_BUF)
                    } else {
                        fidl_fuchsia_io::MAX_BUF
                    };
                    self.state =
                        State::Forwarding { fut: self.file.read(len), zero_byte_request: len == 0 };
                }
                State::Forwarding { ref mut fut, ref zero_byte_request } => {
                    match futures::ready!(Pin::new(fut).poll(cx)) {
                        Ok((status, bytes)) => {
                            if let Err(e) = Status::ok(status) {
                                self.state = State::Empty;
                                return Poll::Ready(Err(std::io::Error::new(
                                    std::io::ErrorKind::Other,
                                    e,
                                )));
                            }
                            // If the File.Read request was for zero bytes, but the current
                            // poll_read is not (because the File.Read request was made by an
                            // earlier call to poll_read with a zero length buffer) then we should
                            // not advance to State::Bytes because that would return Ready(Ok(0)),
                            // which would indicate EOF to the client.
                            // This handling is done here instead of short-circuiting at the
                            // beginning of the function so that zero-length poll_reads still
                            // trigger the validation performed by File.Read.
                            if *zero_byte_request && buf.len() != 0 {
                                self.state = State::Empty;
                            } else {
                                self.state = State::Bytes { bytes, offset: 0 };
                            }
                        }
                        Err(e) => {
                            self.state = State::Empty;
                            return Poll::Ready(Err(std::io::Error::new(
                                std::io::ErrorKind::Other,
                                e,
                            )));
                        }
                    }
                }
                State::Bytes { ref bytes, ref mut offset } => {
                    let n = min(buf.len(), bytes.len() - *offset);
                    let next_offset = *offset + n;
                    let () = buf[..n].copy_from_slice(&bytes[*offset..next_offset]);
                    if next_offset == bytes.len() {
                        self.state = State::Empty;
                    } else {
                        *offset = next_offset;
                    }
                    return Poll::Ready(Ok(n));
                }
            }
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum AsyncReaderError {
    #[error("Failed to convert ClientEnd<FileMarker> to a FileProxy")]
    ConvertClientEndToProxy(#[source] fidl::Error),
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::file,
        fidl::endpoints,
        fidl_fuchsia_io::{FileMarker, FileRequest},
        fuchsia_async as fasync,
        futures::{
            future::poll_fn, io::AsyncReadExt as _, join, StreamExt as _, TryStreamExt as _,
        },
        matches::assert_matches,
        tempfile::TempDir,
    };

    fn proxy_to_client_end(proxy: FileProxy) -> ClientEnd<FileMarker> {
        proxy.into_channel().unwrap().into_zx_channel().into()
    }

    #[fasync::run_singlethreaded(test)]
    async fn client_end_to_proxy_conversion_failure() {
        let bad_handle = fuchsia_zircon::Handle::invalid();

        assert_matches!(
            AsyncReader::from_client_end(ClientEnd::new(bad_handle.into())),
            Err(AsyncReaderError::ConvertClientEndToProxy(_))
        );
    }

    async fn read_to_end_file_with_expected_contents(expected_contents: &[u8]) {
        let dir = TempDir::new().unwrap();
        let path =
            dir.path().join("read_to_end_with_expected_contents").to_str().unwrap().to_owned();
        let () = file::write_in_namespace(&path, expected_contents).await.unwrap();
        let file = file::open_in_namespace(&path, fidl_fuchsia_io::OPEN_RIGHT_READABLE).unwrap();

        let mut reader = AsyncReader::from_client_end(proxy_to_client_end(file)).unwrap();
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

    async fn poll_read_with_specific_buf_size(poll_read_size: u64, expected_file_read_size: u64) {
        let (client, mut stream) = endpoints::create_request_stream::<FileMarker>().unwrap();

        let mut reader = AsyncReader::from_client_end(client).unwrap();

        let () = poll_fn(|cx| {
            let mut buf = vec![0u8; poll_read_size.try_into().unwrap()];
            assert_matches!(Pin::new(&mut reader).poll_read(cx, buf.as_mut_slice()), Poll::Pending);
            Poll::Ready(())
        })
        .await;

        match stream.next().await.unwrap().unwrap() {
            FileRequest::Read { count, .. } => {
                assert_eq!(count, expected_file_read_size);
            }
            req => panic!("unhandled request {:?}", req),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_empty_buf() {
        poll_read_with_specific_buf_size(0, 0).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_caps_buf_size() {
        poll_read_with_specific_buf_size(fidl_fuchsia_io::MAX_BUF * 2, fidl_fuchsia_io::MAX_BUF)
            .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_pending_saves_future() {
        let (client, mut stream) = endpoints::create_request_stream::<FileMarker>().unwrap();

        let mut reader = AsyncReader::from_client_end(client).unwrap();

        // This poll_read call will create a File.Read future and poll it. The poll of the File.Read
        // future will return Pending because nothing is handling the FileRequestStream yet. The
        // reader should save this File.Read future for handling subsequent poll_read calls.
        let () = poll_fn(|cx| {
            assert_matches!(Pin::new(&mut reader).poll_read(cx, &mut [0u8; 1]), Poll::Pending);
            Poll::Ready(())
        })
        .await;

        // Call poll_read until we get a byte out. This byte should be from the first and only
        // File.Read request.
        let poll_read = async move {
            let mut buf = [0u8; 1];
            assert_eq!(reader.read(&mut buf).await.unwrap(), buf.len());
            assert_eq!(&buf, &[1]);
        };

        let mut file_read_requests = 0u8;
        let handle_file_stream = async {
            while let Some(req) = stream.try_next().await.unwrap() {
                file_read_requests += 1;
                match req {
                    FileRequest::Read { count, responder } => {
                        assert_eq!(count, 1);
                        responder.send(Status::OK.into_raw(), &[file_read_requests]).unwrap();
                    }
                    req => panic!("unhandled request {:?}", req),
                }
            }
        };

        let ((), ()) = join!(poll_read, handle_file_stream);
        assert_eq!(file_read_requests, 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_with_smaller_buf_after_pending() {
        let (client, mut stream) = endpoints::create_request_stream::<FileMarker>().unwrap();

        let mut reader = AsyncReader::from_client_end(client).unwrap();

        // Call poll_read with a buf of length 3. This is the first poll_read call, so the reader
        // will create a File.Read future for 3 bytes. poll_read will return Pending because nothing
        // is handling the FileRequestStream yet.
        let () = poll_fn(|cx| {
            assert_matches!(Pin::new(&mut reader).poll_read(cx, &mut [0u8; 3]), Poll::Pending);
            Poll::Ready(())
        })
        .await;

        // Respond to the three byte File.Read request.
        let () = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::Read { count, responder } => {
                    assert_eq!(count, 3);
                    responder.send(Status::OK.into_raw(), b"012").unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        }
        .await;

        // Call poll_read with a buf of length 1. This should resolve the previously created 3 byte
        // File.Read future and return the first byte from it while saving the remaining two bytes.
        let mut buf = [0u8; 1];
        assert_eq!(reader.read(&mut buf).await.unwrap(), buf.len());
        assert_eq!(&buf, b"0");

        // Call poll_read with a buf of len 1. This should return the first saved byte, which should
        // be the second byte from the original File.Read request.
        let mut buf = [0u8; 1];
        assert_eq!(reader.read(&mut buf).await.unwrap(), buf.len());
        assert_eq!(&buf, b"1");

        // Call poll_read with a buf of len 2. There should only be one remaining saved byte from
        // the original File.Read request, so poll_read should only return one byte.
        let mut buf = [0u8; 2];
        assert_eq!(reader.read(&mut buf).await.unwrap(), 1);
        assert_eq!(&buf[..1], b"2");

        // There should be no saved bytes remaining, so a poll_read of four bytes should cause a new
        // File.Read request.
        let mut buf = [0u8; 4];
        let poll_read = reader.read(&mut buf);

        let handle_second_file_request = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::Read { count, responder } => {
                    assert_eq!(count, 4);
                    responder.send(Status::OK.into_raw(), b"3456").unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (read_res, ()) = join!(poll_read, handle_second_file_request);
        assert_eq!(read_res.unwrap(), 4);
        assert_eq!(&buf, b"3456");
    }

    #[fasync::run_singlethreaded(test)]
    async fn transition_to_empty_on_fidl_error() {
        let (client, _) = endpoints::create_request_stream::<FileMarker>().unwrap();

        let mut reader = AsyncReader::from_client_end(client).unwrap();

        // poll_read will fail because the channel is closed because the server end was dropped.
        let () = poll_fn(|cx| {
            assert_matches!(
                Pin::new(&mut reader).poll_read(cx, &mut [0u8; 1]),
                Poll::Ready(Err(_))
            );
            Poll::Ready(())
        })
        .await;

        // This test is accessing internal state because the only fidl error that is easy to inject
        // is ZX_ERR_PEER_CLOSED (by closing the channel). Once the channel is closed, all new
        // futures created by the AsyncReader will fail, but, if poll'ed, the old future would also
        // continue to fail (not panic) because it is Fused.
        assert_matches!(reader.state, State::Empty);
    }

    #[fasync::run_singlethreaded(test)]
    async fn recover_from_file_read_error() {
        let (client, mut stream) = endpoints::create_request_stream::<FileMarker>().unwrap();

        let mut reader = AsyncReader::from_client_end(client).unwrap();

        // Call poll_read until failure.
        let mut buf = [0u8; 1];
        let poll_read = reader.read(&mut buf);

        let failing_file_response = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::Read { count, responder } => {
                    assert_eq!(count, 1);
                    responder.send(Status::NO_MEMORY.into_raw(), b"").unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (read_res, ()) = join!(poll_read, failing_file_response);
        assert_matches!(read_res, Err(_));

        // Calling poll_read again should create a new File.Read request instead of reusing the
        // old future.
        let mut buf = [0u8; 1];
        let poll_read = reader.read(&mut buf);

        let succeeding_file_response = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::Read { count, responder } => {
                    assert_eq!(count, 1);
                    responder.send(Status::OK.into_raw(), b"0").unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (read_res, ()) = join!(poll_read, succeeding_file_response);
        assert_eq!(read_res.unwrap(), 1);
        assert_eq!(&buf, b"0");
    }

    #[fasync::run_singlethreaded(test)]
    async fn poll_read_zero_then_read_nonzero() {
        let (client, mut stream) = endpoints::create_request_stream::<FileMarker>().unwrap();

        let mut reader = AsyncReader::from_client_end(client).unwrap();

        // Call poll_read with a zero-length buffer.
        let () = poll_fn(|cx| {
            assert_matches!(Pin::new(&mut reader).poll_read(cx, &mut []), Poll::Pending);
            Poll::Ready(())
        })
        .await;

        // Handle the zero-length File.Read request.
        match stream.next().await.unwrap().unwrap() {
            FileRequest::Read { count, responder } => {
                assert_eq!(count, 0);
                responder.send(Status::OK.into_raw(), &[]).unwrap();
            }
            req => panic!("unhandled request {:?}", req),
        }

        // Call poll_read with a length 1 buffer until Ready is returned;
        let mut buf = vec![0u8; 1];
        let poll_read = reader.read(&mut buf);

        // The AsyncReader will discard the File.Read response from the first poll_read, and create
        // another request, this handles that second request. The AsyncReader discards the first
        // response because the first poll_read was for zero bytes, but the current poll_read is
        // not.
        let handle_file_request = async {
            match stream.next().await.unwrap().unwrap() {
                FileRequest::Read { count, responder } => {
                    assert_eq!(count, 1);
                    responder.send(Status::OK.into_raw(), &[1]).unwrap();
                }
                req => panic!("unhandled request {:?}", req),
            }
        };

        let (poll_read, ()) = join!(poll_read, handle_file_request);

        // poll_read should read 1 byte, even though the first poll_read request was for zero bytes
        // and returned Pending.
        assert_eq!(poll_read.unwrap(), 1);
        assert_eq!(&buf[..], &[1]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn different_poll_read_and_file_sizes() {
        for first_poll_read_len in 0..5 {
            for file_size in 0..5 {
                for second_poll_read_len in 0..5 {
                    let (client, mut stream) =
                        endpoints::create_request_stream::<FileMarker>().unwrap();

                    let mut reader = AsyncReader::from_client_end(client).unwrap();

                    // poll_read causes the AsyncReader to create a File.Read request.
                    let () = poll_fn(|cx| {
                        let mut buf = vec![0u8; first_poll_read_len];
                        assert_matches!(
                            Pin::new(&mut reader).poll_read(cx, &mut buf),
                            Poll::Pending
                        );
                        Poll::Ready(())
                    })
                    .await;

                    // Respond to the File.Read request with at most as many bytes as the poll_read
                    // requested.
                    match stream.next().await.unwrap().unwrap() {
                        FileRequest::Read { count, responder } => {
                            assert_eq!(count, first_poll_read_len.try_into().unwrap());
                            let resp = vec![7u8; min(file_size, first_poll_read_len)];
                            responder.send(Status::OK.into_raw(), &resp).unwrap();
                        }
                        req => panic!("unhandled request {:?}", req),
                    }

                    // Call poll_read until it returns Ready. If the first poll_read was for zero
                    // bytes and this poll_read is not, the AsyncReader will make another File.Read
                    // request.
                    let mut buf = vec![0u8; second_poll_read_len];
                    let poll_read = reader.read(&mut buf);

                    let handle_conditional_file_request = async {
                        if first_poll_read_len == 0 && second_poll_read_len != 0 {
                            match stream.next().await.unwrap().unwrap() {
                                FileRequest::Read { count, responder } => {
                                    assert_eq!(count, second_poll_read_len.try_into().unwrap());
                                    let resp = vec![7u8; min(file_size, second_poll_read_len)];
                                    responder.send(Status::OK.into_raw(), &resp).unwrap();
                                }
                                req => panic!("unhandled request {:?}", req),
                            }
                        }
                    };

                    let (read_res, ()) = join!(poll_read, handle_conditional_file_request);

                    let expected_len = if first_poll_read_len == 0 {
                        min(file_size, second_poll_read_len)
                    } else {
                        min(first_poll_read_len, min(file_size, second_poll_read_len))
                    };
                    let expected = vec![7u8; expected_len];
                    assert_eq!(read_res.unwrap(), expected_len);
                    assert_eq!(&buf[..expected_len], &expected[..]);
                }
            }
        }
    }
}
