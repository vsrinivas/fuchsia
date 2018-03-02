// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a server for a fidl interface.

use {DecodeBuf, EncodeBuf, Error, ErrorOrClose, FidlService, MsgType};

use async;
use std::io;
use futures::{Async, Future, Poll, Stream, task};
use futures::stream::FuturesUnordered;
use zircon;

/// A value from a server handler indicating that the current channel should be closed.
#[derive(Debug)]
pub struct CloseChannel;

/// The "stub" which handles raw FIDL buffer requests.
pub trait Stub: Send {
    /// The FIDL service type that the stub provides.
    type Service: FidlService;

    /// The type of the future that is resolved to a response.
    type DispatchResponseFuture: Future<Item = EncodeBuf, Error = ErrorOrClose> + Send;

    /// Dispatches a request and returns a future for the response.
    fn dispatch_with_response(&mut self, request: &mut DecodeBuf) -> Self::DispatchResponseFuture;

    /// The type of the future that dispatches a message with no response.
    type DispatchFuture: Future<Item = (), Error = ErrorOrClose> + Send;

    /// Dispatches a request and returns a future with no response.
    fn dispatch(&mut self, request: &mut DecodeBuf) -> Self::DispatchFuture;
}

#[must_use = "futures do nothing unless polled"]
struct DispatchResponseFutureWithId<S: Stub> {
    id: u64,
    future: S::DispatchResponseFuture,
}

impl<S: Stub> Future for DispatchResponseFutureWithId<S> {
    type Item = (u64, EncodeBuf);
    type Error = ErrorOrClose;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        match self.future.poll(cx) {
            Ok(Async::Ready(buf)) => Ok(Async::Ready((self.id, buf))),
            Ok(Async::Pending) => Ok(Async::Pending),
            Err(e) => Err(e),
        }
    }
}

/// FIDL server which processes requests from a channel and runs them through a `Stub`.
///
/// This type is a future which must be polled by an executor.
#[must_use = "futures do nothing unless polled"]
pub struct Server<S: Stub> {
    channel: async::Channel,
    stub: S,
    buf: DecodeBuf,
    dispatch_futures: FuturesUnordered<S::DispatchFuture>,
    dispatch_response_futures: FuturesUnordered<DispatchResponseFutureWithId<S>>,
}

impl<S: Stub> Server<S> {
    /// Create a new FIDL server on the given channel.
    pub fn new(stub: S, channel: async::Channel) -> io::Result<Self> {
        Ok(Server {
            stub,
            channel,
            buf: DecodeBuf::new(),
            dispatch_futures: FuturesUnordered::new(),
            dispatch_response_futures: FuturesUnordered::new(),
        })
    }
}

impl<S: Stub> Future for Server<S> {
    type Item = ();
    type Error = Error;

    fn poll(&mut self, cx: &mut task::Context) -> Poll<Self::Item, Self::Error> {
        loop {
            let mut made_progress_this_loop_iter = false;

            // Handle one dispatch_future at a time if any are available
            match self.dispatch_futures.poll_next(cx) {
                Ok(Async::Ready(Some(()))) => made_progress_this_loop_iter = true,
                Ok(_) => {},
                Err(ErrorOrClose::CloseChannel) => return Ok(Async::Ready(())),
                Err(ErrorOrClose::Error(e)) => return Err(e),
            }

            // Handle one dispatch_response_future at a time if any are available
            match self.dispatch_response_futures.poll_next(cx) {
                Ok(Async::Ready(Some((id, mut encode_buf)))) => {
                    encode_buf.set_message_id(id);
                    let (out_buf, handles) = encode_buf.get_mut_content();
                    self.channel.write(out_buf, handles).map_err(Error::ServerResponseWrite)?;
                    made_progress_this_loop_iter = true;
                },
                Ok(_) => {},
                Err(ErrorOrClose::CloseChannel) => return Ok(Async::Ready(())),
                Err(ErrorOrClose::Error(e)) => return Err(e),
            }

            // Now process incoming requests
            match self.channel.recv_from(self.buf.get_mut_buf(), cx) {
                Ok(Async::Ready(())) => {
                    match self.buf.decode_message_header() {
                        Some(MsgType::Request) => {
                            self.dispatch_futures.push(self.stub.dispatch(&mut self.buf));
                        }
                        Some(MsgType::RequestExpectsResponse) => {
                            let id = self.buf.get_message_id();
                            let future = self.stub.dispatch_with_response(&mut self.buf);
                            self.dispatch_response_futures.push(
                                DispatchResponseFutureWithId { id, future });
                        }
                        None | Some(MsgType::Response) => {
                            return Err(Error::InvalidHeader);
                        }
                    }
                }
                Ok(Async::Pending) => {
                    if !made_progress_this_loop_iter {
                        return Ok(Async::Pending);
                    }
                }
                Err(zircon::Status::PEER_CLOSED) => {
                    return Ok(Async::Ready(()));
                }
                Err(e) => return Err(Error::ServerRequestRead(e)),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use async::TimeoutExt;
    use futures::future;
    use futures::prelude::*;
    use byteorder::{ByteOrder, LittleEndian};
    use zircon::prelude::*;
    use zircon::{self, MessageBuf};
    use super::*;
    use {ClientEnd, ServerEnd, Result};

    struct DummyDispatcher;
    struct DummyService;

    impl FidlService for DummyService {
        type Proxy = ();
        fn new_proxy(_: ClientEnd<Self>) -> Result<Self::Proxy> {
            unimplemented!()
        }
        fn new_pair() -> Result<(Self::Proxy, ServerEnd<Self>)> {
            unimplemented!()
        }
        const NAME: &'static str = "DUMMY_SERVICE";
        const VERSION: u32 = 0;
    }

    impl Stub for DummyDispatcher {
        type Service = DummyService;

        type DispatchResponseFuture = future::FutureResult<EncodeBuf, ErrorOrClose>;
        fn dispatch_with_response(
            &mut self,
            _request: &mut DecodeBuf,
        ) -> Self::DispatchResponseFuture {
            let buf = EncodeBuf::new_response(43);
            future::ok(buf)
        }

        type DispatchFuture = future::FutureResult<(), ErrorOrClose>;
        fn dispatch(&mut self, request: &mut DecodeBuf) -> Self::DispatchFuture {
            let ordinal = LittleEndian::read_u32(&request.get_bytes()[8..12]);
            assert_eq!(ordinal, 42);
            future::ok(())
        }
    }

    #[test]
    fn simple_server() {
        let mut executor = async::Executor::new().unwrap();

        let req = EncodeBuf::new_request(42);

        let (client_end, server_end) = zircon::Channel::create().unwrap();
        let server_end = async::Channel::from_channel(server_end).unwrap();
        let dispatcher = DummyDispatcher;
        let server = Server::new(dispatcher, server_end).unwrap();

        // add a timeout to server
        let receiver = server.on_timeout(300.millis().after_now(), || Ok(())).unwrap();

        let sender = async::Timer::new(100.millis().after_now()).unwrap().map(|()| {
            let mut handles = Vec::new();
            client_end.write(req.get_bytes(), &mut handles).unwrap();
        });

        let done = receiver.join(sender);
        executor.run_singlethreaded(done).unwrap();
    }

    #[test]
    fn simple_response_server() {
        let mut executor = async::Executor::new().unwrap();

        let req = EncodeBuf::new_request_expecting_response(43);

        let (client_end, server_end) = zircon::Channel::create().unwrap();
        let server_end = async::Channel::from_channel(server_end).unwrap();
        let dispatcher = DummyDispatcher;
        let server = Server::new(dispatcher, server_end).unwrap();

        let client_end = async::Channel::from_channel(client_end).unwrap();

        // add a timeout to receiver so if test is broken it doesn't take forever
        let receiver = server.on_timeout(
            300.millis().after_now(),
            || panic!("server timed out")).unwrap();

        let sender = async::Timer::new(100.millis().after_now()).unwrap().and_then(|()| {
            let mut handles = Vec::new();
            client_end.write(req.get_bytes(), &mut handles).unwrap();
            let buffer = MessageBuf::new();
            client_end.recv_msg(buffer).map(|(_chan, buf)| {
                let bytes = &[24, 0, 0, 0, 1, 0, 0, 0, 43, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
                println!("{:?}", buf.bytes());
                assert_eq!(bytes, buf.bytes());
            })
        });

        let done = receiver.join(sender.map_err(Error::TestIo));
        executor.run_singlethreaded(done).unwrap();
    }
}
