// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Number of polls taken to produce the intial sample.
pub const INITIAL_SAMPLE_POLLS: usize = 2;
/// Number of polls to produce all subsequent samples.
pub const SAMPLE_POLLS: usize = 5;
/// Number of samples produced during the convergence phase, during which samples are
/// produced relatively frequently.
pub const CONVERGE_SAMPLES: usize = 20;
/// Maximum random multiplier used to modify the timing between samples. A random multiplier is
/// chosen as 1 + rand([-max randomizer, max randomizer]) to alter the wait periods between
/// samples.
pub const MAX_TIME_BETWEEN_SAMPLES_RANDOMIZATION: f32 = 0.05;
