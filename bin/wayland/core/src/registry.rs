// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem;

use crate::MessageReceiver;

/// Helper for constructing a |Registry|.
///
/// A proper registry implementation can support adding and removing |Global|s
/// at runtime. Since we do not yet support these features, we will only allow
/// |Global|s to be added at initialization time and then leave the |Registry|
/// immutatble.
///
/// Note: the |Registry| only works for single-threaded event loops.
///
/// TODO(tjdetwiler): Allow interfaces to be added and removed from the
/// |Registry| dynamically.
pub struct RegistryBuilder {
    globals: Vec<Box<dyn Global + 'static>>,
}

impl RegistryBuilder {
    pub fn new() -> Self {
        RegistryBuilder {
            globals: Vec::new(),
        }
    }

    /// Adds a global to the registry.
    ///
    /// Note we don't implement proper runtime management
    pub fn add_global<T: Global + 'static>(&mut self, global: T) -> &mut Self {
        self.globals.push(Box::new(global));
        self
    }

    pub fn build(&mut self) -> Registry {
        Registry {
            globals: mem::replace(&mut self.globals, vec![]),
        }
    }
}

/// The |Registry| holds the global interfaces that are advertised to clients.
pub struct Registry {
    globals: Vec<Box<dyn Global + 'static>>,
}

impl Registry {
    pub fn globals(&mut self) -> &mut [Box<dyn Global + 'static>] {
        self.globals.as_mut_slice()
    }
}

/// |Global| advertises availability of a bindable interface to clients.
pub trait Global: Send {
    /// The interface name of this global. This would correspond to the
    /// 'name' attribute on the 'interface' element in the protocol XML.
    fn interface(&self) -> &str;

    /// Binds this global to a new object that client can then interact with.
    ///
    /// A client will associate an object id with the bound object. The
    /// returned |MessageReceiver| will be used to handle in-bound requests with
    /// the sender field identifying the bound object.
    fn bind(&mut self) -> Box<MessageReceiver>;
}

/// Allow implementing a global with a tuple of name & bind function.
///
/// Ex:
///     registry_builder.add_global(("my_interface", || {
///         println!("Client bound to interface!");
///         |msg| -> Result<(), Error> {
///             println!("received a message!");
///             Ok(())
///         }
///     }));
impl<F> Global for (&'static str, F)
where
    F: FnMut() -> Box<MessageReceiver> + Send,
{
    fn interface(&self) -> &str {
        self.0
    }

    fn bind(&mut self) -> Box<MessageReceiver> {
        self.1()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::sync::Arc;

    use failure::Error;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use parking_lot::Mutex;

    use crate::test_protocol::*;
    use crate::{Client, Interface, IntoMessage, RequestDispatcher};

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
            registry.add_global(("interface_1", move || -> Box<MessageReceiver> {
                counts.lock().interface_1_bind_count += 1;
                Box::new(RequestDispatcher::new(TestReceiver::new()))
            }));
        }
        {
            let counts = counts.clone();
            registry.add_global(("interface_2", move || -> Box<MessageReceiver> {
                counts.lock().interface_2_bind_count += 1;
                Box::new(RequestDispatcher::new(TestReceiver::new()))
            }));
        }

        // Build the registry & verify initial counts.
        let mut registry = registry.build();
        assert_eq!(0, counts.lock().interface_1_bind_count);
        assert_eq!(0, counts.lock().interface_2_bind_count);

        // Bind to the globals.
        let mut receivers: Vec<Box<MessageReceiver>> =
            registry.globals.iter_mut().map(|g| g.bind()).collect();
        let (c1, _c2) = zx::Channel::create()?;
        let _executor = fasync::Executor::new();
        let mut client = Client::new(fasync::Channel::from_channel(c1)?, Arc::new(Mutex::new(registry)));
        for (id, r) in receivers.into_iter().enumerate() {
            client.objects().add_object_raw(id as u32, r, &TestInterface::REQUESTS)?;
        }
        assert_eq!(1, counts.lock().interface_1_bind_count);
        assert_eq!(1, counts.lock().interface_2_bind_count);
        assert_eq!(0, client.objects().get::<TestReceiver>(0)?.count());
        assert_eq!(0, client.objects().get::<TestReceiver>(1)?.count());

        // Dispatch a message to each receiver.
        client.receive_message(TestMessage::Message1.into_message(0)?)?;
        client.receive_message(TestMessage::Message1.into_message(1)?)?;

        assert_eq!(1, counts.lock().interface_1_bind_count);
        assert_eq!(1, counts.lock().interface_2_bind_count);
        assert_eq!(1, client.objects().get::<TestReceiver>(0)?.count());
        assert_eq!(1, client.objects().get::<TestReceiver>(1)?.count());
        Ok(())
    }
}
