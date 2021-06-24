// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Measurable,
    anyhow::{anyhow, Context as _},
    fidl_fuchsia_pkg::{
        BlobIdIteratorNextResponder, BlobIdIteratorRequest, BlobIdIteratorRequestStream,
        BlobInfoIteratorNextResponder, BlobInfoIteratorRequest, BlobInfoIteratorRequestStream,
        PackageIndexEntry, PackageIndexIteratorNextResponder, PackageIndexIteratorRequest,
        PackageIndexIteratorRequestStream,
    },
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::sys::ZX_CHANNEL_MAX_MSG_BYTES,
    futures::prelude::*,
};

// FIXME(52297) This constant would ideally be exported by the `fidl` crate.
// sizeof(TransactionHeader) + sizeof(VectorHeader)
const FIDL_VEC_RESPONSE_OVERHEAD_BYTES: usize = 32;

/// Helper to split a slice of items into chunks that will fit in a single FIDL vec response.
///
/// Note, Chunker assumes the fixed overhead of a single fidl response header and a single vec
/// header per chunk.  It must not be used with more complex responses.
struct Chunker<'a, I> {
    items: &'a mut [I],
}

impl<'a, I> Chunker<'a, I>
where
    I: Measurable,
{
    fn new(items: &'a mut [I]) -> Self {
        Self { items }
    }

    /// Produce the next chunk of items to respond with. Iteration stops when this method returns
    /// an empty slice, which occurs when either:
    /// * All items have been returned
    /// * Chunker encounters an item so large that it cannot even be stored in a response
    ///   dedicated to just that one item.
    ///
    /// Once next() returns an empty slice, it will continue to do so in future calls.
    fn next(&mut self) -> &'a mut [I] {
        let mut bytes_used: usize = FIDL_VEC_RESPONSE_OVERHEAD_BYTES;
        let mut entry_count = 0;

        for entry in &*self.items {
            bytes_used += entry.measure();
            if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize {
                break;
            }
            entry_count += 1;
        }

        // tmp/swap dance to appease the borrow checker.
        let tmp = std::mem::replace(&mut self.items, &mut []);
        let (chunk, rest) = tmp.split_at_mut(entry_count);
        self.items = rest;
        chunk
    }
}

/// A FIDL request stream for a FIDL protocol following the iterator pattern.
pub trait FidlIteratorRequestStream:
    fidl::endpoints::RequestStream + TryStream<Error = fidl::Error>
{
    type Responder: FidlIteratorNextResponder;

    fn request_to_responder(request: <Self as TryStream>::Ok) -> Self::Responder;
}

/// A responder to a Next() request for a FIDL iterator.
pub trait FidlIteratorNextResponder {
    type Item: Measurable + fidl::encoding::Encodable;

    fn send_chunk(self, chunk: &mut [Self::Item]) -> Result<(), fidl::Error>;
}

impl FidlIteratorRequestStream for PackageIndexIteratorRequestStream {
    type Responder = PackageIndexIteratorNextResponder;

    fn request_to_responder(request: PackageIndexIteratorRequest) -> Self::Responder {
        let PackageIndexIteratorRequest::Next { responder } = request;
        responder
    }
}

impl FidlIteratorNextResponder for PackageIndexIteratorNextResponder {
    type Item = PackageIndexEntry;

    fn send_chunk(self, chunk: &mut [Self::Item]) -> Result<(), fidl::Error> {
        self.send(&mut chunk.iter_mut())
    }
}

impl FidlIteratorRequestStream for BlobInfoIteratorRequestStream {
    type Responder = BlobInfoIteratorNextResponder;

    fn request_to_responder(request: BlobInfoIteratorRequest) -> Self::Responder {
        let BlobInfoIteratorRequest::Next { responder } = request;
        responder
    }
}

impl FidlIteratorRequestStream for BlobIdIteratorRequestStream {
    type Responder = BlobIdIteratorNextResponder;

    fn request_to_responder(request: BlobIdIteratorRequest) -> Self::Responder {
        let BlobIdIteratorRequest::Next { responder } = request;
        responder
    }
}

impl FidlIteratorNextResponder for BlobInfoIteratorNextResponder {
    type Item = fidl_fuchsia_pkg::BlobInfo;

    fn send_chunk(self, chunk: &mut [Self::Item]) -> Result<(), fidl::Error> {
        self.send(&mut chunk.iter_mut())
    }
}

impl FidlIteratorNextResponder for BlobIdIteratorNextResponder {
    type Item = fidl_fuchsia_pkg::BlobId;

    fn send_chunk(self, chunk: &mut [Self::Item]) -> Result<(), fidl::Error> {
        self.send(&mut chunk.iter_mut())
    }
}

/// Serves the provided `FidlIteratorRequestStream` with as many entries per `Next()` request as
/// will fit in a fidl message. The task completes after yielding an empty response or the iterator
/// is interrupted (client closes the channel or this task encounters a FIDL layer error).
pub fn serve_fidl_iterator<I>(
    mut items: impl AsMut<[<I::Responder as FidlIteratorNextResponder>::Item]>,
    mut stream: I,
) -> impl Future<Output = ()>
where
    I: FidlIteratorRequestStream,
{
    async move {
        let mut items = Chunker::new(items.as_mut());

        loop {
            let mut chunk = items.next();

            let responder =
                match stream.try_next().await.context("while waiting for next() request")? {
                    None => break,
                    Some(request) => I::request_to_responder(request),
                };

            let () = responder.send_chunk(&mut chunk).context("while responding")?;

            // Yield a single empty chunk, then stop serving the protocol.
            if chunk.is_empty() {
                break;
            }
        }

        Ok(())
    }
    .unwrap_or_else(|e: anyhow::Error| {
        fx_log_err!(
            "error serving {} protocol: {:#}",
            <I::Service as fidl::endpoints::ServiceMarker>::DEBUG_NAME,
            anyhow!(e)
        )
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    struct Byte(u8);

    impl Measurable for Byte {
        fn measure(&self) -> usize {
            1
        }
    }

    #[test]
    fn chunker_fuses() {
        let items = &mut [Byte(42)];
        let mut chunker = Chunker::new(items);

        assert_eq!(chunker.next(), &mut [Byte(42)]);
        assert_eq!(chunker.next(), &mut []);
        assert_eq!(chunker.next(), &mut []);
    }

    #[test]
    fn chunker_chunks_at_expected_boundary() {
        const BYTES_PER_CHUNK: usize =
            ZX_CHANNEL_MAX_MSG_BYTES as usize - FIDL_VEC_RESPONSE_OVERHEAD_BYTES;

        // Expect to fill 2 full chunks with 1 item left over.
        let mut items =
            (0..=(BYTES_PER_CHUNK as u64 * 2)).map(|n| Byte(n as u8)).collect::<Vec<Byte>>();
        let expected = items.clone();
        let mut chunker = Chunker::new(&mut items);

        let mut actual: Vec<Byte> = vec![];

        for _ in 0..2 {
            let chunk = chunker.next();
            assert_eq!(chunk.len(), BYTES_PER_CHUNK);

            actual.extend(&*chunk);
        }

        let chunk = chunker.next();
        assert_eq!(chunk.len(), 1);
        actual.extend(&*chunk);

        assert_eq!(actual, expected);
    }

    #[test]
    fn chunker_terminates_at_too_large_item() {
        #[derive(Debug, PartialEq, Eq)]
        struct TooBig;
        impl Measurable for TooBig {
            fn measure(&self) -> usize {
                ZX_CHANNEL_MAX_MSG_BYTES as usize
            }
        }

        let items = &mut [TooBig];
        let mut chunker = Chunker::new(items);
        assert_eq!(chunker.next(), &mut []);
    }
}
