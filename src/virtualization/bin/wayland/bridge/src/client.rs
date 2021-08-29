// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::display::{Display, DISPLAY_SINGLETON_OBJECT_ID},
    crate::object::{
        MessageReceiver, ObjectLookupError, ObjectMap, ObjectRef, ObjectRefSet, RequestReceiver,
    },
    crate::seat::InputDispatcher,
    crate::xdg_shell::XdgToplevel,
    anyhow::Error,
    fuchsia_async as fasync, fuchsia_trace as ftrace, fuchsia_wayland_core as wl,
    fuchsia_zircon as zx,
    futures::channel::mpsc,
    futures::prelude::*,
    futures::select,
    std::{any::Any, cell::Cell, rc::Rc},
    wayland::WlDisplayEvent,
};

type Task = Box<dyn FnMut(&mut Client) -> Result<(), Error> + 'static>;

/// The state of a single client connection. Each client connection will have
/// have its own zircon channel and its own set of protocol objects. The
/// |Display| is the only piece of global state that is shared between
/// clients.
pub struct Client {
    /// The zircon channel used to communicate with this client.
    chan: Rc<fasync::Channel>,

    /// The display for this client.
    display: Display,

    /// The set of objects for this client.
    objects: ObjectMap,

    /// An incoming task queue of closures to be invoked on the client. These
    /// closures will be invoked with a mutable reference to the `Client`,
    /// providing a way for background tasks to access client resources.
    tasks: mpsc::UnboundedReceiver<Task>,

    /// The sending endpoint for the task channel.
    task_queue: TaskQueue,

    /// The sending endpoint for protocol events.
    event_queue: EventQueue,

    /// If `true`, all requests and events will be logged.
    protocol_logging: Rc<Cell<bool>>,

    /// Decode and dispatch Scenic input events.
    pub input_dispatcher: InputDispatcher,

    pub xdg_toplevels: ObjectRefSet<XdgToplevel>,
}

impl Client {
    /// Creates a new client.
    pub fn new(chan: fasync::Channel, display: Display) -> Self {
        let (sender, receiver) = mpsc::unbounded();
        let log_flag = Rc::new(Cell::new(false));
        let chan = Rc::new(chan);
        let event_queue = EventQueue {
            chan: chan.clone(),
            log_flag: log_flag.clone(),
            next_serial: Rc::new(Cell::new(0)),
        };
        Client {
            display,
            chan,
            objects: ObjectMap::new(),
            tasks: receiver,
            task_queue: TaskQueue(sender),
            protocol_logging: log_flag,
            input_dispatcher: InputDispatcher::new(event_queue.clone()),
            event_queue,
            xdg_toplevels: ObjectRefSet::new(),
        }
    }

    /// Enables or disables protocol message logging.
    pub fn set_protocol_logging(&mut self, enabled: bool) {
        self.protocol_logging.set(enabled);
    }

    /// Returns `true` if protocol messages should be logged.
    pub(crate) fn protocol_logging(&self) -> bool {
        self.protocol_logging.get()
    }

    /// Returns a object that can post messages to the `Client`.
    pub fn task_queue(&self) -> TaskQueue {
        self.task_queue.clone()
    }

    /// Returns an object that can post events back to the client.
    pub fn event_queue(&self) -> &EventQueue {
        &self.event_queue
    }

    /// Spawns an async task that waits for messages to be received on the
    /// zircon channel, decodes the messages, and dispatches them to the
    /// corresponding |MessageReceiver|s.
    pub fn start(mut self) {
        fasync::Task::local(async move {
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
                            break;
                        }
                        // Dispatch the message.
                        if let Err(e) = self.handle_message(buffer.into()) {
                            println!("Failed to receive message on the channel {}", e);
                            break;
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
                        if let Err(e) = self.handle_task(task.expect("Task stream has unexpectedly closed.")) {
                            println!("Failed to run wayland task: {}", e);
                            break;
                        }
                    },
                }
            }
            // We need to shutdown the client. This includes tearning down
            // all views associated with this client.
            self.xdg_toplevels.iter().for_each(|toplevel| {
                if let Ok(t) = toplevel.get(&self) {
                    t.shutdown(&self);
                }
            });
        }).detach();
    }

    /// The `Display` for this client.
    pub fn display(&self) -> &Display {
        &self.display
    }

    /// Looks up an object in the map and returns a downcasted reference to
    /// the implementation.
    pub fn get_object<T: Any>(&self, id: wl::ObjectId) -> Result<&T, ObjectLookupError> {
        self.objects.get(id)
    }

    /// Looks up an object in the map and returns a downcasted mutable
    /// reference to the implementation.
    pub fn get_object_mut<T: Any>(
        &mut self,
        id: wl::ObjectId,
    ) -> Result<&mut T, ObjectLookupError> {
        self.objects.get_mut(id)
    }

    /// Adds a new object into the map that will handle messages with the sender
    /// set to |id|. When a message is received with the corresponding |id|, the
    /// message will be decoded and forwarded to the |RequestReceiver|.
    ///
    /// Returns Err if there is already an object for |id| in this |ObjectMap|.
    pub fn add_object<I: wl::Interface + 'static, R: RequestReceiver<I> + 'static>(
        &mut self,
        id: u32,
        receiver: R,
    ) -> Result<ObjectRef<R>, Error> {
        self.objects.add_object(id, receiver)
    }

    /// Adds an object to the map using the low-level primitives. It's favorable
    /// to use instead |add_object| if the wayland interface for the object is
    /// statically known.
    pub fn add_object_raw(
        &mut self,
        id: wl::ObjectId,
        receiver: Box<dyn MessageReceiver>,
        request_spec: &'static wl::MessageGroupSpec,
    ) -> Result<(), Error> {
        self.objects.add_object_raw(id, receiver, request_spec)
    }

    /// Deletes the object `id` from the local object map and send a notification to the
    /// client confirming that `id` can be reused.
    pub fn delete_id(&mut self, id: wl::ObjectId) -> Result<(), Error> {
        self.objects.delete(id)?;
        self.event_queue().post(DISPLAY_SINGLETON_OBJECT_ID, WlDisplayEvent::DeleteId { id })
    }

    /// Reads the message header to find the target for this message and then
    /// forwards the message to the associated |MessageReceiver|.
    ///
    /// Returns Err if no object is associated with the sender field in the
    /// message header, or if the objects receiver itself fails.
    pub(crate) fn handle_message(&mut self, mut message: wl::Message) -> Result<(), Error> {
        ftrace::duration!("wayland", "Client::handle_message");
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

    fn handle_task(&mut self, mut task: Task) -> Result<(), Error> {
        ftrace::duration!("wayland", "Client::handle_task");
        task(self)
    }
}

/// An `EventQueue` enables protocol events to be sent back to the client.
#[derive(Clone)]
pub struct EventQueue {
    chan: Rc<fasync::Channel>,
    log_flag: Rc<Cell<bool>>,
    next_serial: Rc<Cell<u32>>,
}

impl EventQueue {
    /// Serializes `event` and writes it to the client channel.
    ///
    /// The 'sender' will be embedded in the meesage header indicating what
    /// protocol object dispatched the event.
    pub fn post<E: wl::IntoMessage + std::marker::Send>(
        &self,
        sender: wl::ObjectId,
        event: E,
    ) -> Result<(), Error>
    where
        <E as wl::IntoMessage>::Error: std::marker::Send + 'static,
    {
        ftrace::duration!("wayland", "EventQueue::post");
        if self.log_flag.get() {
            println!("<-e-- {}", event.log(sender));
        }
        let message = Self::serialize(sender, event)?;
        self.write_to_chan(message)
    }

    fn serialize<E: wl::IntoMessage>(sender: wl::ObjectId, event: E) -> Result<wl::Message, Error>
    where
        <E as wl::IntoMessage>::Error: std::marker::Send + 'static,
    {
        ftrace::duration!("wayland", "EventQueue::serialize");
        Ok(event.into_message(sender).unwrap())
    }

    fn write_to_chan(&self, message: wl::Message) -> Result<(), Error> {
        ftrace::duration!("wayland", "EventQueue::write_to_chan");
        let (bytes, mut handles) = message.take();
        self.chan.write(&bytes, &mut handles)?;
        Ok(())
    }

    /// Returns a monotonically increasing value. Many protocol events rely
    /// on an event serial number, which can be obtained with this method.
    pub fn next_serial(&self) -> u32 {
        let serial = self.next_serial.get();
        self.next_serial.set(serial + 1);
        serial
    }
}

/// A `TaskQueue` enables asynchronous operations to post tasks back to the
/// `Client`.
///
/// Ex:
///   let foo: ObjectRef<Foo> = get_foo_ref();
///   let tasks = client.task_queue();
///   task.post(|client| {
///       let foo = foo.get(client);
///       foo.handle_delayed_operation();
///   });
#[derive(Clone)]
pub struct TaskQueue(mpsc::UnboundedSender<Task>);

impl TaskQueue {
    /// Posts the closure to be run as soon as possible.
    pub fn post<F>(&self, f: F)
    where
        F: FnMut(&mut Client) -> Result<(), Error> + 'static,
    {
        // Failure here means the client is shutting down and we don't want to
        // accept any more tasks.
        let _result = self.0.unbounded_send(Box::new(f));
    }
}
