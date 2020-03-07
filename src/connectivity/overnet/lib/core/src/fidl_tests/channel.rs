// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::{Fixture, Target};
use fidl_handle_tests::channel;

struct ChanFixture {
    fixture: Fixture,
    map_purpose_to_target: Box<dyn Fn(channel::CreateHandlePurpose) -> Target>,
}

impl ChanFixture {
    fn new(
        map_purpose_to_target: impl 'static + Fn(channel::CreateHandlePurpose) -> Target,
    ) -> ChanFixture {
        ChanFixture {
            fixture: Fixture::new(),
            map_purpose_to_target: Box::new(map_purpose_to_target),
        }
    }
}

impl channel::Fixture for ChanFixture {
    fn create_handles(
        &self,
        purpose: channel::CreateHandlePurpose,
    ) -> (fidl::Channel, fidl::Channel) {
        let (local, remote) = fidl::Channel::create().unwrap();
        (local, self.fixture.distribute_handle(remote, (self.map_purpose_to_target)(purpose)))
    }
}

#[test]
fn fidl_channel_tests_no_transfer() {
    channel::run(ChanFixture::new(|purpose| match purpose {
        channel::CreateHandlePurpose::PrimaryTestChannel => Target::B,
        channel::CreateHandlePurpose::PayloadChannel => Target::A,
    }))
}

#[test]
fn fidl_channel_tests_all_to_b() {
    channel::run(ChanFixture::new(|_| Target::B))
}

#[test]
fn fidl_channel_tests_b_then_c() {
    channel::run(ChanFixture::new(|purpose| match purpose {
        channel::CreateHandlePurpose::PrimaryTestChannel => Target::B,
        channel::CreateHandlePurpose::PayloadChannel => Target::C,
    }))
}
