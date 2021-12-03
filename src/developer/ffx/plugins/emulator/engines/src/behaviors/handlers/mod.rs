// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains the various Behavior types used by the emulator plugin. Each Behavior is
/// defined in its own file, then exported from here for general use.
///
/// A Behavior is a generic abstraction for a configuration choice. For example, the no_kvm behavior
/// instructs the emulator to run without hardware acceleration. The command-line flags needed to
/// get the emulator to do this are defined in the virtual device structure in the product bundle
/// manifest, grouped by the engine they pertain to (since a Behavior can be used across multiple
/// engine types), while the Rust code for setup, cleanup, and selection/filtering is defined here
/// in the types module.
///
/// In Rust, a Behavior has a set of data fields which are engine-specific, and implements the
/// BehaviorTrait, which has methods for setting up the Behavior, cleaning it up on shutdown, and
/// a "filter" method to determine if it should be used on this invocation. A filter can always
/// return Ok(()), indicating it should always be used, or it can validate against anything in the
/// EmulatorConfiguration to decide if it should pertain to that instance or not.
///
/// In the trivial case, where there is no setup or cleanup needed for the Behavior, and it should
/// always be included, a SimpleBehavior is provided.
pub mod hvf_behavior;
pub mod kvm_behavior;
pub mod no_acceleration_behavior;
pub mod simple_behavior;
