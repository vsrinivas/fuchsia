#![doc(html_root_url = "https://docs.rs/http-body/0.1.0")]
#![deny(missing_debug_implementations, missing_docs, unreachable_pub)]
#![cfg_attr(test, deny(warnings))]

//! Asynchronous HTTP request or response body.
//!
//! See [`Body`] for more details.
//!
//! [`Body`]: trait.Body.html

extern crate bytes;
extern crate futures;
extern crate http;
extern crate tokio_buf;

use bytes::Buf;
use futures::{Async, Poll};
use http::HeaderMap;
use tokio_buf::{BufStream, SizeHint};

/// Trait representing a streaming body of a Request or Response.
///
/// Data is streamed via the `poll_data` function, which asynchronously yields `T: Buf` values. The
/// `size_hint` function provides insight into the total number of bytes that will be streamed.
///
/// The `poll_trailers` function returns an optional set of trailers used to finalize the request /
/// response exchange. This is mostly used when using the HTTP/2.0 protocol.
///
/// # Relation with [`BufStream`].
///
/// The `Body` trait is a superset of the `BufStream` trait. However, `BufStream` is not considered
/// a super trait of `Body`. Instead, a `T: Body` can be thought of as containing a `BufStream` as
/// well as the HTTP trailers.
///
/// There exists is a blanket implementation of `Body` for `T: BufStream`. In other words, any type
/// that implements `BufStream` also implements `Body` yielding no trailers.
///
/// [`BufStream`]: https://docs.rs/tokio-buf/0.1.1/tokio_buf/trait.BufStream.html
///
pub trait Body {
    /// Values yielded by the `Body`.
    type Data: Buf;

    /// The error type this `BufStream` might generate.
    type Error;

    /// Attempt to pull out the next data buffer of this stream.
    fn poll_data(&mut self) -> Poll<Option<Self::Data>, Self::Error>;

    /// Returns the bounds on the remaining length of the stream.
    ///
    /// When the **exact** remaining length of the stream is known, the upper bound will be set and
    /// will equal the lower bound.
    fn size_hint(&self) -> SizeHint {
        SizeHint::default()
    }

    /// Poll for an optional **single** `HeaderMap` of trailers.
    ///
    /// This function should only be called once `poll_data` returns `None`.
    fn poll_trailers(&mut self) -> Poll<Option<HeaderMap>, Self::Error>;

    /// Returns `true` when the end of stream has been reached.
    ///
    /// An end of stream means that both `poll_data` and `poll_trailers` will
    /// return `None`.
    ///
    /// A return value of `false` **does not** guarantee that a value will be
    /// returned from `poll_stream` or `poll_trailers`.
    fn is_end_stream(&self) -> bool {
        false
    }
}

impl<T: BufStream> Body for T {
    type Data = T::Item;
    type Error = T::Error;

    fn poll_data(&mut self) -> Poll<Option<Self::Data>, Self::Error> {
        BufStream::poll_buf(self)
    }

    fn size_hint(&self) -> SizeHint {
        BufStream::size_hint(self)
    }

    fn poll_trailers(&mut self) -> Poll<Option<HeaderMap>, Self::Error> {
        Ok(Async::Ready(None))
    }

    fn is_end_stream(&self) -> bool {
        let size_hint = self.size_hint();

        size_hint
            .upper()
            .map(|upper| upper == 0 && upper == size_hint.lower())
            .unwrap_or(false)
    }
}
