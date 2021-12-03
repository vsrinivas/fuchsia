// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! This module provides types for managing the set of wayland objects for a
///! connection. The |ObjectMap| associates a numeric object id with a
///! |MessageReceiver| that can interpret the message and provide the logic to
///! implement the interface contract for that object.
///!
///! At a high level, the |RequestReceiver<I:Interface>| trait allows for a
///! decoded request to be interacted with. In the middle we provide the
///! |RequestDispatcher| struct that implements the |MessageReceiver| trait
///! by decoding the |Message| into the concrete |Request| type for the
///! |Interface|.
///!
///! Consumers should mostly have to only concern themselves with the
///! |RequestReceiver<I:Interface>| trait, with the other types being mostly the
///! glue and dispatch logic.
use {
    crate::client::Client,
    anyhow::{format_err, Error},
    fuchsia_trace as ftrace,
    fuchsia_wayland_core::{self as wl, FromArgs, MessageType},
    std::{
        any::Any,
        collections::{
            hash_map::{Entry, HashMap},
            HashSet,
        },
        fmt::{self, Debug},
        marker::PhantomData,
    },
    thiserror::Error,
};

/// The |ObjectMap| holds the state of active objects for a single connection.
///
/// When a new connection is established, the server should populate the
/// |ObjectMap| with the "wl_display" singleton object. From the client can
/// query the registry and bind new objects to the interfaces the client
/// understands.
pub(crate) struct ObjectMap {
    /// The set of active objects. This holds the descriptors of supported
    /// messages for each object, as well as the |MessageReceiver| that's
    /// capable of handling the requests.
    objects: HashMap<u32, ObjectMapEntry>,
}

struct ObjectMapEntry {
    /// The opcodes and method signatures accepted by this object.
    request_spec: &'static wl::MessageGroupSpec,
    /// The handler for this object.
    receiver: Box<dyn MessageReceiver>,
}

#[derive(Copy, Clone, Debug, Error)]
pub enum ObjectMapError {
    /// An error raised if a message is received with the 'sender' field set
    /// to an unknown object (ex: either the object has not been created yet
    /// or it has already been deleted).
    #[error("Object with id {} is not found", _0)]
    InvalidObjectId(u32),

    /// An error raised if a message is delivered to an object with an
    /// unsupported or unknown opcode.
    #[error("Opcode {} is not supported", _0)]
    InvalidOpcode(u16),
}

/// Errors generated when looking up objects from the map.
#[derive(Debug, Error)]
pub enum ObjectLookupError {
    #[error("Object with id {} does not exist", _0)]
    ObjectDoesNotExist(u32),
    #[error("Failed to downcast")]
    DowncastFailed,
}

impl ObjectMap {
    pub fn new() -> Self {
        ObjectMap { objects: HashMap::new() }
    }

    /// Looks up an object in the map and returns a downcasted reference to
    /// the implementation.
    pub fn get<T: Any>(&self, id: wl::ObjectId) -> Result<&T, ObjectLookupError> {
        ftrace::duration!("wayland", "ObjectMap::get");
        match self.objects.get(&id) {
            Some(entry) => match entry.receiver.data().downcast_ref() {
                Some(t) => Ok(t),
                None => Err(ObjectLookupError::DowncastFailed),
            },
            None => Err(ObjectLookupError::ObjectDoesNotExist(id)),
        }
    }

    /// Looks up an object in the map and returns a downcasted mutable
    /// reference to the implementation.
    pub fn get_mut<T: Any>(&mut self, id: wl::ObjectId) -> Result<&mut T, ObjectLookupError> {
        ftrace::duration!("wayland", "ObjectMap::get_mut");
        match self.objects.get_mut(&id) {
            Some(entry) => match entry.receiver.data_mut().downcast_mut() {
                Some(t) => Ok(t),
                None => Err(ObjectLookupError::DowncastFailed),
            },
            None => Err(ObjectLookupError::ObjectDoesNotExist(id)),
        }
    }

    /// Looks up the receiver function and the message structure from the map.
    pub(crate) fn lookup_internal(
        &self,
        header: &wl::MessageHeader,
    ) -> Result<(MessageReceiverFn, &'static wl::MessageSpec), Error> {
        ftrace::duration!("wayland", "ObjectMap::lookup_internal");
        let ObjectMapEntry { request_spec, receiver } = self
            .objects
            .get(&header.sender)
            .ok_or(ObjectMapError::InvalidObjectId(header.sender))?;
        let spec = request_spec
            .0
            .get(header.opcode as usize)
            .ok_or(ObjectMapError::InvalidOpcode(header.opcode))?;

        Ok((receiver.receiver(), spec))
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
        ftrace::duration!("wayland", "ObjectMap::add_object");
        self.add_object_raw(id, Box::new(RequestDispatcher::new(receiver)), &I::REQUESTS)?;
        Ok(ObjectRef::from_id(id))
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
        ftrace::duration!("wayland", "ObjectMap::add_object_raw");
        if let Entry::Vacant(entry) = self.objects.entry(id) {
            entry.insert(ObjectMapEntry { receiver, request_spec });
            Ok(())
        } else {
            Err(format_err!("Can't add duplicate object with id {}. ", id))
        }
    }

    pub fn delete(&mut self, id: wl::ObjectId) -> Result<(), Error> {
        ftrace::duration!("wayland", "ObjectMap::delete");
        if self.objects.remove(&id).is_some() {
            // TODO: Send wl_display::delete_id.
            Ok(())
        } else {
            Err(format_err!("Item {} does not exist", id))
        }
    }
}

/// When the concrete type of an object is known statically, we can provide
/// an ObjectRef wrapper around the ObjectId in order to make downcasting
/// simpler.
///
/// This is primarily useful when vending self references to MessageReceviers.
pub struct ObjectRef<T: 'static>(PhantomData<T>, wl::ObjectId);

// We cannot just derive these since that will place the corresponding trait
// bound on `T`.
impl<T: 'static> Copy for ObjectRef<T> {}
impl<T: 'static> Clone for ObjectRef<T> {
    fn clone(&self) -> Self {
        ObjectRef(PhantomData, self.1)
    }
}
impl<T: 'static> Debug for ObjectRef<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        write!(f, "ObjectRef({})", self.1)
    }
}
impl<T> From<wl::ObjectId> for ObjectRef<T> {
    fn from(id: wl::ObjectId) -> Self {
        Self::from_id(id)
    }
}
impl<T> Eq for ObjectRef<T> {}
impl<T> PartialEq for ObjectRef<T> {
    fn eq(&self, other: &Self) -> bool {
        self.1.eq(&other.1)
    }
}
impl<T> std::hash::Hash for ObjectRef<T> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.1.hash(state);
    }
}

impl<T> ObjectRef<T> {
    pub fn from_id(id: wl::ObjectId) -> Self {
        ObjectRef(PhantomData, id)
    }

    pub fn id(&self) -> wl::ObjectId {
        self.1
    }

    /// Provides an immutable reference to an object, downcasted to |T|.
    pub fn get<'a>(&self, client: &'a Client) -> Result<&'a T, ObjectLookupError> {
        client.get_object(self.1)
    }

    /// Provides a mutable reference to an object, downcasted to |T|.
    pub fn get_mut<'a>(&self, client: &'a mut Client) -> Result<&'a mut T, ObjectLookupError> {
        client.get_object_mut(self.1)
    }

    /// Returns `true` iff the underlying object is still valid.
    ///
    /// Here 'valid' means that the object_id exists and refers to an object
    /// of type `T`. This method will still return `true` if the associated
    /// object_id has been deleted and recreated with the same type `T. If this
    /// is not desirable then the caller must track instance with some state
    /// embedded into `T`.
    ///
    /// This can be useful to verify prior to sending events using the
    /// associated object_id but the host object itself is not required.
    pub fn is_valid(&self, client: &mut Client) -> bool {
        self.get(client).is_ok()
    }
}

/// A collection of `ObjectRef`s.
pub struct ObjectRefSet<ObjectType: 'static>(HashSet<ObjectRef<ObjectType>>);

impl<ObjectType> ObjectRefSet<ObjectType> {
    pub fn new() -> Self {
        ObjectRefSet(HashSet::new())
    }

    /// Adds `id` into the set.
    ///
    /// Returns `true` iff the item was inserted. If `false` is returned then an
    /// entry with the same id already exists in the set.
    pub fn add(&mut self, id: ObjectRef<ObjectType>) -> bool {
        self.0.insert(id)
    }

    /// Removes`id` from the set.
    ///
    /// Returns `true` iff the item was removed. If `false` is returned then an
    /// entry with a matching `id` did not exist in the set.
    pub fn remove(&mut self, id: ObjectRef<ObjectType>) -> bool {
        self.0.remove(&id)
    }

    pub fn iter(&self) -> impl Iterator<Item = &ObjectRef<ObjectType>> {
        self.0.iter()
    }
}

pub trait NewObjectExt<I: wl::Interface> {
    fn implement<R: RequestReceiver<I>>(
        self,
        client: &mut Client,
        receiver: R,
    ) -> Result<ObjectRef<R>, Error>;
}

impl<I: wl::Interface> NewObjectExt<I> for wl::NewObject<I> {
    fn implement<R: RequestReceiver<I>>(
        self,
        client: &mut Client,
        receiver: R,
    ) -> Result<ObjectRef<R>, Error> {
        client.add_object(self.id(), receiver)
    }
}

/// A |MessageReceiver| is a type that can accept in-bound messages from a
/// client.
///
/// The server will dispatch |Message|s to the appropriate |MessageReceiver|
/// by reading the sender field in the message header.
type MessageReceiverFn = fn(
    this: wl::ObjectId,
    opcode: u16,
    args: Vec<wl::Arg>,
    client: &mut Client,
) -> Result<(), Error>;
pub trait MessageReceiver {
    /// Returns a function pointer that will be called to handle requests
    /// targeting this object.
    fn receiver(&self) -> MessageReceiverFn;

    fn data(&self) -> &dyn Any;

    fn data_mut(&mut self) -> &mut dyn Any;
}

/// The |RequestReceiver| trait is what high level code will use to work with
/// request messages for a given type.
pub trait RequestReceiver<I: wl::Interface>: Any + Sized {
    /// Handle a decoded message for the associated |Interface|.
    ///
    /// |self| is not directly provided, but instead is provided as an
    /// |ObjectId| that can be used to get a reference to self.
    ///
    /// Ex:
    ///   struct MyReceiver;
    ///
    ///   impl RequestReceiver<MyInterface> for MyReceiver {
    ///       fn receive(mut this: ObjectRef<Self>,
    ///                  request: MyInterfaceRequest,
    ///                  client: &mut Client
    ///       ) -> Result<(), Error> {
    ///           let this = self.get()?;
    ///           let this_mut = self.get_mut()?;
    ///       }
    ///   }
    fn receive(
        this: ObjectRef<Self>,
        request: I::Request,
        client: &mut Client,
    ) -> Result<(), Error>;
}

/// Implements a |MessageReceiver| that can decode a request into the
/// appropriate request type for an |Interface|, and then invoke an
/// |Implementation|
///
/// This struct essentially is the glue that sits in between the generic
/// |MessageReceiver| trait that is used to dispatch raw message buffers and
/// the higher level |RequestReceiver| that operates on the decoded request
/// enums.
pub struct RequestDispatcher<I: wl::Interface, R: RequestReceiver<I>> {
    _marker: PhantomData<I>,
    receiver: R,
}

impl<I: wl::Interface, R: RequestReceiver<I>> RequestDispatcher<I, R> {
    pub fn new(receiver: R) -> Self {
        RequestDispatcher { receiver, _marker: PhantomData }
    }
}

fn receive_message<I: wl::Interface, R: RequestReceiver<I>>(
    this: wl::ObjectId,
    opcode: u16,
    args: Vec<wl::Arg>,
    client: &mut Client,
) -> Result<(), Error> {
    ftrace::duration!("wayland", "receive_message");
    let request = {
        ftrace::duration!("wayland", "I::Request::from_args");
        I::Request::from_args(opcode, args).unwrap()
    };
    if client.protocol_logging() {
        println!("--r-> {}", request.log(this));
    }

    {
        let _scope = ftrace::duration(ftrace::cstr!("wayland"), request.message_name(), &[]);
        R::receive(ObjectRef(PhantomData, this), request, client)?;
    }
    Ok(())
}

/// Convert the raw Message into the appropriate request type by delegating
/// to the associated |Request| type of |Interface|, and then invoke the
/// receiver.
impl<I: wl::Interface, R: RequestReceiver<I>> MessageReceiver for RequestDispatcher<I, R> {
    fn receiver(&self) -> MessageReceiverFn {
        receive_message::<I, R>
    }

    fn data(&self) -> &dyn Any {
        &self.receiver
    }

    fn data_mut(&mut self) -> &mut dyn Any {
        &mut self.receiver
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async as fasync;
    use fuchsia_wayland_core::IntoMessage;
    use fuchsia_zircon as zx;

    use crate::display::Display;
    use crate::registry::RegistryBuilder;
    use crate::test_protocol::*;

    fn create_client() -> Result<Client, Error> {
        let display = Display::new_no_scenic(RegistryBuilder::new().build())
            .expect("Failed to create display");
        let (c1, _c2) = zx::Channel::create()?;
        Ok(Client::new(fasync::Channel::from_channel(c1)?, display))
    }

    #[test]
    fn dispatch_message_to_request_receiver() -> Result<(), Error> {
        let _executor = fasync::LocalExecutor::new();
        let mut client = create_client()?;
        client.add_object(0, TestReceiver::new())?;

        // Send a sync message; verify it's received.
        client.handle_message(TestMessage::Message1.into_message(0)?)?;
        assert_eq!(1, client.get_object::<TestReceiver>(0)?.count());
        Ok(())
    }

    #[test]
    fn add_object_duplicate_id() -> Result<(), Error> {
        let _executor = fasync::LocalExecutor::new();
        let mut client = create_client()?;
        assert!(client.add_object(0, TestReceiver::new()).is_ok());
        assert!(client.add_object(0, TestReceiver::new()).is_err());
        Ok(())
    }

    #[test]
    fn dispatch_message_to_invalid_id() -> Result<(), Error> {
        let _executor = fasync::LocalExecutor::new();
        // Send a message to an empty map.
        let mut client = create_client()?;

        assert!(client.handle_message(TestMessage::Message1.into_message(0)?).is_err());
        Ok(())
    }
}
