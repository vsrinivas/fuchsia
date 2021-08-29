// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client, crate::object::MessageReceiver, anyhow::Error,
    fuchsia_wayland_core as wl, std::mem,
};

/// Helper for constructing a |Registry|.
///
/// A proper registry implementation can support adding and removing |Global|s
/// at runtime. Since we do not yet support these features, we will only allow
/// |Global|s to be added at initialization time and then leave the |Registry|
/// immutable.
///
/// Note: the |Registry| only works for single-threaded event loops.
///
/// TODO(tjdetwiler): Allow interfaces to be added and removed from the
/// |Registry| dynamically.
pub struct RegistryBuilder {
    globals: Vec<Global>,
}

impl RegistryBuilder {
    pub fn new() -> Self {
        RegistryBuilder { globals: Vec::new() }
    }

    /// Adds a new global interface to the registry.
    ///
    /// The wayland interface name & version are identified by the |Interface|
    /// provided. When a client attempts to bind to this global using
    /// |wl_registry::bind|, the closure will be called to create a
    /// |MessageReceiver| that will be use to handle requests to the instance
    /// of that global.
    pub fn add_global<
        I: wl::Interface + 'static,
        F: FnMut(wl::ObjectId, u32, &mut Client) -> Result<Box<dyn MessageReceiver>, Error>
            + Send
            + 'static,
    >(
        &mut self,
        _: I,
        bind: F,
    ) -> &mut Self {
        self.globals.push(Global {
            name: I::NAME,
            version: I::VERSION,
            requests: &I::REQUESTS,
            bind_fn: Box::new(bind),
        });
        self
    }

    pub fn build(&mut self) -> Registry {
        Registry { globals: mem::replace(&mut self.globals, vec![]) }
    }
}

/// The |Registry| holds the global interfaces that are advertised to clients.
pub struct Registry {
    globals: Vec<Global>,
}

impl Registry {
    pub fn globals(&mut self) -> &mut [Global] {
        self.globals.as_mut_slice()
    }
}

pub struct Global {
    name: &'static str,
    version: u32,
    requests: &'static wl::MessageGroupSpec,
    bind_fn: Box<
        dyn FnMut(wl::ObjectId, u32, &mut Client) -> Result<Box<dyn MessageReceiver>, Error> + Send,
    >,
}

impl Global {
    /// The wayland interface name for this global.
    pub fn interface(&self) -> &str {
        self.name
    }

    /// The wayland interface version for this global.
    pub fn version(&self) -> u32 {
        self.version
    }

    /// A descriptor of the set of requests this global can handle.
    pub fn request_spec(&self) -> &'static wl::MessageGroupSpec {
        self.requests
    }

    /// Create a new object instance for this global. The returned
    /// |MessageReceiver| will be used to handle all requests for the new
    /// object.
    pub fn bind(
        &mut self,
        id: wl::ObjectId,
        version: u32,
        client: &mut Client,
    ) -> Result<Box<dyn MessageReceiver>, Error> {
        (*self.bind_fn)(id, version, client)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::sync::Arc;

    use crate::test_protocol::{TestInterface, TestInterface2};
    use anyhow::Error;
    use fuchsia_async as fasync;
    use fuchsia_wayland_core::{Interface, IntoMessage};
    use fuchsia_zircon as zx;
    use parking_lot::Mutex;

    use crate::client::Client;
    use crate::display::Display;
    use crate::object::RequestDispatcher;
    use crate::test_protocol::*;

    #[test]
    fn registry_bind() -> Result<(), Error> {
        // We'll pass this to our globals and verify that the right number of
        // messages are delivered.
        #[derive(Default)]
        struct Counts {
            interface_1_bind_count: usize,
            interface_2_bind_count: usize,
        }
        let counts: Arc<Mutex<Counts>> = Arc::new(Mutex::new(Default::default()));
        let mut registry = RegistryBuilder::new();

        {
            let counts = counts.clone();
            registry.add_global(TestInterface, move |_, _, _| {
                counts.lock().interface_1_bind_count += 1;
                Ok(Box::new(RequestDispatcher::new(TestReceiver::new())))
            });
        }
        {
            let counts = counts.clone();
            registry.add_global(TestInterface2, move |_, _, _| {
                counts.lock().interface_2_bind_count += 1;
                Ok(Box::new(RequestDispatcher::new(TestReceiver::new())))
            });
        }

        // Build the registry & verify initial counts.
        let registry = registry.build();
        assert_eq!(0, counts.lock().interface_1_bind_count);
        assert_eq!(0, counts.lock().interface_2_bind_count);

        // Bind to the globals.
        let (c1, _c2) = zx::Channel::create()?;
        let _executor = fasync::LocalExecutor::new();
        let display = Display::new_no_scenic(registry).expect("Failed to create display");
        let mut client = Client::new(fasync::Channel::from_channel(c1)?, display.clone());

        let receivers: Vec<Box<dyn MessageReceiver>> = display
            .registry()
            .lock()
            .globals
            .iter_mut()
            .map(|g| g.bind(0, 0, &mut client).unwrap())
            .collect();
        for (id, r) in receivers.into_iter().enumerate() {
            client.add_object_raw(id as u32, r, &TestInterface::REQUESTS)?;
        }
        assert_eq!(1, counts.lock().interface_1_bind_count);
        assert_eq!(1, counts.lock().interface_2_bind_count);
        assert_eq!(0, client.get_object::<TestReceiver>(0)?.count());
        assert_eq!(0, client.get_object::<TestReceiver>(1)?.count());

        // Dispatch a message to each receiver.
        client.handle_message(TestMessage::Message1.into_message(0)?)?;
        client.handle_message(TestMessage::Message1.into_message(1)?)?;

        assert_eq!(1, counts.lock().interface_1_bind_count);
        assert_eq!(1, counts.lock().interface_2_bind_count);
        assert_eq!(1, client.get_object::<TestReceiver>(0)?.count());
        assert_eq!(1, client.get_object::<TestReceiver>(1)?.count());
        Ok(())
    }
}
