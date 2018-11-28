// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::any::Any;
use std::sync::Arc;

use failure::Error;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::channel::mpsc;
use futures::prelude::*;
use futures::select;

use parking_lot::Mutex;

use crate::{Interface, IntoMessage, Message, MessageGroupSpec, MessageReceiver, ObjectId,
            ObjectLookupError, ObjectMap, ObjectRef, Registry, RequestReceiver};

type Task = Box<FnMut(&mut Client) -> Result<(), Error> + 'static>;
type ObjectDeleter = fn(&mut Client, ObjectId) -> Result<(), Error>;

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

    /// An incoming task queue of closures to be invoked on the client. These
    /// closures will be invoked with a mutable reference to the `Client`,
    /// providing a way for background tasks to access client resources.
    tasks: mpsc::UnboundedReceiver<Task>,

    /// The sending endpoint for the task channel.
    task_queue: TaskQueue,

    /// A function that will be called after objects have been deleted from
    /// the underlying `ObjectMap`. The purpose of this is to allow the
    /// application to send the wl_display::delete_id event
    object_deleter: Option<ObjectDeleter>,

    protocol_logging: bool,
}

impl Client {
    /// Creates a new client.
    pub fn new(chan: fasync::Channel, registry: Arc<Mutex<Registry>>) -> Self {
        let (sender, receiver) = mpsc::unbounded();
        Client {
            registry,
            chan: Arc::new(chan),
            objects: ObjectMap::new(),
            tasks: receiver,
            task_queue: TaskQueue(sender),
            object_deleter: None,
            protocol_logging: false,
        }
    }

    /// Enables or disables protocol message logging.
    pub fn set_protocol_logging(&mut self, enabled: bool) {
        self.protocol_logging = enabled;
    }

    /// Returns `true` if protocol messages should be logged.
    pub(crate) fn protocol_logging(&self) -> bool {
        self.protocol_logging
    }

    pub fn set_object_deleter(&mut self, deleter: ObjectDeleter) {
        self.object_deleter = Some(deleter);
    }

    /// Returns a object that can post messages to the `Client`.
    pub fn task_queue(&self) -> TaskQueue {
        self.task_queue.clone()
    }

    /// Spawns an async task that waits for messages to be received on the
    /// zircon channel, decodes the messages, and dispatches them to the
    /// corresponding |MessageReceiver|s.
    pub fn start(mut self) {
        fasync::spawn_local(
            async move {
                let mut buffer = zx::MessageBuf::new();
                loop {
                    select! {
                        // Fusing: we exit when `recv_msg` fails, so we don't
                        // need to worry about fast-looping when the channel is
                        // closed.
                        message = self.chan.recv_msg(&mut buffer).fuse() => {
                            // We got a new message over the zircon channel.
                            if let Err(e) = message {
                                println!("Failed to receive message on the channel {}", e);
                                return;
                            }
                            // Dispatch the message.
                            if let Err(e) = self.receive_message(buffer.into()) {
                                println!("Failed to receive message on the channel {}", e);
                                return;
                            }
                            buffer = zx::MessageBuf::new();
                        },
                        // Fusing: we panic immediately if the task queue ever returns
                        // `None`, so no need to track state of the channel between
                        // loop iterations. NOTE: for this to remain true, no other code
                        // can be given access to mutate `self.tasks`.
                        task = self.tasks.next().fuse() => {
                            // A new closure has been received.
                            //
                            // We unwrap since we retain a reference to the
                            // sending endpoint of the channel, preventing it
                            // from closing.
                            let mut task = task.expect("Task stream has unexpectedly closed.");
                            if let Err(e) = task(&mut self) {
                                println!("Failed to run wayland task: {}", e);
                                return;
                            }
                        },
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

    /// Looks up an object in the map and returns a downcasted reference to
    /// the implementation.
    pub fn get_object<T: Any>(&self, id: ObjectId) -> Result<&T, ObjectLookupError> {
        self.objects.get(id)
    }

    /// Looks up an object in the map and returns a downcasted mutable
    /// reference to the implementation.
    pub fn get_object_mut<T: Any>(&mut self, id: ObjectId) -> Result<&mut T, ObjectLookupError> {
        self.objects.get_mut(id)
    }

    /// Adds a new object into the map that will handle messages with the sender
    /// set to |id|. When a message is received with the corresponding |id|, the
    /// message will be decoded and forwarded to the |RequestReceiver|.
    ///
    /// Returns Err if there is already an object for |id| in this |ObjectMap|.
    pub fn add_object<I: Interface + 'static, R: RequestReceiver<I> + 'static>(
        &mut self, id: u32, receiver: R,
    ) -> Result<ObjectRef<R>, Error> {
        self.objects.add_object(id, receiver)
    }

    /// Adds an object to the map using the low-level primitives. It's favorable
    /// to use instead |add_object| if the wayland interface for the object is
    /// statically known.
    pub fn add_object_raw(
        &mut self, id: ObjectId, receiver: Box<MessageReceiver>,
        request_spec: &'static MessageGroupSpec,
    ) -> Result<(), Error> {
        self.objects.add_object_raw(id, receiver, request_spec)
    }

    pub fn delete_id(&mut self, id: ObjectId) -> Result<(), Error> {
        self.objects.delete(id)?;
        self.object_deleter.map_or(Ok(()), |f| f(self, id))
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
        if self.protocol_logging() {
            println!("<-e-- {}", event.log(sender));
        }
        let message = event.into_message(sender)?;
        let (bytes, mut handles) = message.take();
        self.chan.write(&bytes, &mut handles)?;
        Ok(())
    }
}

/// A `TaskQueue` enables asyncronous operations to post tasks back to the
/// `Client`.
///
/// Ex:
///   let foo: ObjectRef<Foo> = get_foo_ref();
///   let tasks = client.task_queue();
///   task.post_at(zx::Time::after(100.millis()), |client| {
///       let foo = foo.get(client);
///       foo.handle_delayed_operation();
///   });
#[derive(Clone)]
pub struct TaskQueue(mpsc::UnboundedSender<Task>);

impl TaskQueue {
    /// Posts the closure to be run as soon as possilbe.
    pub fn post<F>(&self, f: F)
    where
        F: FnMut(&mut Client) -> Result<(), Error> + 'static,
    {
        self.0
            .unbounded_send(Box::new(f))
            .expect("failed to send task");
    }

    /// Posts the task to be run after the provided deadline.
    pub fn post_at<F>(&self, deadline: zx::Time, f: F)
    where
        F: FnMut(&mut Client) -> Result<(), Error> + 'static,
    {
        let tx = self.0.clone();
        fasync::spawn_local(fasync::Timer::new(deadline).map(move |_| {
            tx.unbounded_send(Box::new(f))
                .expect("Failed to send closure");
        }));
    }
}
