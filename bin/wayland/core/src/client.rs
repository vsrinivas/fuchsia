// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use failure::Error;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use parking_lot::Mutex;

use crate::{IntoMessage, Message, ObjectId, ObjectMap, Registry};

/// The state of a single client connection. Each client connection will have
/// have its own zircon channel and its own set of protocol objects. The
/// |Registry| is the only piece of global state that is shared between
/// clients.
pub struct Client {
    /// The zircon channel used to communicate with this client.
    chan: Arc<fasync::Channel>,

    /// The set of objects for this client.
    objects: ObjectMap,

    /// A pointer to the global registry.
    registry: Arc<Mutex<Registry>>,
}

impl Client {
    /// Creates a new client.
    pub fn new(chan: fasync::Channel, registry: Arc<Mutex<Registry>>) -> Self {
        Client {
            registry,
            chan: Arc::new(chan),
            objects: ObjectMap::new(),
        }
    }

    /// Spawns an async task that waits for messages to be received on the
    /// zircon channel, decodes the messages, and dispatches them to the
    /// corresponding |MessageReceiver|s.
    pub fn start(mut self) {
        fasync::spawn_local(
            async move {
                loop {
                    // Wait on a message.
                    let mut buffer = zx::MessageBuf::new();
                    if let Err(e) = await!(self.chan.recv_msg(&mut buffer)) {
                        println!("Failed to receive message on the channel {}", e);
                        return;
                    }

                    // Dispatch the message.
                    if let Err(e) = self.receive_message(buffer.into()) {
                        println!("Failed to receive message on the channel {}", e);
                        return;
                    }
                }
            },
        );
    }

    /// A pointer to the global registry for this client.
    pub fn registry(&self) -> Arc<Mutex<Registry>> {
        self.registry.clone()
    }

    /// A pointer to the underlying zircon channel for this client.
    pub fn chan(&self) -> Arc<fasync::Channel> {
        self.chan.clone()
    }

    /// Provides a reference to the object map for this client.
    pub fn objects(&mut self) -> &mut ObjectMap {
        &mut self.objects
    }

    /// Reads the message header to find the target for this message and then
    /// forwards the message to the associated |MessageReceiver|.
    ///
    /// Returns Err if no object is associated with the sender field in the
    /// message header, or if the objects receiver itself fails.
    pub(crate) fn receive_message(&mut self, mut message: Message) -> Result<(), Error> {
        while !message.is_empty() {
            let header = message.read_header()?;
            // Lookup the table entry for this object & fail if there is no entry
            // found.
            let (receiver, spec) = self.objects.lookup_internal(&header)?;

            // Decode the argument stream and invoke the |MessageReceiver|.
            let args = message.read_args(spec.0)?;
            receiver(header.sender, header.opcode, args, self)?;
        }
        Ok(())
    }

    pub fn post<E: IntoMessage>(&self, sender: ObjectId, event: E) -> Result<(), Error> {
        let mut message = event.into_message(sender)?;
        let (bytes, mut handles) = message.take();
        self.chan.write(&bytes, &mut handles)?;
        Ok(())
    }
}
