// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types that help describe `get_entry_names` callback for the lazy directories.

use crate::directory::entry::EntryInfo;

use std::any::Any;

/// Every sink that can consume directory entries information implements this trait.
pub trait Sink<TraversalPosition>: Send
where
    TraversalPosition: Default + Send + 'static,
{
    /// Try to append an entry with the specified entry name and attributes into this sink.
    /// If the entry was successfully added, `pos` is not invoked and the result is
    /// [`AppendResult::Ok`].  If the sink could not consume this entry, `pos` is used to get
    /// current traversal position and an [`AppendResult::Sealed`] value is returned.
    ///
    /// `entry` is the [`EntryInfo`] attributes of the next entry.  `name` is the name of the next
    /// entry.  `pos` is a method that returns the position of the next entry when invoked.  `pos`
    /// is a method as this position is not needed unless the sink needs to be sealed, and
    /// constructing a position object might not be completely free.  So it is an optimization.
    fn append(
        self: Box<Self>,
        entry: &EntryInfo,
        name: &str,
        pos: &dyn Fn() -> TraversalPosition,
    ) -> AppendResult<TraversalPosition>;

    /// If the producer has reached the end of the list of entries, it should call this method to
    /// produce a "sealed" sink.
    fn seal(self: Box<Self>, pos: TraversalPosition) -> Box<dyn Sealed>;
}

/// When a sink has reached it's full capacity or when the producer has exhausted all the values it
/// had, sink is transformed into a value that implements this trait.  It helps to reduce the
/// number of possible states the sink has.  Once "sealed" it can not be converted back into a
/// [`Sink`] instance.  And the only way forward is to call the [`open()`] method to downcast sink
/// into the underlying type that gives access to the data that the sink have stored.
pub trait Sealed: Send {
    /// Converts a "sealed" sink into [`Any`] that can be later cast into a specific type that the
    /// consumer of the full sink can use to extract contained data.
    ///
    /// TODO It would be awesome to have a method that converts `Sealed` into a specific underlying
    /// type directly.  With the underlying type somehow only known by the code that constructed
    /// the `Sink` in the first place.  And not to the code that is operating on the
    /// `Sink`/`Sealed` values via the trait.  But I could not find a way to express that.
    ///
    /// Seems like an associated type would express that, but if I add an associated type to both
    /// `Sink` and `Sealed`, I need any method that consumes the sink to be generic over this type.
    /// And that means that sink consumers are not object safe :(  I think generic methods might be
    /// object safe, as long as they do not use their type arguments, which seems pointless, but
    /// here would be used to make sure that the type of the returned object is preserved.  Maybe
    /// `?Sized` would be a good boundary for generic methods allowed in traits?
    fn open(self: Box<Self>) -> Box<dyn Any>;
}

/// Result of the [`Sink::append`] method. See there for details.
pub enum AppendResult<TraversalPosition>
where
    TraversalPosition: Default + Send + 'static,
{
    /// Sink have consumed the value and may consume more.
    Ok(Box<dyn Sink<TraversalPosition>>),
    /// Sink could not consume the last value provided.  It should have remembered the traversal
    /// position given to the most recent [`Sink::append()`] call, allowing the sink owner to
    /// resume the operation later from the same standpoint.
    Sealed(Box<dyn Sealed>),
}
