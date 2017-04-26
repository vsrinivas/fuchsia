// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An implementation of a client for a fidl interface.

use std::thread;
use std::sync::{Arc, Weak, Mutex};

use {EncodeBuf, DecodeBuf, MsgType, Error};
use {Future, Promise};
use cookiemap::CookieMap;

use magenta::{Channel, HandleBase, Packet, PacketContents, Port,
    SignalPacket, WaitAsyncOpts};
use magenta::{MX_TIME_INFINITE, MX_CHANNEL_READABLE, MX_CHANNEL_PEER_CLOSED};

#[derive(Clone)]
pub struct Client(Arc<ClientState>);

pub struct ClientState {
    channel: Channel,
    listener: Arc<ListenerThread>,
}

struct ListenerThread {
    // The reference to ClientState is to keep the channel open for pending requests, even if all
    // references to the Client are dropped. This could be optimized some (for example, a single
    // optional reference if the map is non-empty).
    pending: Mutex<Option<CookieMap<(Promise<DecodeBuf, Error>, Arc<ClientState>)>>>,
}

impl Client {
    /// Creates a new client, given the channel, and spawns a listener thread.
    // Note: spawning the listener thread could be lazy, waiting for the first send_msg_expect_response.
    pub fn new(channel: Channel) -> Self {
        let listener = Arc::new(ListenerThread::new());
        let client_state = Arc::new(ClientState {
            channel: channel,
            listener: listener.clone(),
        });
        ListenerThread::spawn(listener, Arc::downgrade(&client_state));
        Client(client_state)
    }

    pub fn send_msg(&self, buf: &mut EncodeBuf) {
        let (out_buf, handles) = buf.get_mut_content();
        let _ = self.0.channel.write(out_buf, handles, 0);
    }

    pub fn send_msg_expect_response(&self, buf: &mut EncodeBuf) -> Future<DecodeBuf, Error> {
        let (future, promise) = Future::make_promise();
        if let Some(id) = self.0.listener.take_promise(promise, &self.0) {
            buf.set_message_id(id);
            self.send_msg(buf);
            future
        } else {
            return Future::failed(Error::RemoteClosed);
        }
    }
}

// Helper function to extract a valid signal packet.
fn as_signal_packet(packet: &Packet) -> Option<SignalPacket> {
    if packet.status() == 0 {
        match packet.contents() {
            PacketContents::SignalOne(s) => return Some(s),
            PacketContents::SignalRep(s) => return Some(s),
            _ => (),
        }
    }
    None
}

// Since we're only waiting on one event, we don't need multiple keys.
const DEFAULT_KEY: u64 = 0;

impl ListenerThread {
    fn new() -> Self {
        ListenerThread {
            pending: Mutex::new(Some(CookieMap::new())),
        }
    }

    // This is designed so that it always waits on the channel. When all clients have gone
    // away (and there are no pending requests), the channel will get closed, and the wait
    // will complete.
    //
    // To facilitate this, the code is designed so that it's holding only a weak reference
    // to the channel object itself. When the last client's reference goes away, the wait
    // call will return with an error, and this code will clean up the thread. This is the
    // reason we use a multi-event wait primitive, even though we're only waiting on a
    // single channel.
    //
    // Another perfectly good way of designing this would be to only wait when responses
    // are pending. Then the thread would be parked when no responses are pending, and we'd
    // hold a strong reference to the client state (and thus the channel) when responses
    // were pending.
    //
    // But the current implementation is probably good enough for now.
    fn work(&self, client: Weak<ClientState>) {
        let port = if let Some(c) = client.upgrade() {
            if let Ok(port) = Port::create(Default::default()) {
                if c.channel.wait_async(&port, DEFAULT_KEY,
                        MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED, WaitAsyncOpts::Once).is_ok() {
                    port
                } else {
                    return;
                }
            } else {
                return;
            }
        } else {
            return;
        };
        loop {
            let wait_result = port.wait(MX_TIME_INFINITE);
            if let Some(packet) = wait_result.ok().and_then(|p| as_signal_packet(&p)) {
                if !packet.observed().contains(MX_CHANNEL_READABLE) {
                    break;
                }
                let mut buf = DecodeBuf::new();
                if let Some(c) = client.upgrade() {
                    let status = c.channel.read(0, buf.get_mut_buf());
                    if status.is_err() || buf.decode_message_header() != Some(MsgType::Response) {
                        break;
                    }
                    let id = buf.get_message_id();
                    let pending = self.pending.lock().unwrap().as_mut().unwrap().remove(id);
                    if let Some((promise, _client_state_ref)) = pending {
                        promise.set_ok(buf);
                        if c.channel.wait_async(&port, 0,
                            MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                            WaitAsyncOpts::Once).is_ok()
                        {
                            continue;
                        }
                    }
                }
            }
            break;
        }
        let pending = self.pending.lock().unwrap().take().unwrap();
        for (_id, (promise, _client_state_ref)) in pending {
            promise.set_err(Error::RemoteClosed);
        }
    }

    fn take_promise(&self, promise: Promise<DecodeBuf, Error>, client_ref: &Arc<ClientState>) -> Option<u64> {
        self.pending.lock().unwrap().as_mut().map(|pending|
            pending.insert((promise, client_ref.clone()))
        )
    }

    fn spawn(this: Arc<ListenerThread>, client: Weak<ClientState>) {
        let _ = thread::spawn(move || this.work(client));
    }
}
