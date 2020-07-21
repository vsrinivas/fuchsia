// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::{Fixture, Target};
use crate::test_util::NodeIdGenerator;
use fidl_handle_tests::{socket, LoggingFixture};
use fuchsia_async::Task;

struct SockFixture {
    fixture: Fixture,
}

impl SockFixture {
    async fn new(node_id_gen: NodeIdGenerator) -> SockFixture {
        SockFixture { fixture: Fixture::new(node_id_gen).await }
    }
}

impl socket::Fixture for SockFixture {
    fn create_handles(&self, opts: fidl::SocketOpts) -> (fidl::Socket, fidl::Socket) {
        let (local, remote) = fidl::Socket::create(opts).unwrap();
        (local, self.fixture.distribute_handle(remote, Target::B))
    }
}

impl LoggingFixture for SockFixture {
    fn log(&mut self, msg: &str) {
        self.fixture.log(msg)
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn fidl_socket_tests(run: usize) {
    crate::test_util::init();
    let node_id_gen = NodeIdGenerator::new("fidl_socket_tests", run);
    let fixture = SockFixture::new(node_id_gen).await;
    Task::blocking(async move { socket::run(fixture) }).await
}
