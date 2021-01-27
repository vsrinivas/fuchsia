// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::actor::ActorConfig, futures::future::BoxFuture};

/// Every stress test must implement this trait exactly once and pass it
/// to run_test(). The test loop uses these methods to drive the stress test.
///
/// The environment is responsible for:
/// * creating actors that run during the stress test
/// * creating a new instance when an actor requests one
/// * specifying success criteria for the test (num operations or timeout)
pub trait Environment: Send + Sync {
    /// Returns the target number of operations to complete before exiting
    fn target_operations(&self) -> Option<u64>;

    /// Returns the number of seconds to wait before exiting
    fn timeout_seconds(&self) -> Option<u64>;

    /// Return the configuration for all actors
    fn actor_configs<'a>(&'a mut self) -> Vec<ActorConfig<'a>>;

    /// Reset the environment, when an actor requests one
    fn reset(&mut self) -> BoxFuture<'_, ()>;
}
