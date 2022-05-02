// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Specific declarations and convenience methods for running blackout in CI.
//!
//! When running in CI, we are expected to be run using the standard rust test harness, instead of
//! just running the binary directly like people will do locally. This method has a couple of
//! caveats, most notably that we can't be passed command line options, which means we have to
//! collect the options in a different way. To this end, we manually construct the options
//! according to a well-known contract. This isn't how tests should be run locally - it's
//! infinitely more flexible to run the binary with command line arguments.

use crate::CommonOpts;

/// We hard-code the block device. This assumes that we are being booted in a netbooted environment;
/// we clobber the entire fvm with our own filesystem, which could be bad if we have to worry about
/// things like "system partitions".
const BLOCK_DEVICE: &'static str = "/dev/sys/platform/05:00:f/aml-raw_nand/nand/fvm/ftl/block";

/// Construct the options for running in an infra-specific environment.
pub fn options() -> CommonOpts {
    CommonOpts {
        block_device: BLOCK_DEVICE.into(),
        seed: None,
        relay: None,
        // eventually we may want to bump this up to do multiple iterations in a single run.
        iterations: None,
        run_until_failure: false,
    }
}
