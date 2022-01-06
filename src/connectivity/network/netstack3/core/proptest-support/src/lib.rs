// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Support for property test usage in netstack3 core.

use std::{any::Any, fmt::Debug, str::FromStr as _};

use proptest::test_runner::{FailurePersistence, PersistedSeed};

/// Persists all failed seeds to the source file.
///
/// Proptest generates nondeterministic tests in the sense that different seeds
/// will be used in each run, this is intended because we expect the properties
/// to hold for all possible inputs. So test flakes should be treated as test
/// failures. This struct configures the containing proptest to log the seeds
/// that caused test failures, please add all those seeds to the source file
/// so that the test failure can be persisted.
#[derive(Clone, Debug, PartialEq)]
pub struct FailedSeeds(pub Vec<&'static str>);

impl FailurePersistence for FailedSeeds {
    fn load_persisted_failures2(&self, _source_file: Option<&'static str>) -> Vec<PersistedSeed> {
        let Self(seeds) = self;
        seeds.iter().map(|s| PersistedSeed::from_str(s).expect("malformed seed")).collect()
    }

    fn save_persisted_failure2(
        &mut self,
        source_file: Option<&'static str>,
        seed: PersistedSeed,
        shrunken_value: &dyn Debug,
    ) {
        eprintln!("Test failed when: {:?}", shrunken_value);
        eprintln!("To reproduce this failure please add the following line:");
        // The `Display` and `FromStr` impl for `PersistedSeed` are inverse
        // of each other.
        eprintln!("\"{}\"", seed);
        eprintln!("to the test config in file {}", source_file.expect("failed to get source file"));
    }

    fn box_clone(&self) -> Box<dyn FailurePersistence> {
        Box::new(self.clone())
    }

    fn eq(&self, other: &dyn FailurePersistence) -> bool {
        other.as_any().downcast_ref::<Self>().map_or(false, |x| x == self)
    }

    fn as_any(&self) -> &dyn Any {
        self
    }
}

#[macro_export]
macro_rules! failed_seeds {
    ($($seed:literal),*) => {
        Some({
            use alloc::{boxed::Box, vec};
            Box::new(proptest_support::FailedSeeds(vec![$($seed),*]))
        })
    }
}
