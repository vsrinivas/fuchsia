// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use crate::labels::NodeId;
use crate::security_context::SecurityContext;
use anyhow::Error;
use std::sync::Arc;
use std::sync::Once;

const LOG_LEVEL: log::Level = log::Level::Info;
const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

struct Logger;

fn short_log_level(level: &log::Level) -> &'static str {
    match *level {
        log::Level::Error => "E",
        log::Level::Warn => "W",
        log::Level::Info => "I",
        log::Level::Debug => "D",
        log::Level::Trace => "T",
    }
}

impl log::Log for Logger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() <= LOG_LEVEL
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            let msg = format!(
                "{:?} {:?} {} {} [{}]: {}",
                std::time::Instant::now(),
                std::thread::current().id(),
                record.target(),
                record
                    .file()
                    .map(|file| {
                        if let Some(line) = record.line() {
                            format!("{}:{}: ", file, line)
                        } else {
                            format!("{}: ", file)
                        }
                    })
                    .unwrap_or(String::new()),
                short_log_level(&record.level()),
                record.args()
            );
            let _ = std::panic::catch_unwind(|| {
                println!("{}", msg);
            });
        }
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;
static START: Once = Once::new();

pub fn init() {
    START.call_once(|| {
        log::set_logger(&LOGGER).unwrap();
        log::set_max_level(MAX_LOG_LEVEL);
    })
}

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
    pub fn new(test_name: &'static str, run: usize) -> NodeIdGenerator {
        NodeIdGenerator {
            test_id: crc::crc16::checksum_x25(test_name.as_bytes()) as u64,
            test_name,
            run: run as u64,
            n: 1,
        }
    }

    pub fn new_router(&mut self) -> Result<Arc<crate::router::Router>, Error> {
        crate::router::Router::new(
            crate::router::RouterOptions::new()
                .set_node_id(self.next().ok_or(anyhow::format_err!("No more node ids available"))?),
            Box::new(test_security_context()),
        )
    }

    pub fn test_desc(&self) -> String {
        format!("({}) {}", self.node_id(0).0, self.test_name)
    }

    pub fn node_id(&self, idx: u64) -> NodeId {
        (self.test_id * 10000 * 100000 + self.run * 10000 + idx).into()
    }
}

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
    return crate::security_context::StringSecurityContext {
        node_cert: path_for("cert.crt"),
        node_private_key: path_for("cert.key"),
        root_cert: path_for("rootca.crt"),
    };
}
