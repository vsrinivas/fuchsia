// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a server for a fidl interface.

use {DecodeBuf, EncodeBuf, Error, FidlService, Result, MsgType};

use std::io;

use futures::{Async, Future, Poll};
use tokio_core::reactor::Handle;

use zircon::Channel;

use tokio_fuchsia;

/// The "stub" which handles raw FIDL buffer requests.
pub trait Stub {
    /// The FIDL service type that the stub provides.
    type Service: FidlService;

    /// The type of the future that is resolved to a response.
    type DispatchFuture: Future<Item = EncodeBuf, Error = Error>;

    /// Dispatches a request and returns a future for the response.
    fn dispatch_with_response(&mut self, request: &mut DecodeBuf) -> Self::DispatchFuture;

    /// Dispatches a request that doesn't have a response.
    fn dispatch(&mut self, request: &mut DecodeBuf) -> Result<()>;
}

/// FIDL server which processes requests from a channel and runs them through a `Stub`.
///
/// This type is a future which must be polled by an executor.
#[must_use = "futures do nothing unless polled"]
pub struct Server<S: Stub> {
    channel: tokio_fuchsia::Channel,
    stub: S,
    buf: DecodeBuf,
    // TODO(cramertj): consider spawning these separately or keeping them in a
    // map (rather than a vec) to improve performance
    dispatch_futures: Vec<(u64, S::DispatchFuture)>,
}

impl<S: Stub> Server<S> {
    /// Create a new FIDL server on the given channel.
    pub fn new(stub: S, channel: Channel, handle: &Handle) -> io::Result<Self> {
        Ok(Server {
            stub: stub,
            channel: tokio_fuchsia::Channel::from_channel(channel, handle)?,
            buf: DecodeBuf::new(),
            dispatch_futures: Vec::new(),
        })
    }
}

impl<S: Stub> Future for Server<S> {
    type Item = ();
    type Error = Error;

    fn poll(&mut self) -> Poll<Self::Item, Self::Error> {
        loop {
            // Handle one dispatch_future at a time if any are available
            let item = self.dispatch_futures.iter_mut().enumerate()
                           .filter_map(|(i, &mut (id, ref mut f))|
            {
                match f.poll() {
                    Ok(Async::NotReady) => None,
                    Ok(Async::Ready(e)) => Some((i, Ok((id, e)))),
                    Err(e) => Some((i, Err(e))),
                }
            }).next();

            if let Some((idx, res)) = item {
                self.dispatch_futures.remove(idx);
                let (id, mut encode_buf) = res?;
                encode_buf.set_message_id(id);
                let (out_buf, handles) = encode_buf.get_mut_content();
                self.channel.write(out_buf, handles, 0)?;
            }

            // Now process incoming requests
            try_nb!(self.channel.recv_from(0, self.buf.get_mut_buf()));

            match self.buf.decode_message_header() {
                Some(MsgType::Request) => {
                    self.stub.dispatch(&mut self.buf)?;
                }
                Some(MsgType::RequestExpectsResponse) => {
                    let id = self.buf.get_message_id();
                    let dispatch_future = self.stub.dispatch_with_response(&mut self.buf);
                    self.dispatch_futures.push((id, dispatch_future));
                }
                None | Some(MsgType::Response) => return Err(Error::InvalidHeader),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use tokio_core::reactor::{Core, Timeout};
    use std::time::Duration;
    use zircon::{self, MessageBuf, ChannelOpts};
    use tokio_fuchsia::Channel;
    use futures::future;
    use byteorder::{ByteOrder, LittleEndian};
    use super::*;
    use {ClientEnd, ServerEnd};

    struct DummyDispatcher;
    struct DummyService;

    impl FidlService for DummyService {
        type Proxy = ();
        fn new_proxy(_: ClientEnd<Self>, _: &Handle) -> Result<Self::Proxy> {
            unimplemented!()
        }
        fn new_pair(_: &Handle) -> Result<(Self::Proxy, ServerEnd<Self>)> {
            unimplemented!()
        }
        const NAME: &'static str = "DUMMY_SERVICE";
        const VERSION: u32 = 0;
    }

    impl Stub for DummyDispatcher {
        type Service = DummyService;

        type DispatchFuture = future::FutureResult<EncodeBuf, Error>;

        fn dispatch_with_response(&mut self, _request: &mut DecodeBuf) -> Self::DispatchFuture {
            let buf = EncodeBuf::new_response(43);
            future::ok(buf)
        }

        fn dispatch(&mut self, request: &mut DecodeBuf) -> Result<()> {
            let ordinal = LittleEndian::read_u32(&request.get_bytes()[8..12]);
            assert_eq!(ordinal, 42);
            Ok(())
        }
    }

    #[test]
    fn simple_server() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();
        let req = EncodeBuf::new_request(42);

        let (client_end, server_end) = zircon::Channel::create(ChannelOpts::Normal).unwrap();
        let dispatcher = DummyDispatcher;
        let server = Server::new(dispatcher, server_end, &handle).unwrap();

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map_err(|err| err.into());
        let receiver = server.select(rcv_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap().map(|()| {
            let mut handles = Vec::new();
            client_end.write(req.get_bytes(), &mut handles, 0).unwrap();
        }).map_err(|err| err.into());

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }

    #[test]
    fn simple_response_server() {
        let mut core = Core::new().unwrap();
        let handle = core.handle();
        let req = EncodeBuf::new_request_expecting_response(43);

        let (client_end, server_end) = zircon::Channel::create(ChannelOpts::Normal).unwrap();
        let dispatcher = DummyDispatcher;
        let server = Server::new(dispatcher, server_end, &handle).unwrap();

        let client_end = Channel::from_channel(client_end, &handle).unwrap();

        // add a timeout to receiver so if test is broken it doesn't take forever
        let rcv_timeout = Timeout::new(Duration::from_millis(300), &handle).unwrap().map_err(|err| err.into());
        let receiver = server.select(rcv_timeout).map(|(_,_)| ()).map_err(|(err,_)| err);

        let sender = Timeout::new(Duration::from_millis(100), &handle).unwrap().and_then(|()| {
            let mut handles = Vec::new();
            client_end.write(req.get_bytes(), &mut handles, 0).unwrap();
            let buffer = MessageBuf::new();
            client_end.recv_msg(0, buffer).map(|(_chan, buf)| {
                let bytes = &[24, 0, 0, 0, 1, 0, 0, 0, 43, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
                println!("{:?}", buf.bytes());
                assert_eq!(bytes, buf.bytes());
            })
        }).map_err(|err| err.into());

        let done = receiver.join(sender);
        core.run(done).unwrap();
    }
}
