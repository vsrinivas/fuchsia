// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::{Fixture, Target};
use fidl_handle_tests::{channel, LoggingFixture};

struct ChanFixture {
    fixture: Fixture,
    map_purpose_to_target: Box<dyn Send + Fn(channel::CreateHandlePurpose) -> Target>,
}

impl ChanFixture {
    fn new(
        test_name: &'static str,
        map_purpose_to_target: impl 'static + Send + Fn(channel::CreateHandlePurpose) -> Target,
    ) -> ChanFixture {
        ChanFixture {
            fixture: Fixture::new(test_name),
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

impl LoggingFixture for ChanFixture {
    fn log(&mut self, msg: &str) {
        self.fixture.log(msg)
    }
}

#[test]
fn fidl_channel_tests_no_transfer() {
    super::run_test(move || {
        channel::run(ChanFixture::new("fidl_channel_tests_no_transfer", |purpose| match purpose {
            channel::CreateHandlePurpose::PrimaryTestChannel => Target::B,
            channel::CreateHandlePurpose::PayloadChannel => Target::A,
        }))
    })
}

#[test]
fn fidl_channel_tests_all_to_b() {
    super::run_test(move || {
        channel::run(ChanFixture::new("fidl_channel_tests_all_to_b", |_| Target::B))
    })
}

#[test]
fn fidl_channel_tests_b_then_c() {
    super::run_test(move || {
        channel::run(ChanFixture::new("fidl_channel_tests_b_then_c", |purpose| match purpose {
            channel::CreateHandlePurpose::PrimaryTestChannel => Target::B,
            channel::CreateHandlePurpose::PayloadChannel => Target::C,
        }))
    })
}
