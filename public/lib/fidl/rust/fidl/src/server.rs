// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a server for a fidl interface.

use {DecodeBuf, EncodeBuf, Future, Error, Result, MsgType};

use std::sync::Arc;
use std::thread;

use magenta::{Channel, HandleBase};
use magenta::{MX_CHANNEL_READABLE, MX_CHANNEL_PEER_CLOSED};

use magenta::MX_TIME_INFINITE;

pub trait Stub {
    #[allow(non_snake_case)]
    fn dispatch_with_response_Stub(&mut self, request: &mut DecodeBuf) -> Future<EncodeBuf, Error>;

    #[allow(non_snake_case)]
    fn dispatch_Stub(&mut self, request: &mut DecodeBuf) -> Result<()>;
}

#[macro_export]
macro_rules! impl_fidl_stub {
    ( $impl_ty:ty : $( $stub:tt )* ) => {
        impl $crate::Stub for $impl_ty {
            // consider taking a &mut EncodeBuf for result and letting server do lifetime mgmt
            fn dispatch_with_response_Stub(&mut self, request: &mut $crate::DecodeBuf)
                    -> $crate::Future<$crate::EncodeBuf, $crate::Error>
            {
                $( $stub )* ::dispatch_with_response_Impl(self, request)
            }

            fn dispatch_Stub(&mut self, request: &mut $crate::DecodeBuf) -> $crate::Result<()> {
                $( $stub )* ::dispatch_Impl(self, request)
            }
        }
    };
}

pub struct Server<S> {
    stub: S,
    channel: Arc<Channel>,
}

impl<S: Stub + Send + 'static> Server<S> {
    pub fn new(stub: S, channel: Channel) -> Self {
        Server {
            stub: stub,
            channel: Arc::new(channel),
        }
    }

    pub fn work(&mut self) {
        let wait_sigs = MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED;
        let mut buf = DecodeBuf::new();
        loop {
            match self.channel.wait(wait_sigs, MX_TIME_INFINITE) {
                Ok(signals) => {
                    if signals.contains(MX_CHANNEL_PEER_CLOSED) {
                        break;
                    }
                }
                Err(_) => break,
            }
            let status = self.channel.read(0, buf.get_mut_buf());
            if status.is_err() {
                break;
            }
            match buf.decode_message_header() {
                Some(MsgType::Request) => {
                    let status = self.stub.dispatch_Stub(&mut buf);
                    if status.is_err() {
                        break;
                    }
                }
                Some(MsgType::RequestExpectsResponse) => {
                    let id = buf.get_message_id();
                    let result_future = self.stub.dispatch_with_response_Stub(&mut buf);
                    let weak_channel = Arc::downgrade(&self.channel);
                    result_future.with(move |result|
                        if let Ok(mut value) = result {
                            if let Some(channel) = weak_channel.upgrade() {
                                value.set_message_id(id);
                                let (out_buf, handles) = value.get_mut_content();
                                let _ = channel.write(out_buf, handles, 0);
                            }
                            value.recycle();
                        }
                    );
                }
                _ => break
            }
        }
    }

    pub fn spawn(mut self) -> thread::JoinHandle<()> {
        thread::spawn(move || self.work())
    }
}
