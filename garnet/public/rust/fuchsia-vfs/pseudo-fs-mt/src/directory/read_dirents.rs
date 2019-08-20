// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A sink that can consume directory entry information, encoding them as expected by the `io.fidl`
//! `Directory::ReadDirents` result.

use crate::directory::{
    common::encode_dirent,
    dirents_sink::{self, AppendResult},
    entry::EntryInfo,
};

use {
    fuchsia_zircon::Status,
    std::{any::Any, marker::PhantomData},
};

/// An instance of this type represents a sink that may still accept additional entries.  Depending
/// on the entry size it may turn itself into a [`Done`] value, indicating that the internal buffer
/// is full.
pub struct Sink<TraversalPosition>
where
    TraversalPosition: Default + Send + 'static,
{
    buf: Vec<u8>,
    max_bytes: u64,
    state: SinkState,
    phantom: PhantomData<TraversalPosition>,
}

/// An instance of this type is a `ReadDirents` result sink that is full and may not consume any
/// more values.
pub struct Done<TraversalPosition>
where
    TraversalPosition: Default + Send + 'static,
{
    pub(super) buf: Vec<u8>,
    pub(super) pos: TraversalPosition,
    pub(super) status: Status,
}

#[derive(PartialEq, Eq)]
enum SinkState {
    NotCalled,
    DidNotFit,
    FitOne,
}

impl<TraversalPosition> Sink<TraversalPosition>
where
    TraversalPosition: Default + Send + 'static,
{
    /// Constructs a new sync that will have the specified number of bytes of storage.
    pub(super) fn new(max_bytes: u64) -> Box<Sink<TraversalPosition>> {
        Box::new(Sink::<TraversalPosition> {
            buf: vec![],
            max_bytes,
            state: SinkState::NotCalled,
            phantom: PhantomData,
        })
    }
}

impl<TraversalPosition> dirents_sink::Sink<TraversalPosition> for Sink<TraversalPosition>
where
    TraversalPosition: Default + Send + 'static,
{
    fn append(
        mut self: Box<Self>,
        entry: &EntryInfo,
        name: &str,
        pos: &dyn Fn() -> TraversalPosition,
    ) -> AppendResult<TraversalPosition> {
        if !encode_dirent(&mut self.buf, self.max_bytes, entry, name) {
            if self.state == SinkState::NotCalled {
                self.state = SinkState::DidNotFit;
            }
            AppendResult::Sealed(self.seal(pos()))
        } else {
            if self.state == SinkState::NotCalled {
                self.state = SinkState::FitOne;
            }
            AppendResult::Ok(self)
        }
    }

    fn seal(self: Box<Self>, pos: TraversalPosition) -> Box<dyn dirents_sink::Sealed> {
        Box::new(Done::<TraversalPosition> {
            buf: self.buf,
            pos,
            status: match self.state {
                SinkState::NotCalled | SinkState::FitOne => Status::OK,
                SinkState::DidNotFit => Status::BUFFER_TOO_SMALL,
            },
        })
    }
}

impl<TraversalPosition> dirents_sink::Sealed for Done<TraversalPosition>
where
    TraversalPosition: Default + Send + 'static,
{
    fn open(self: Box<Self>) -> Box<dyn Any> {
        self
    }
}
