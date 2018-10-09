// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_mlme::MlmeEvent,
    futures::channel::mpsc,
    log::{debug},
    crate::sink::MlmeSink,
};


// A token is an opaque value that identifies a particular request from a user.
// To avoid parameterizing over many different token types, we introduce a helper
// trait that enables us to group them into a single generic parameter.
pub trait Tokens {}

mod internal {
    pub type UserSink<T> = crate::sink::UnboundedSink<super::UserEvent<T>>;
}
use self::internal::*;

pub type UserStream<T> = mpsc::UnboundedReceiver<UserEvent<T>>;

pub struct MeshSme<T: Tokens> {
    _mlme_sink: MlmeSink,
    _user_sink: UserSink<T>,
}

// A message from the Mesh node to a user or a group of listeners
#[derive(Debug)]
pub enum UserEvent<T: Tokens> {
    _Dummy(::std::marker::PhantomData<T>)
}

impl<T: Tokens> super::Station for MeshSme<T> {
    fn on_mlme_event(&mut self, event: MlmeEvent) {
        debug!("received MLME event: {:?}", event);
    }
}

impl<T: Tokens> MeshSme<T> {
    pub fn new() -> (Self, crate::MlmeStream, UserStream<T>) {
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (user_sink, user_stream) = mpsc::unbounded();
        let sme = MeshSme {
            _mlme_sink: MlmeSink::new(mlme_sink),
            _user_sink: UserSink::new(user_sink),
        };
        (sme, mlme_stream, user_stream)
    }
}
