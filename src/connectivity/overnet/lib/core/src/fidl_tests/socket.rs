// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::{Fixture, Target};
use fidl_handle_tests::{socket, LoggingFixture};

struct SockFixture {
    fixture: Fixture,
}

impl SockFixture {
    fn new(test_name: &'static str) -> SockFixture {
        SockFixture { fixture: Fixture::new(test_name) }
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

#[test]
fn fidl_socket_tests() {
    super::run_test(move || socket::run(SockFixture::new("fidl_socket_tests")))
}
