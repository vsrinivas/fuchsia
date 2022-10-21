// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::labels::NodeId;
use crate::router::security_context::SecurityContext;
use anyhow::Error;
use std::sync::Arc;

pub static TEST_ROUTER_INTERVAL: std::time::Duration = std::time::Duration::from_millis(500);

/// Generates node id's for tests in a repeatable fashion.
pub struct NodeIdGenerator {
    test_id: u64,
    test_name: &'static str,
    run: u64,
    n: u64,
}

impl Iterator for NodeIdGenerator {
    type Item = NodeId;
    fn next(&mut self) -> Option<NodeId> {
        let id = self.n;
        if id >= 100 {
            return None;
        }
        self.n += 1;
        Some(self.node_id(id))
    }
}

impl NodeIdGenerator {
    /// Create a new generator; uses the test name as salt for the node id's generated.
    pub fn new(test_name: &'static str, run: usize) -> NodeIdGenerator {
        NodeIdGenerator {
            test_id: crc::crc16::checksum_x25(test_name.as_bytes()) as u64,
            test_name,
            run: run as u64,
            n: 1,
        }
    }

    /// Create a new router with a unique (within this test run) node id.
    pub fn new_router(&mut self) -> Result<Arc<crate::router::Router>, Error> {
        crate::router::Router::new(
            crate::router::RouterOptions::new()
                .set_node_id(self.next().ok_or(anyhow::format_err!("No more node ids available"))?),
            Box::new(test_security_context()),
        )
    }

    /// Like [`new_router`] but enables circuit route forwarding.
    pub fn new_router_circuit_router(&mut self) -> Result<Arc<crate::router::Router>, Error> {
        crate::router::Router::new(
            crate::router::RouterOptions::new()
                .set_node_id(self.next().ok_or(anyhow::format_err!("No more node ids available"))?)
                .set_router_interval(TEST_ROUTER_INTERVAL),
            Box::new(test_security_context()),
        )
    }

    /// Returns a string describing this test (including the base node id so it can be grepped).
    pub fn test_desc(&self) -> String {
        format!("({}) {}", self.node_id(0).0, self.test_name)
    }

    /// Generate the `idx` node id in this set.
    pub fn node_id(&self, idx: u64) -> NodeId {
        (self.test_id * 10000 * 100000 + self.run * 10000 + idx).into()
    }
}

#[cfg(test)]
mod test {

    #[fuchsia::test]
    fn node_id_generator_works() {
        let n = super::NodeIdGenerator::new("foo", 3);
        assert_eq!(n.node_id(1), 26676000030001.into());
        assert_eq!(&n.test_desc(), "(26676000030000) foo");
    }
}

/// Create a security context that can be used for testing.
pub fn test_security_context() -> impl SecurityContext {
    #[cfg(not(target_os = "fuchsia"))]
    let path_for = |name| {
        let relative_path = &format!("overnet_test_certs/{}", name);
        let mut path = std::env::current_exe().unwrap();
        // We don't know exactly where the binary is in the out directory (varies by target platform and
        // architecture), so search up the file tree for the certificate file.
        loop {
            if path.join(relative_path).exists() {
                path.push(relative_path);
                break;
            }
            if !path.pop() {
                // Reached the root of the file system
                panic!(
                    "Couldn't find {} near {:?}",
                    relative_path,
                    std::env::current_exe().unwrap()
                );
            }
        }
        path.to_str().unwrap().to_string()
    };
    #[cfg(target_os = "fuchsia")]
    let path_for = |name| format!("/pkg/data/{}", name);
    return crate::router::security_context::StringSecurityContext {
        node_cert: path_for("cert.crt"),
        node_private_key: path_for("cert.key"),
        root_cert: path_for("rootca.crt"),
    };
}
