// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::channel::mpsc;

#[derive(Debug)]
pub struct UnboundedSink<T> {
    sink: mpsc::UnboundedSender<T>,
}

impl<T> UnboundedSink<T> {
    pub fn new(sink: mpsc::UnboundedSender<T>) -> Self {
        UnboundedSink { sink }
    }

    pub fn send(&self, msg: T) {
        match self.sink.unbounded_send(msg) {
            Ok(()) => {}
            Err(e) => {
                if e.is_full() {
                    panic!("Did not expect an unbounded channel to be full: {:?}", e);
                }
                // If the other side has disconnected, we can still technically function,
                // so ignore the error.
            }
        }
    }
}

pub type MlmeSink = UnboundedSink<crate::MlmeRequest>;
pub type InfoSink = UnboundedSink<crate::InfoEvent>;
