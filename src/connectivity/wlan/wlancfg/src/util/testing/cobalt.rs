// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {fidl_fuchsia_cobalt::CobaltEvent, fuchsia_cobalt::CobaltSender, futures::channel::mpsc};

pub fn create_mock_cobalt_sender() -> CobaltSender {
    create_mock_cobalt_sender_and_receiver().0
}

pub fn create_mock_cobalt_sender_and_receiver() -> (CobaltSender, mpsc::Receiver<CobaltEvent>) {
    // Arbitrary number that is (much) larger than the count of metrics we might send to it
    const MOCK_COBALT_MSG_BUFFER: usize = 100;
    let (cobalt_mpsc_sender, cobalt_mpsc_receiver) = mpsc::channel(MOCK_COBALT_MSG_BUFFER);
    (CobaltSender::new(cobalt_mpsc_sender), cobalt_mpsc_receiver)
}

#[macro_export]
macro_rules! validate_cobalt_events {
    ($receiver:expr, $metric_id:expr, $event_code:expr) => {
        assert_eq!(
            $receiver.try_next().unwrap(),
            Some(
                CobaltEvent::builder($metric_id)
                    .with_event_code($event_code.as_event_code())
                    .as_event()
            )
        );
    };
}

#[macro_export]
macro_rules! validate_no_cobalt_events {
    ($receiver:expr) => {
        match $receiver.try_next() {
            Ok(result) => assert_eq!(result, None),
            Err(_) => (), // An error means the other end is closed, which is expected
        }
    };
}
