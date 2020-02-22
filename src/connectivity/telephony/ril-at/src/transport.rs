// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use std::{collections::VecDeque, str::Utf8Error, sync::Arc};

#[derive(Clone, Debug)]
pub enum AtTransportError {
    ChannelClosed,
    ClientRead { error: zx::Status },
    ClientWrite { error: zx::Status },
    Deserialize { bytes: Vec<u8>, error: Utf8Error },
}

struct AtMessage {
    request: String,
    response: Option<Result<String, AtTransportError>>,
}

impl AtMessage {
    fn new(request: String) -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(AtMessage { request, response: None }))
    }
}

pub struct AtTransport {
    messages: Mutex<VecDeque<Arc<Mutex<AtMessage>>>>,
    transport_channel: Mutex<fasync::Channel>,
}

impl AtTransport {
    pub fn new(chan: fasync::Channel) -> Self {
        AtTransport { transport_channel: Mutex::new(chan), messages: Mutex::new(VecDeque::new()) }
    }

    pub async fn send_msg(&mut self, string_msg: String) -> Result<String, AtTransportError> {
        let msg = self.enqueue_message(string_msg).await;
        self.do_sends_and_recvs().await;

        let msg = msg.lock().await;
        msg.response.clone().unwrap() // Unwraps the option set in do_sends_and_recvs
    }

    async fn enqueue_message(&self, string_msg: String) -> Arc<Mutex<AtMessage>> {
        let msg = AtMessage::new(string_msg);

        let messages = &mut self.messages.lock().await;
        messages.push_back(msg.clone());

        msg
    }

    async fn do_sends_and_recvs(&mut self) -> () {
        // Serialize message send/recvs.
        let transport_channel = self.transport_channel.lock().await;

        'messages: loop {
            let message = {
                let mut messages = self.messages.lock().await;
                match messages.pop_front() {
                    None => break 'messages,
                    Some(m) => m,
                }
            };
            let mut message = message.lock().await;

            let response = self.send_one(&transport_channel, &message.request).await;
            message.response = Some(response);
        }
    }

    async fn send_one(
        &self,
        transport: &fasync::Channel,
        request: &String,
    ) -> Result<String, AtTransportError> {
        let request_bytes = Self::serialize_request(request);

        transport
            .write(&request_bytes, /* no handles */ &mut vec![])
            .map_err(|error| AtTransportError::ClientWrite { error })?;

        // Loop over unsolicited events until we get a response or an error.
        loop {
            let response_buf = Self::recv(transport).await?;
            let response = Self::deserialize_response(response_buf.bytes().to_vec())?;

            if Self::is_unsolicited_event(&response) {
                // TODO Handle these
            } else {
                return Ok(response);
            }
        }
    }

    fn serialize_request(request: &String) -> Vec<u8> {
        request.as_bytes().to_vec()
    }

    fn deserialize_response(response_bytes: Vec<u8>) -> Result<String, AtTransportError> {
        String::from_utf8(response_bytes).map_err(|error| AtTransportError::Deserialize {
            bytes: error.as_bytes().to_vec(),
            error: error.utf8_error(),
        })
    }

    fn is_unsolicited_event(_response: &String) -> bool {
        // TODO Fix this.
        false
    }

    async fn recv(transport_channel: &fasync::Channel) -> Result<zx::MessageBuf, AtTransportError> {
        let mut response_buf = zx::MessageBuf::new();
        match transport_channel.recv_msg(&mut response_buf).await {
            Ok(()) => return Ok(response_buf),
            Err(zx::Status::PEER_CLOSED) => return Err(AtTransportError::ChannelClosed),
            Err(error) => return Err(AtTransportError::ClientRead { error }),
        }
    }
}
