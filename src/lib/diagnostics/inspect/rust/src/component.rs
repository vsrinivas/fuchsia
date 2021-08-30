// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Component inspection utilities
//!
//!
//! This module contains standardized entry points to the Fuchsia inspect subsystem. It works based
//! on the assumpton that a top-level static [`Inspector`][Inspector] is desirable.
//!
//! The [`inspector()`][inspector] function can be used to get a top level inspector, which ensures
//! consistent inspect behavior across components.
//!
//! Use the [`health()`][health] function to report the component health state through the
//! component inspector.
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

use super::{health, stats, Inspector, LazyNode};
use inspect_format::constants;
use lazy_static::lazy_static;
use parking_lot::Mutex;
use std::sync::Arc;

lazy_static! {
  // The size with which the default inspector is initialized.
  static ref INSPECTOR_SIZE : Mutex<usize> = Mutex::new(constants::DEFAULT_VMO_SIZE_BYTES);

  // The component-level inspector.  We probably want to use this inspector across components where
  // practical.
  static ref INSPECTOR: Inspector = Inspector::new_with_size(*INSPECTOR_SIZE.lock());

  // Health node based on the global inspector from `inspector()`.
  static ref HEALTH: Arc<Mutex<health::Node>> = Arc::new(Mutex::new(health::Node::new(INSPECTOR.root())));

  // Stats node based on the global inspector from `inspector()`.
  static ref STATS: stats::Node<LazyNode> = stats::Node::new(&INSPECTOR, INSPECTOR.root());
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
        self.health_node.lock().set_starting_up();
    }
    fn set_ok(&mut self) {
        self.health_node.lock().set_ok();
    }
    fn set_unhealthy(&mut self, message: &str) {
        self.health_node.lock().set_unhealthy(message);
    }
}

/// Returns the singleton component inspector.
///
/// It is recommended that all health nodes register with this inspector (as opposed to any other
/// that may have been created).
pub fn inspector() -> &'static Inspector {
    &INSPECTOR
}

/// Initializes and returns the singleton component inspector.
pub fn init_inspector_with_size(max_size: usize) -> &'static Inspector {
    lazy_static::initialize(&INSPECTOR_SIZE);
    *INSPECTOR_SIZE.lock() = max_size;
    lazy_static::initialize(&INSPECTOR);
    &INSPECTOR
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

/// Serves statistics about inspect such as size or number of dynamic children in the
/// `fuchsia.inspect.Stats` lazy node.
pub fn serve_inspect_stats() {
    lazy_static::initialize(&STATS);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{assert_data_tree, health::Reporter, testing::AnyProperty};
    use futures::FutureExt;

    #[test]
    fn health_checker_lifecycle() {
        let inspector = super::inspector();
        // In the beginning, the inspector has no stats.
        assert_data_tree!(inspector, root: contains {});

        let mut health = health();
        assert_data_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "STARTING_UP",
                start_timestamp_nanos: AnyProperty,
            }
        });

        health.set_ok();
        assert_data_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "OK",
                start_timestamp_nanos: AnyProperty,
            }
        });

        health.set_unhealthy("Bad state");
        assert_data_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "UNHEALTHY",
                message: "Bad state",
                start_timestamp_nanos: AnyProperty,
            }
        });

        // Verify that the message changes.
        health.set_unhealthy("Another bad state");
        assert_data_tree!(inspector,
        root: contains {
            "fuchsia.inspect.Health": {
                status: "UNHEALTHY",
                message: "Another bad state",
                start_timestamp_nanos: AnyProperty,
            }
        });

        // Also verifies that there is no more message.
        health.set_ok();
        assert_data_tree!(inspector,
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
        assert_eq!(
            inspector.vmo.as_ref().unwrap().get_size().unwrap(),
            constants::DEFAULT_VMO_SIZE_BYTES as u64
        );
        inspector.root().record_int("a", 1);
        assert_data_tree!(inspector, root: contains {
            a: 1i64,
        })
    }

    #[test]
    fn init_inspector_with_size() {
        super::init_inspector_with_size(8192);
        assert_eq!(super::inspector().vmo.as_ref().unwrap().get_size().unwrap(), 8192);
    }

    #[test]
    fn inspect_stats() {
        let inspector = super::inspector();
        super::serve_inspect_stats();
        inspector.root().record_lazy_child("foo", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_uint("a", 1);
                Ok(inspector)
            }
            .boxed()
        });
        assert_data_tree!(inspector, root: {
            foo: {
                a: 1u64,
            },
            "fuchsia.inspect.Stats": {
                current_size: 4096u64,
                maximum_size: constants::DEFAULT_VMO_SIZE_BYTES as u64,
                total_dynamic_children: 2u64,
                allocated_blocks: 7u64,
                deallocated_blocks: 0u64,
                failed_allocations: 0u64,
            }
        });
    }
}
