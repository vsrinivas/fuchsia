// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! When generating a watcher event, one needs "a list of names" that are then converted into
//! buffers sent to the watchers.  In a sense, an iterator over a list of strings would work, but
//! in order to avoid copying the data around, this namespace provides a more specialized version
//! of this abstraction.

use {fidl_fuchsia_io as fio, static_assertions::assert_eq_size};

/// Watcher event producer, that generates buffers filled with watcher events.  Watchers use this
/// API to obtain buffers that are then sent to the actual watchers.  Every producer may generate
/// multiple events, but they all need to be of the same type, as returned by [`Self::event()`] and
/// [`Self::mask()`] methods.
pub trait EventProducer {
    /// Returns a mask that represents the type of events this producer can generate, as one of the
    /// `fidl_fuchsia_io::WatchMask::*` constants.  There might be only one bit set and it should
    /// correspond to the event returned by the [`Self::event()`] method.  It is a duplication, but it
    /// helps the callers that need both masks and event IDs.
    fn mask(&self) -> fio::WatchMask;

    /// Returns an event ID this event producer will use to populate the buffers, as one of the
    /// `fidl_fuchsia_io::WatchEvent::*` constants.  Must match what [`Self::mask()`], returns, see
    /// there for details.
    fn event(&self) -> fio::WatchEvent;

    /// Checks if this producer can create another buffer, returning `true` if it can.  This method
    /// does not actually need to construct the buffer just yet, as an optimization if it will not
    /// be needed.
    fn prepare_for_next_buffer(&mut self) -> bool;

    /// Returns a copy of the current buffer prepared by this producer.  This method will be the
    /// one constructing a buffer, if necessary, after a preceding call to
    /// [`Self::prepare_for_next_buffer()`].
    ///
    /// Note that this method will keep returning copies of the same buffer, until
    /// [`Self::prepare_for_next_buffer()`] is not called explicitly.
    fn buffer(&mut self) -> Vec<u8>;
}

/// Common mechanism used by both [`StaticVecEventProducer`] and, later, [`SinkEventProducer`].
struct CachingEventProducer {
    mask: fio::WatchMask,
    event: fio::WatchEvent,
    current_buffer: Option<Vec<u8>>,
}

impl CachingEventProducer {
    fn new(mask: fio::WatchMask, event: fio::WatchEvent) -> Self {
        CachingEventProducer { mask, event, current_buffer: None }
    }

    fn mask(&self) -> fio::WatchMask {
        self.mask
    }

    fn event(&self) -> fio::WatchEvent {
        self.event
    }

    fn prepare_for_next_buffer(&mut self) {
        self.current_buffer = None;
    }

    /// Users of [`CachingEventProducer`] should use this method to implement
    /// [`EventProducer::buffer`].  `fill_buffer` is a callback used to populate the buffer when
    /// necessary.  It's 'u8' argument is the event ID used by this producer.
    fn buffer<FillBuffer>(&mut self, fill_buffer: FillBuffer) -> Vec<u8>
    where
        FillBuffer: FnOnce(fio::WatchEvent) -> Vec<u8>,
    {
        match &self.current_buffer {
            Some(buf) => buf.clone(),
            None => {
                let buf = fill_buffer(self.event);
                self.current_buffer = Some(buf.clone());
                buf
            }
        }
    }
}

/// An [`EventProducer`] that uses a `Vec<String>` with names of the entires to be put into the
/// watcher event.
pub struct StaticVecEventProducer {
    cache: CachingEventProducer,
    names: Vec<String>,
    next: usize,
}

impl StaticVecEventProducer {
    /// Constructs a new [`EventProducer`] that is producing names form the specified list,
    /// building events of type `WatchEvent::Added`.  `names` is not allowed to be empty.
    pub fn added(names: Vec<String>) -> Self {
        Self::new(fio::WatchMask::ADDED, fio::WatchEvent::Added, names)
    }

    /// Constructs a new [`EventProducer`] that is producing names form the specified list,
    /// building events of type `WatchEvent::Removed`.  `names` is not allowed to be empty.
    pub fn removed(names: Vec<String>) -> Self {
        Self::new(fio::WatchMask::REMOVED, fio::WatchEvent::Removed, names)
    }

    /// Constructs a new [`EventProducer`] that is producing names form the specified list,
    /// building events of type `WatchEvent::Existing`.  `names` is not allowed to be empty.
    pub fn existing(names: Vec<String>) -> Self {
        Self::new(fio::WatchMask::EXISTING, fio::WatchEvent::Existing, names)
    }

    fn new(mask: fio::WatchMask, event: fio::WatchEvent, names: Vec<String>) -> Self {
        debug_assert!(!names.is_empty());
        Self { cache: CachingEventProducer::new(mask, event), names, next: 0 }
    }

    // Can not use `&mut self` here as it would "lock" the whole object disallowing the
    // `self.cache.buffer()` call where we want to pass this method in a closure.
    fn fill_buffer(event: fio::WatchEvent, next: &mut usize, names: &mut Vec<String>) -> Vec<u8> {
        let mut buffer = vec![];

        while *next < names.len() {
            if !encode_name(&mut buffer, event, &names[*next]) {
                break;
            }
            *next += 1;
        }

        buffer
    }
}

impl EventProducer for StaticVecEventProducer {
    fn mask(&self) -> fio::WatchMask {
        self.cache.mask()
    }

    fn event(&self) -> fio::WatchEvent {
        self.cache.event()
    }

    fn prepare_for_next_buffer(&mut self) -> bool {
        self.cache.prepare_for_next_buffer();
        self.next < self.names.len()
    }

    fn buffer(&mut self) -> Vec<u8> {
        let cache = &mut self.cache;
        let next = &mut self.next;
        let names = &mut self.names;
        cache.buffer(|event| Self::fill_buffer(event, next, names))
    }
}

/// An event producer for an event containing only one name.  It is slightly optimized, but
/// otherwise functionally equivalent to the [`StaticVecEventProducer`] with an array of one
/// element.
pub struct SingleNameEventProducer {
    producer: SingleBufferEventProducer,
}

impl SingleNameEventProducer {
    /// Constructs a new [`SingleNameEventProducer`] that will produce an event for one name of
    /// type `WatchEvent::Deleted`. Deleted refers to the directory the watcher itself is on, and
    /// therefore statically refers to itself as ".".
    pub fn deleted() -> Self {
        Self::new(fio::WatchMask::DELETED, fio::WatchEvent::Deleted, ".")
    }

    /// Constructs a new [`SingleNameEventProducer`] that will produce an event for one name of
    /// type `WatchEvent::Added`.
    pub fn added(name: &str) -> Self {
        Self::new(fio::WatchMask::ADDED, fio::WatchEvent::Added, name)
    }

    /// Constructs a new [`SingleNameEventProducer`] that will produce an event for one name of
    /// type `WatchEvent::Removed`.
    pub fn removed(name: &str) -> Self {
        Self::new(fio::WatchMask::REMOVED, fio::WatchEvent::Removed, name)
    }

    /// Constructs a new [`SingleNameEventProducer`] that will produce an event for one name of
    /// type `WatchEvent::Existing`.
    pub fn existing(name: &str) -> Self {
        Self::new(fio::WatchMask::EXISTING, fio::WatchEvent::Existing, name)
    }

    /// Constructs a new [`SingleNameEventProducer`] that will produce an `WatchEvent::Idle` event.
    pub fn idle() -> Self {
        Self::new(fio::WatchMask::IDLE, fio::WatchEvent::Idle, "")
    }

    fn new(mask: fio::WatchMask, event: fio::WatchEvent, name: &str) -> Self {
        let mut buffer = vec![];
        encode_name(&mut buffer, event, name);

        Self { producer: SingleBufferEventProducer::new(mask, event, buffer) }
    }
}

impl EventProducer for SingleNameEventProducer {
    fn mask(&self) -> fio::WatchMask {
        self.producer.mask()
    }

    fn event(&self) -> fio::WatchEvent {
        self.producer.event()
    }

    fn prepare_for_next_buffer(&mut self) -> bool {
        self.producer.prepare_for_next_buffer()
    }

    fn buffer(&mut self) -> Vec<u8> {
        self.producer.buffer()
    }
}

pub(crate) fn encode_name(buffer: &mut Vec<u8>, event: fio::WatchEvent, name: &str) -> bool {
    if buffer.len() + (2 + name.len()) > fio::MAX_BUF as usize {
        return false;
    }

    // We are going to encode the file name length as u8.
    debug_assert!(u8::max_value() as u64 >= fio::MAX_FILENAME);

    buffer.push(event.into_primitive());
    buffer.push(name.len() as u8);
    buffer.extend_from_slice(name.as_bytes());
    true
}

enum SingleBufferEventProducerState {
    Start,
    FirstEvent,
    Done,
}

/// An event producer for an event that has one buffer of data.
pub struct SingleBufferEventProducer {
    mask: fio::WatchMask,
    event: fio::WatchEvent,
    buffer: Vec<u8>,
    state: SingleBufferEventProducerState,
}

impl SingleBufferEventProducer {
    /// Constructs a new [`SingleBufferEventProducer`] that will produce an event for one name of
    /// type `WatchEvent::Existing`.
    pub fn existing(buffer: Vec<u8>) -> Self {
        assert_eq_size!(usize, u64);
        debug_assert!(buffer.len() as u64 <= fio::MAX_BUF);
        Self::new(fio::WatchMask::EXISTING, fio::WatchEvent::Existing, buffer)
    }

    fn new(mask: fio::WatchMask, event: fio::WatchEvent, buffer: Vec<u8>) -> Self {
        assert_eq_size!(usize, u64);
        debug_assert!(buffer.len() as u64 <= fio::MAX_BUF);
        Self { mask, event, buffer, state: SingleBufferEventProducerState::Start }
    }
}

impl EventProducer for SingleBufferEventProducer {
    fn mask(&self) -> fio::WatchMask {
        self.mask
    }

    fn event(&self) -> fio::WatchEvent {
        self.event
    }

    fn prepare_for_next_buffer(&mut self) -> bool {
        match self.state {
            SingleBufferEventProducerState::Start => {
                self.state = SingleBufferEventProducerState::FirstEvent;
                true
            }
            SingleBufferEventProducerState::FirstEvent => {
                self.state = SingleBufferEventProducerState::Done;
                false
            }
            SingleBufferEventProducerState::Done => false,
        }
    }

    fn buffer(&mut self) -> Vec<u8> {
        self.buffer.clone()
    }
}
