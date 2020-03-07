// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::{Fixture, Target};
use fidl_handle_tests::socket;

struct SockFixture {
    fixture: Fixture,
}

impl SockFixture {
    fn new() -> SockFixture {
        SockFixture { fixture: Fixture::new() }
    }
}

impl socket::Fixture for SockFixture {
    fn create_handles(&self, opts: fidl::SocketOpts) -> (fidl::Socket, fidl::Socket) {
        let (local, remote) = fidl::Socket::create(opts).unwrap();
        (local, self.fixture.distribute_handle(remote, Target::B))
    }
}

#[test]
fn fidl_socket_tests() {
    socket::run(SockFixture::new())
}
