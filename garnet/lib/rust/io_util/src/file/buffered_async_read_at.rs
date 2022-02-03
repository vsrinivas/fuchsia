// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::file::{AsyncGetSize, AsyncReadAt},
    pin_project::pin_project,
    std::{
        cmp::min,
        convert::TryInto as _,
        pin::Pin,
        task::{Context, Poll},
    },
};

// Checked addition on `usize` that fails with `std::io::Error` instead of `None` to reduce clutter.
trait UsizeExt {
    fn add(self, rhs: usize) -> Result<usize, std::io::Error>;
}

impl UsizeExt for usize {
    fn add(self, rhs: usize) -> Result<usize, std::io::Error> {
        self.checked_add(rhs).ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::Other, "usize addition overflowed")
        })
    }
}

fn u64_to_usize_safe(u: u64) -> usize {
    let ret: usize = u.try_into().unwrap();
    static_assertions::assert_eq_size_val!(u, ret);
    ret
}

/// Wraps an `AsyncReadAt` with an in-memory buffer of size `fidl_fuchsia_io::MAX_TRANSFER_SIZE`
/// in which it stores the results of `poll_read_at` calls made to the wrapped `AsyncReadAt`.
///
/// Calls to `poll_read_at` that begin in the buffer will be serviced only from the buffer
/// without interacting with the wrapped `AsyncReadAt`, therefore the read will be short if the
/// buffer ends before the requested range.
///
/// Calls to `poll_read_at` that do not begin in the buffer will be forwarded
/// to the wrapped `AsyncReadAt` with the length of the forwarded buffer always exactly
/// `fidl_fuchsia_io::MAX_TRANSFER_SIZE`, therefore calls to `poll_read_at` requesting more than
/// `fidl_fuchsia_io::MAX_TRANSFER_SIZE` bytes will always be short.
#[pin_project]
pub struct BufferedAsyncReadAt<T> {
    #[pin]
    wrapped: T,
    // Offset of `cache` relative to beginning of `wrapped`.
    offset: usize,
    // Length of valid `cache` contents.
    len: usize,
    cache: Option<Box<[u8; fidl_fuchsia_io::MAX_TRANSFER_SIZE as usize]>>,
}

impl<T> BufferedAsyncReadAt<T> {
    pub fn new(wrapped: T) -> Self {
        Self { wrapped, offset: 0, len: 0, cache: None }
    }
}

impl<T: AsyncReadAt> AsyncReadAt for BufferedAsyncReadAt<T> {
    fn poll_read_at(
        self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        offset_u64: u64,
        buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        let this = self.project();
        let offset = u64_to_usize_safe(offset_u64);

        let cache = this
            .cache
            .get_or_insert_with(|| Box::new([0u8; fidl_fuchsia_io::MAX_TRANSFER_SIZE as usize]));

        if *this.offset <= offset && this.offset.add(*this.len)? > offset {
            let start = offset - *this.offset;
            let n = min(buf.len(), *this.len - start);
            let () = buf[..n].copy_from_slice(&cache[start..start + n]);
            return Poll::Ready(Ok(n));
        }

        // AsyncReadAt::poll_read_at only modifies `cache` on success, so `cache` does not need to
        // be invalidated.
        match this.wrapped.poll_read_at(cx, offset_u64, &mut cache[..]) {
            Poll::Pending => return Poll::Pending,
            Poll::Ready(Ok(len)) => {
                *this.offset = offset;
                *this.len = len;
                let n = min(len, buf.len());
                let () = buf[..n].copy_from_slice(&cache[..n]);
                return Poll::Ready(Ok(n));
            }
            p @ Poll::Ready(_) => {
                return p;
            }
        }
    }
}

impl<T: AsyncGetSize> AsyncGetSize for BufferedAsyncReadAt<T> {
    fn poll_get_size(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<std::io::Result<u64>> {
        let this = self.project();
        this.wrapped.poll_get_size(cx)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::file::{AsyncGetSizeExt as _, AsyncReadAtExt as _},
        assert_matches::assert_matches,
        std::{cell::RefCell, convert::TryFrom as _, rc::Rc},
    };

    #[test]
    fn max_transfer_size_fits_in_usize() {
        assert_eq!(
            fidl_fuchsia_io::MAX_TRANSFER_SIZE,
            u64::try_from(usize::try_from(fidl_fuchsia_io::MAX_TRANSFER_SIZE).unwrap()).unwrap()
        );
    }

    #[test]
    fn usize_ext_add() {
        assert_eq!(0usize.add(1).unwrap(), 1);
        assert_matches!(usize::MAX.add(1), Err(_));
    }

    #[test]
    fn u64_to_usize_safe() {
        assert_eq!(super::u64_to_usize_safe(5u64), 5usize);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn poll_get_size_forwards() {
        struct Mock {
            called: bool,
        }
        impl AsyncGetSize for Mock {
            fn poll_get_size(
                mut self: Pin<&mut Self>,
                _: &mut Context<'_>,
            ) -> Poll<std::io::Result<u64>> {
                self.called = true;
                Poll::Ready(Ok(3))
            }
        }

        let mut reader = BufferedAsyncReadAt::new(Mock { called: false });

        assert_matches!(reader.get_size().await, Ok(3));
        assert!(reader.wrapped.called);
    }

    struct Mock {
        recorded_offsets: Rc<RefCell<Vec<u64>>>,
        content: Vec<u8>,
    }
    impl Mock {
        fn new(content: Vec<u8>) -> (Self, Rc<RefCell<Vec<u64>>>) {
            let recorded_offsets = Rc::new(RefCell::new(vec![]));
            (Self { recorded_offsets: recorded_offsets.clone(), content }, recorded_offsets)
        }
    }
    impl AsyncReadAt for Mock {
        fn poll_read_at(
            self: Pin<&mut Self>,
            _cx: &mut Context<'_>,
            offset: u64,
            buf: &mut [u8],
        ) -> Poll<std::io::Result<usize>> {
            self.recorded_offsets.borrow_mut().push(offset);
            let offset = super::u64_to_usize_safe(offset);
            assert_eq!(buf.len(), usize::try_from(fidl_fuchsia_io::MAX_TRANSFER_SIZE).unwrap());
            let start = std::cmp::min(offset, self.content.len());
            let n = std::cmp::min(buf.len(), self.content.len() - start);
            let end = start + n;
            buf[..n].copy_from_slice(&self.content[start..end]);
            Poll::Ready(Ok(n))
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn poll_read_at_uses_cache() {
        let (mock, offsets) = Mock::new(vec![0, 1, 2, 3, 4]);
        let mut reader = BufferedAsyncReadAt::new(mock);

        // First read forwarded to backing store.
        let mut buf = vec![5; 3];
        let bytes_read = reader.read_at(1, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 3);
        assert_eq!(buf, vec![1, 2, 3]);
        assert_eq!(*offsets.borrow(), vec![1]);

        // Second read starts at beginning of cache, ends early, uses cache.
        offsets.borrow_mut().clear();

        let mut buf = vec![5; 2];
        let bytes_read = reader.read_at(1, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 2);
        assert_eq!(buf, vec![1, 2]);
        assert_eq!(*offsets.borrow(), Vec::<u64>::new());

        // Third read starts in middle of cache, ends early, uses cache.
        let mut buf = vec![5; 2];
        let bytes_read = reader.read_at(2, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 2);
        assert_eq!(buf, vec![2, 3]);
        assert_eq!(*offsets.borrow(), Vec::<u64>::new());

        // Fourth read aligns exactly with cache (including the extra bytes that were not
        // explicitly requested in the first read but that were eagerly requested by
        // BufferedAsyncRead), uses cache.
        let mut buf = vec![5; 4];
        let bytes_read = reader.read_at(1, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 4);
        assert_eq!(buf, vec![1, 2, 3, 4]);
        assert_eq!(*offsets.borrow(), Vec::<u64>::new());

        // Fifth read includes only the byte eagerly fetched by BufferedAsyncRead, uses cache.
        let mut buf = vec![5; 1];
        let bytes_read = reader.read_at(4, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 1);
        assert_eq!(buf, vec![4]);
        assert_eq!(*offsets.borrow(), Vec::<u64>::new());

        // Sixth read starts in the middle of the cache and goes past the end, uses cache.
        let mut buf = vec![5; 3];
        let bytes_read = reader.read_at(3, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 2);
        assert_eq!(buf, vec![3, 4, 5]);
        assert_eq!(*offsets.borrow(), Vec::<u64>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn poll_read_at_forwards() {
        let content = (0u8..8)
            .into_iter()
            .cycle()
            .take(fidl_fuchsia_io::MAX_TRANSFER_SIZE.try_into().unwrap())
            .chain([8])
            .collect();
        let (mock, offsets) = Mock::new(content);
        let mut reader = BufferedAsyncReadAt::new(mock);

        // First read forwarded to backing store.
        let mut buf = vec![9; 1];
        let bytes_read = reader.read_at(1, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 1);
        assert_eq!(buf, vec![1]);
        assert_eq!(*offsets.borrow(), vec![1]);

        // Second read starts before cache, forwarded to backing store.
        offsets.borrow_mut().clear();

        let mut buf = vec![9; 1];
        let bytes_read = reader.read_at(0, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 1);
        assert_eq!(buf, vec![0]);
        assert_eq!(*offsets.borrow(), vec![0]);

        // Third read starts at end of cache, forwarded to backing store.
        offsets.borrow_mut().clear();

        let mut buf = vec![9; 1];
        let bytes_read =
            reader.read_at(fidl_fuchsia_io::MAX_TRANSFER_SIZE, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 1);
        assert_eq!(buf, vec![8]);
        assert_eq!(*offsets.borrow(), vec![fidl_fuchsia_io::MAX_TRANSFER_SIZE]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn poll_read_at_requested_range_ends_beyond_content() {
        let (mock, offsets) = Mock::new(vec![0, 1, 2]);
        let mut reader = BufferedAsyncReadAt::new(mock);

        let mut buf = vec![3; 5];
        let bytes_read = reader.read_at(0, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 3);
        assert_eq!(buf, vec![0, 1, 2, 3, 3]);
        assert_eq!(*offsets.borrow(), vec![0]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn poll_read_at_requested_range_starts_beyond_content() {
        let (mock, offsets) = Mock::new(vec![0, 1, 2]);
        let mut reader = BufferedAsyncReadAt::new(mock);

        let mut buf = vec![3; 5];
        let bytes_read = reader.read_at(3, buf.as_mut_slice()).await.unwrap();

        assert_eq!(bytes_read, 0);
        assert_eq!(buf, vec![3, 3, 3, 3, 3]);
        assert_eq!(*offsets.borrow(), vec![3]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn poll_read_at_forwards_error() {
        struct Mock;
        impl AsyncReadAt for Mock {
            fn poll_read_at(
                self: Pin<&mut Self>,
                _cx: &mut Context<'_>,
                _offset: u64,
                _buf: &mut [u8],
            ) -> Poll<std::io::Result<usize>> {
                Poll::Ready(Err(std::io::Error::new(
                    std::io::ErrorKind::Other,
                    "BufferedAsyncReadAt forwarded the error",
                )))
            }
        }

        let mut reader = BufferedAsyncReadAt::new(Mock);

        let mut buf = vec![0u8; 1];
        let res = reader.read_at(0, buf.as_mut_slice()).await;

        assert_matches!(res, Err(_));
        assert_eq!(res.err().unwrap().to_string(), "BufferedAsyncReadAt forwarded the error");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn poll_read_at_forwards_pending() {
        struct Mock;
        impl AsyncReadAt for Mock {
            fn poll_read_at(
                self: Pin<&mut Self>,
                _cx: &mut Context<'_>,
                _offset: u64,
                _buf: &mut [u8],
            ) -> Poll<std::io::Result<usize>> {
                Poll::Pending
            }
        }

        #[pin_project]
        struct VerifyPending {
            #[pin]
            object_under_test: BufferedAsyncReadAt<Mock>,
        }
        impl futures::future::Future for VerifyPending {
            type Output = ();
            fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
                let this = self.project();
                let res = this.object_under_test.poll_read_at(cx, 0, &mut [0]);
                assert_matches!(res, Poll::Pending);
                Poll::Ready(())
            }
        }

        let reader = BufferedAsyncReadAt::new(Mock);
        let verifier = VerifyPending { object_under_test: reader };

        let () = verifier.await;
    }
}
