// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Standardized component inspection
//!
//! This mod contains standardized entry points to the Fuchsia inspect subsystem.  The `inspector()`
//! function can be used to get a top level inspector, which ensures consistent inspect behavior
//! across components.
//!
//! Use the `health()` function to report the component health state through the component inspector.
//!
//! While using the component inspector is not mandatory, it is probably a good idea from the
//! standpoint of uniform reporting.
//!
//! # Examples
//!
//! ```rust
//! use fuchsia_inspect::component;
//! let inspector = component::inspector();
//! // Add a standardized health node to the default inspector as early as possible in code.
//! // The component will report `STARTING_UP` as the status from here on.
//! let mut health = component::health();
//!
//! // Add a node with a metric to the inspector.
//! inspector.root().create_string("property", "value");
//!
//! // Report the component health as `OK` when ready.  Calls to `health` are thread-safe.
//! health.set_ok();
//! ```

use {
    super::{health, Inspector},
    lazy_static::lazy_static,
    std::sync::{Arc, Mutex},
};

lazy_static! {
  // The component-level inspector.  We probably want to use this inspector across components where
  // practical.
  static ref INSPECTOR: Inspector = Inspector::new();

  // Health node based on the global inspector from `inspector()`.
  static ref HEALTH: Arc<Mutex<health::Node>> = Arc::new(Mutex::new(health::Node::new(INSPECTOR.root())));
}

/// A thread-safe handle to a health reporter.  See `component::health()` for instructions on how
/// to create one.
pub struct Health {
    // The thread-safe component health reporter that reports to the top-level inspector.
    health_node: Arc<Mutex<health::Node>>,
}

// A thread-safe implementation of a global health reporter.
impl health::Reporter for Health {
    fn set_starting_up(&mut self) {
        let lock = self.health_node.lock();
        lock.expect("lock success").set_starting_up();
    }
    fn set_ok(&mut self) {
        let lock = self.health_node.lock();
        lock.expect("lock success").set_ok();
    }
    fn set_unhealthy(&mut self, message: &str) {
        let lock = self.health_node.lock();
        lock.expect("lock success").set_unhealthy(message);
    }
}

/// Returns the singleton component inspector.
///
/// It is recommended that all health nodes register with this inspector (as opposed to any other
/// that may have been created).
pub fn inspector() -> &'static Inspector {
    return &INSPECTOR;
}

/// Returns a handle to the standardized singleton top-level health reporter on each call.
///
/// Calling this function installs a health reporting child node below the default inspector's
/// `root` node.  When using it, consider using the default inspector for all health reporting, for
/// uniformity: `fuchsia_inspect::component::inspector()`.
///
/// # Caveats
///
/// The health reporting node is created when it is first referenced.  It is advisable to reference
/// it as early as possible, so that it could export a `STARTING_UP` health status while the
/// component is initializing.
pub fn health() -> Health {
    return Health { health_node: HEALTH.clone() };
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{assert_inspect_tree, health::Reporter, testing::AnyProperty},
    };

    #[test]
    fn health_checker_lifecycle() {
        let inspector = super::inspector();
        // In the beginning, the inspector has no stats.
        assert_inspect_tree!(inspector, root: contains {});

        let mut health = health();
        assert_inspect_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "STARTING_UP",
                start_timestamp_nanos: AnyProperty,
            }
        });

        health.set_ok();
        assert_inspect_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });

        health.set_unhealthy("Bad state");
        assert_inspect_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "UNHEALTHY",
                message: "Bad state",
                start_timestamp_nanos: AnyProperty,
            }
        });

        // Verify that the message changes.
        health.set_unhealthy("Another bad state");
        assert_inspect_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "UNHEALTHY",
                message: "Another bad state",
                start_timestamp_nanos: AnyProperty,
            }
        });

        // Also verifies that there is no more message.
        health.set_ok();
        assert_inspect_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });
    }

    #[test]
    fn record_on_inspector() {
        let inspector = super::inspector();
        inspector.root().record_int("a", 1);
        assert_inspect_tree!(inspector, root: contains {
            a: 1i64,
        })
    }
}
