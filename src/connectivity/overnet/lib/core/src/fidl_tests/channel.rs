// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::{Fixture, Target};
use crate::test_util::NodeIdGenerator;
use fidl_handle_tests::{channel, LoggingFixture};
use fuchsia_async::Task;

struct ChanFixture {
    fixture: Fixture,
    map_purpose_to_target: Box<dyn Send + Fn(channel::CreateHandlePurpose) -> Target>,
}

impl ChanFixture {
    async fn new(
        node_id_gen: NodeIdGenerator,
        map_purpose_to_target: impl 'static + Send + Fn(channel::CreateHandlePurpose) -> Target,
    ) -> ChanFixture {
        ChanFixture {
            fixture: Fixture::new(node_id_gen).await,
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

#[fuchsia_async::run_singlethreaded(test)]
async fn fidl_channel_tests_no_transfer(run: usize) {
    crate::test_util::init();
    let node_id_gen = NodeIdGenerator::new("fidl_channel_tests_no_transfer", run);
    let fixture = ChanFixture::new(node_id_gen, |purpose| match purpose {
        channel::CreateHandlePurpose::PrimaryTestChannel => Target::B,
        channel::CreateHandlePurpose::PayloadChannel => Target::A,
    })
    .await;
    Task::blocking(async move { channel::run(fixture) }).await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn fidl_channel_tests_all_to_b(run: usize) {
    crate::test_util::init();
    let node_id_gen = NodeIdGenerator::new("fidl_channel_tests_all_to_b", run);
    let fixture = ChanFixture::new(node_id_gen, |_| Target::B).await;
    Task::blocking(async move { channel::run(fixture) }).await
}

#[fuchsia_async::run_singlethreaded(test)]
async fn fidl_channel_tests_b_then_c(run: usize) {
    crate::test_util::init();
    let node_id_gen = NodeIdGenerator::new("fidl_channel_tests_b_then_c", run);
    let fixture = ChanFixture::new(node_id_gen, |purpose| match purpose {
        channel::CreateHandlePurpose::PrimaryTestChannel => Target::B,
        channel::CreateHandlePurpose::PayloadChannel => Target::C,
    })
    .await;
    Task::blocking(async move { channel::run(fixture) }).await
}
